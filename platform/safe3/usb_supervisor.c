/* SPDX-License-Identifier: GPL-3.0-or-later */

/* Supervisor-backed USB controller for genuine Trezor Core. Core constructs
 * its device and interface configuration through the normal usb_* API. This
 * driver projects that state into a typed UsbPersonality, while data endpoint
 * files remain direct FunctionFS capabilities in the worker. */

#define _GNU_SOURCE

#include <trezor_rtl.h>

#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <io/usb.h>
#include <io/usb_hid.h>
#include <io/usb_vcp.h>
#include <io/usb_webusb.h>
#include <sys/sysevent_source.h>
#include <sys/systask.h>

#include <usb_personality_ffi.h>
#include <usb_worker_protocol.h>

#include "supervisor_resources.h"

#define CONTROL_FD 3
#define PROTOCOL_HEADER_LENGTH 20U
#define MAX_RECORD_LENGTH (1024U * 1024U)
#define MAX_TRANSFERRED_FDS 32U
#define MAX_INTERFACES 8U
#define MAX_ENDPOINT_NUMBER 8U

#define KIND_INITIAL_RESOURCES 0x01
#define KIND_USB_ENDPOINTS 0x02
#define KIND_USB_BUS_EVENT 0x03
#define KIND_USB_CONTROL_REQUEST 0x04
#define KIND_QUIESCE 0x11
#define KIND_CONFIGURATION_REJECTED 0x12
#define KIND_CONFIGURE 0x80
#define KIND_USB_CONTROL_RESPONSE 0x81
#define KIND_SERVING 0x82
#define KIND_QUIESCED 0x84

#define RESPONSE_STALL 0
#define RESPONSE_ACK 1
#define RESPONSE_DATA 2

#define USB_DT_STRING 3
#define USB_DT_HID 0x21
#define USB_DT_REPORT 0x22

#define USB_REQ_GET_STATUS 0x00
#define USB_REQ_CLEAR_FEATURE 0x01
#define USB_REQ_SET_FEATURE 0x03
#define USB_REQ_GET_DESCRIPTOR 0x06
#define USB_REQ_GET_CONFIGURATION 0x08
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_REQ_GET_INTERFACE 0x0a
#define USB_REQ_SET_INTERFACE 0x0b

#define USB_HID_GET_REPORT 0x01
#define USB_HID_GET_IDLE 0x02
#define USB_HID_GET_PROTOCOL 0x03
#define USB_HID_SET_REPORT 0x09
#define USB_HID_SET_IDLE 0x0a
#define USB_HID_SET_PROTOCOL 0x0b

typedef enum {
  IFACE_DISABLED = 0,
  IFACE_WEBUSB,
  IFACE_HID,
} interface_type_t;

struct worker_record {
  uint8_t kind;
  uint32_t generation;
  uint32_t request_id;
  uint8_t *body;
  size_t body_length;
  int fds[MAX_TRANSFERRED_FDS];
  size_t fd_count;
};

typedef struct {
  int fd;
  int event_fd;
  pthread_t thread;
  pthread_mutex_t mutex;
  pthread_cond_t produced;
  pthread_cond_t consumed;
  bool synchronization_initialized;
  bool thread_running;
  bool occupied;
  uint8_t address;
  uint8_t transfer_type;
  uint16_t max_packet_size;
  uint16_t packet_length;
  uint8_t *packet;
  uint8_t *read_buffer;
} endpoint_t;

typedef struct {
  interface_type_t type;
  syshandle_t handle;
  uint8_t number;
  uint8_t subclass;
  uint8_t protocol;
  uint8_t polling_interval;
  uint8_t max_packet_len;
  const uint8_t *report_descriptor;
  uint8_t report_descriptor_len;
  uint8_t hid_protocol;
  uint8_t hid_idle;
} interface_t;

typedef struct {
  bool initialized;
  bool started;
  bool enabled;
  bool suspended;
  bool ejected;
  bool landing_page;
  uint32_t generation;
  uint32_t next_request_id;
  uint64_t activation;
  usb_dev_info_t device;
  struct ugsp_personality_builder *personality;
  interface_t interfaces[MAX_INTERFACES];
  endpoint_t endpoints[MAX_ENDPOINT_NUMBER + 1][2];
  usb_state_t task_state[SYSTASK_MAX_TASKS];
} safe3_usb_driver_t;

static safe3_usb_driver_t g_usb = {.next_request_id = 1};
static int g_display_bus_fd = -1;
static int g_display_control_fd = -1;
static int g_button_lines_fd = -1;
static int g_reconnect_button_fd = -1;

static void die_errno(const char *operation) {
  fprintf(stderr, "virtual-trezor-safe3: %s: %s\n", operation,
          strerror(errno));
  exit(1);
}

static void die(const char *message) {
  fprintf(stderr, "virtual-trezor-safe3: %s\n", message);
  exit(1);
}

static uint16_t be16(const uint8_t *bytes) {
  return ((uint16_t)bytes[0] << 8) | bytes[1];
}

static uint32_t be32(const uint8_t *bytes) {
  return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
         ((uint32_t)bytes[2] << 8) | bytes[3];
}

static uint16_t le16(const uint8_t *bytes) {
  return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static void put_be16(uint8_t *bytes, uint16_t value) {
  bytes[0] = (uint8_t)(value >> 8);
  bytes[1] = (uint8_t)value;
}

static void put_be32(uint8_t *bytes, uint32_t value) {
  bytes[0] = (uint8_t)(value >> 24);
  bytes[1] = (uint8_t)(value >> 16);
  bytes[2] = (uint8_t)(value >> 8);
  bytes[3] = (uint8_t)value;
}

static void close_record_fds(struct worker_record *record) {
  for (size_t i = 0; i < record->fd_count; i++) {
    if (record->fds[i] >= 0) {
      close(record->fds[i]);
    }
  }
  record->fd_count = 0;
}

static void receive_record(struct worker_record *record, int flags) {
  static uint8_t packet[PROTOCOL_HEADER_LENGTH + MAX_RECORD_LENGTH + 1];
  union {
    struct cmsghdr alignment;
    uint8_t bytes[CMSG_SPACE(MAX_TRANSFERRED_FDS * sizeof(int))];
  } ancillary;
  memset(record, 0, sizeof(*record));
  memset(&ancillary, 0, sizeof(ancillary));
  for (size_t i = 0; i < MAX_TRANSFERRED_FDS; i++) {
    record->fds[i] = -1;
  }
  struct iovec iov = {.iov_base = packet, .iov_len = sizeof(packet)};
  struct msghdr message = {
      .msg_iov = &iov,
      .msg_iovlen = 1,
      .msg_control = ancillary.bytes,
      .msg_controllen = sizeof(ancillary.bytes),
  };
  ssize_t length = recvmsg(CONTROL_FD, &message, flags | MSG_CMSG_CLOEXEC);
  if (length < 0) {
    die_errno("receive supervisor record");
  }
  if (length == 0) {
    exit(0);
  }
  if ((message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) != 0 ||
      length < (ssize_t)PROTOCOL_HEADER_LENGTH || memcmp(packet, "UGSP", 4) ||
      packet[4] != 1) {
    die("invalid supervisor record header");
  }
  size_t body_length = be32(packet + 16);
  size_t declared_fds = be16(packet + 6);
  if (body_length > MAX_RECORD_LENGTH ||
      (size_t)length != PROTOCOL_HEADER_LENGTH + body_length ||
      declared_fds > MAX_TRANSFERRED_FDS) {
    die("invalid supervisor record length");
  }
  record->kind = packet[5];
  record->generation = be32(packet + 8);
  record->request_id = be32(packet + 12);
  record->body = packet + PROTOCOL_HEADER_LENGTH;
  record->body_length = body_length;
  for (struct cmsghdr *item = CMSG_FIRSTHDR(&message); item != NULL;
       item = CMSG_NXTHDR(&message, item)) {
    if (item->cmsg_level != SOL_SOCKET || item->cmsg_type != SCM_RIGHTS ||
        item->cmsg_len < CMSG_LEN(0)) {
      die("invalid supervisor descriptor transfer");
    }
    size_t count = (item->cmsg_len - CMSG_LEN(0)) / sizeof(int);
    if (record->fd_count + count > MAX_TRANSFERRED_FDS) {
      die("too many supervisor descriptors");
    }
    memcpy(record->fds + record->fd_count, CMSG_DATA(item),
           count * sizeof(int));
    record->fd_count += count;
  }
  if (record->fd_count != declared_fds) {
    close_record_fds(record);
    die("supervisor descriptor count mismatch");
  }
}

static void send_record(uint8_t kind, uint32_t generation,
                        uint32_t request_id, const uint8_t *body,
                        size_t body_length) {
  if (body_length > MAX_RECORD_LENGTH) {
    die("worker record is too large");
  }
  uint8_t *packet = malloc(PROTOCOL_HEADER_LENGTH + body_length);
  if (packet == NULL) {
    die_errno("allocate worker record");
  }
  memcpy(packet, "UGSP", 4);
  packet[4] = 1;
  packet[5] = kind;
  put_be16(packet + 6, 0);
  put_be32(packet + 8, generation);
  put_be32(packet + 12, request_id);
  put_be32(packet + 16, (uint32_t)body_length);
  if (body_length != 0) {
    memcpy(packet + PROTOCOL_HEADER_LENGTH, body, body_length);
  }
  ssize_t length = send(CONTROL_FD, packet,
                        PROTOCOL_HEADER_LENGTH + body_length, MSG_NOSIGNAL);
  free(packet);
  if (length != (ssize_t)(PROTOCOL_HEADER_LENGTH + body_length)) {
    die_errno("send supervisor record");
  }
}

static void assign_resource(const char *name, size_t length, int fd) {
  if (length == strlen("display-spi") &&
      memcmp(name, "display-spi", length) == 0) {
    g_display_bus_fd = fd;
  } else if (length == strlen("display-control") &&
             memcmp(name, "display-control", length) == 0) {
    g_display_control_fd = fd;
  } else if (length == strlen("buttons") &&
             memcmp(name, "buttons", length) == 0) {
    g_button_lines_fd = fd;
  } else if (length == strlen("reconnect-button") &&
             memcmp(name, "reconnect-button", length) == 0) {
    g_reconnect_button_fd = fd;
  } else {
    close(fd);
    die("unknown supervisor resource name");
  }
}

__attribute__((constructor)) static void receive_initial_resources(void) {
  if (geteuid() == 0) {
    die("firmware worker must not run as root");
  }
  const char *state_directory = getenv("STATE_DIRECTORY");
  if (state_directory == NULL || state_directory[0] != '/') {
    die("STATE_DIRECTORY is missing or invalid");
  }
  if (setenv("TREZOR_PROFILE_DIR", state_directory, 1) != 0) {
    die_errno("set Core profile directory");
  }
  if (chdir(state_directory) != 0) {
    die_errno("enter worker state directory");
  }
  struct worker_record record;
  receive_record(&record, 0);
  if (record.kind != KIND_INITIAL_RESOURCES || record.generation != 0 ||
      record.request_id != 0 || record.body_length < 2) {
    close_record_fds(&record);
    die("expected initial supervisor resources");
  }
  size_t count = be16(record.body);
  size_t offset = 2;
  if (count != record.fd_count) {
    close_record_fds(&record);
    die("resource name and descriptor counts differ");
  }
  for (size_t i = 0; i < count; i++) {
    if (offset + 2 > record.body_length) {
      die("truncated supervisor resource name");
    }
    size_t length = be16(record.body + offset);
    offset += 2;
    if (length == 0 || offset + length > record.body_length) {
      die("invalid supervisor resource name");
    }
    assign_resource((const char *)record.body + offset, length, record.fds[i]);
    record.fds[i] = -1;
    offset += length;
  }
  record.fd_count = 0;
  if (offset != record.body_length || g_display_bus_fd < 0 ||
      g_display_control_fd < 0 || g_button_lines_fd < 0 ||
      g_reconnect_button_fd < 0) {
    die("incomplete Safe 3 supervisor resource set");
  }
  int flags = fcntl(g_reconnect_button_fd, F_GETFL);
  if (flags < 0 ||
      fcntl(g_reconnect_button_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    die_errno("make Safe 3 reconnect button nonblocking");
  }
}

int safe3_supervisor_display_bus_fd(void) { return g_display_bus_fd; }
int safe3_supervisor_display_control_fd(void) { return g_display_control_fd; }
int safe3_supervisor_button_lines_fd(void) { return g_button_lines_fd; }

static endpoint_t *endpoint_for(uint8_t address) {
  uint8_t number = address & 0x7f;
  if (number == 0 || number > MAX_ENDPOINT_NUMBER) {
    return NULL;
  }
  return &g_usb.endpoints[number][(address & 0x80) != 0];
}

static interface_t *interface_for_number(uint8_t number) {
  if (number >= MAX_INTERFACES ||
      g_usb.interfaces[number].type == IFACE_DISABLED) {
    return NULL;
  }
  return &g_usb.interfaces[number];
}

static bool generation_ended(void) {
  return errno == ENODEV || errno == ESHUTDOWN || errno == EPIPE ||
         errno == EBADF;
}

static void unlock_endpoint_mutex(void *context) {
  pthread_mutex_unlock(context);
}

static void *out_thread_main(void *context) {
  endpoint_t *endpoint = context;
  for (;;) {
    ssize_t length;
    do {
      length = read(endpoint->fd, endpoint->read_buffer,
                    endpoint->max_packet_size);
    } while (length < 0 && errno == EINTR);
    if (length < 0) {
      if (generation_ended()) {
        break;
      }
      die_errno("read FunctionFS OUT endpoint");
    }
    int result = pthread_mutex_lock(&endpoint->mutex);
    if (result != 0) {
      errno = result;
      die_errno("lock Safe 3 OUT packet");
    }
    pthread_cleanup_push(unlock_endpoint_mutex, &endpoint->mutex);
    while (endpoint->occupied) {
      result = pthread_cond_wait(&endpoint->consumed, &endpoint->mutex);
      if (result != 0) {
        errno = result;
        die_errno("wait for Safe 3 OUT packet consumption");
      }
    }
    if (length != 0) {
      memcpy(endpoint->packet, endpoint->read_buffer, (size_t)length);
    }
    endpoint->packet_length = (uint16_t)length;
    endpoint->occupied = true;
    pthread_cleanup_pop(1);
    uint64_t notification = 1;
    if (write(endpoint->event_fd, &notification, sizeof(notification)) !=
        (ssize_t)sizeof(notification)) {
      die_errno("notify Safe 3 OUT packet");
    }
  }
  return NULL;
}

static void *in_thread_main(void *context) {
  endpoint_t *endpoint = context;
  for (;;) {
    size_t length;
    int result = pthread_mutex_lock(&endpoint->mutex);
    if (result != 0) {
      errno = result;
      die_errno("lock Safe 3 IN packet");
    }
    pthread_cleanup_push(unlock_endpoint_mutex, &endpoint->mutex);
    while (!endpoint->occupied) {
      result = pthread_cond_wait(&endpoint->produced, &endpoint->mutex);
      if (result != 0) {
        errno = result;
        die_errno("wait for Safe 3 IN packet production");
      }
    }
    length = endpoint->packet_length;
    pthread_cleanup_pop(1);

    const void *payload = length == 0 ? (const void *)"" : endpoint->packet;
    ssize_t written;
    do {
      written = write(endpoint->fd, payload, length);
    } while (written < 0 && errno == EINTR);
    if (written < 0) {
      if (generation_ended()) {
        break;
      }
      die_errno("write FunctionFS IN endpoint");
    }
    if ((size_t)written != length) {
      die("short write to FunctionFS IN endpoint");
    }

    result = pthread_mutex_lock(&endpoint->mutex);
    if (result != 0) {
      errno = result;
      die_errno("lock consumed Safe 3 IN packet");
    }
    endpoint->packet_length = 0;
    endpoint->occupied = false;
    pthread_cond_signal(&endpoint->consumed);
    pthread_mutex_unlock(&endpoint->mutex);

    uint64_t notification = 1;
    if (write(endpoint->event_fd, &notification, sizeof(notification)) !=
        (ssize_t)sizeof(notification)) {
      die_errno("notify Safe 3 IN packet consumption");
    }
  }
  return NULL;
}

static void stop_endpoint_threads(void) {
  for (uint8_t number = 1; number <= MAX_ENDPOINT_NUMBER; number++) {
    for (size_t direction = 0; direction < 2; direction++) {
      endpoint_t *endpoint = &g_usb.endpoints[number][direction];
      if (endpoint->thread_running) {
        int result = pthread_cancel(endpoint->thread);
        if (result == 0) {
          result = pthread_join(endpoint->thread, NULL);
        }
        if (result != 0) {
          errno = result;
          die_errno("stop Safe 3 endpoint thread");
        }
        endpoint->thread_running = false;
      }
      if (endpoint->event_fd >= 0) {
        close(endpoint->event_fd);
        endpoint->event_fd = -1;
      }
      free(endpoint->packet);
      free(endpoint->read_buffer);
      endpoint->packet = NULL;
      endpoint->read_buffer = NULL;
      endpoint->packet_length = 0;
      endpoint->occupied = false;
    }
  }
}

static void start_endpoint_threads(void) {
  for (uint8_t number = 1; number <= MAX_ENDPOINT_NUMBER; number++) {
    for (size_t direction = 0; direction < 2; direction++) {
      endpoint_t *endpoint = &g_usb.endpoints[number][direction];
      if (endpoint->fd < 0) {
        continue;
      }
      endpoint->packet = malloc(endpoint->max_packet_size);
      if (direction == 0) {
        endpoint->read_buffer = malloc(endpoint->max_packet_size);
      }
      if (endpoint->packet == NULL ||
          (direction == 0 && endpoint->read_buffer == NULL)) {
        die_errno("allocate Safe 3 endpoint buffers");
      }
      endpoint->event_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
      if (endpoint->event_fd < 0) {
        die_errno("create Safe 3 endpoint notification");
      }
      void *(*thread_main)(void *) =
          direction == 0 ? out_thread_main : in_thread_main;
      int result =
          pthread_create(&endpoint->thread, NULL, thread_main, endpoint);
      if (result != 0) {
        errno = result;
        die_errno("start Safe 3 endpoint thread");
      }
      endpoint->thread_running = true;
    }
  }
}

static void close_endpoints(void) {
  g_usb.enabled = false;
  stop_endpoint_threads();
  for (uint8_t number = 1; number <= MAX_ENDPOINT_NUMBER; number++) {
    for (size_t direction = 0; direction < 2; direction++) {
      endpoint_t *endpoint = &g_usb.endpoints[number][direction];
      if (endpoint->fd >= 0) {
        close(endpoint->fd);
        endpoint->fd = -1;
      }
    }
  }
}

static void install_endpoints(struct worker_record *record) {
  if (record->body_length < 2) {
    die("invalid Safe 3 endpoint map");
  }
  size_t count = be16(record->body);
  if (record->body_length != 2 + count * 4 || record->fd_count != count) {
    close_record_fds(record);
    die("Safe 3 endpoint map and descriptors differ");
  }
  close_endpoints();
  for (size_t i = 0; i < count; i++) {
    const uint8_t *entry = record->body + 2 + i * 4;
    endpoint_t *endpoint = endpoint_for(entry[0]);
    if (endpoint == NULL || endpoint->fd >= 0) {
      close_record_fds(record);
      die("invalid or duplicate Safe 3 endpoint address");
    }
    endpoint->address = entry[0];
    endpoint->transfer_type = entry[1];
    endpoint->max_packet_size = be16(entry + 2);
    endpoint->fd = record->fds[i];
    record->fds[i] = -1;
  }
  record->fd_count = 0;
  g_usb.generation = record->generation;
}

static uint32_t send_configuration(void) {
  uint8_t *bundle = NULL;
  size_t bundle_length = 0;
  if (!ugsp_personality_builder_finish(g_usb.personality, &bundle,
                                       &bundle_length)) {
    die("finish Safe 3 USB personality");
  }
  uint32_t request_id = g_usb.next_request_id++;
  if (request_id == 0) {
    die("Safe 3 USB configuration request ID overflow");
  }
  send_record(KIND_CONFIGURE, g_usb.generation, request_id, bundle,
              bundle_length);
  ugsp_personality_cbor_free(bundle, bundle_length);
  return request_id;
}

static bool configure_until_endpoints(bool replacement) {
  uint32_t request_id = send_configuration();
  bool quiesced = !replacement;
  for (;;) {
    struct worker_record record;
    receive_record(&record, 0);
    if (record.kind == KIND_CONFIGURATION_REJECTED &&
        record.generation == g_usb.generation &&
        record.request_id == request_id && record.fd_count == 0) {
      fprintf(stderr,
              "virtual-trezor-safe3: supervisor rejected USB configuration: "
              "%.*s\n",
              (int)record.body_length, (const char *)record.body);
      return false;
    }
    if (record.kind == KIND_QUIESCE && replacement && !quiesced &&
        record.generation == g_usb.generation &&
        record.request_id == request_id && record.body_length == 0 &&
        record.fd_count == 0) {
      close_endpoints();
      send_record(KIND_QUIESCED, g_usb.generation, request_id, NULL, 0);
      quiesced = true;
    } else if (record.kind == KIND_USB_ENDPOINTS && quiesced &&
               record.request_id == request_id) {
      install_endpoints(&record);
      send_record(KIND_SERVING, g_usb.generation, request_id, NULL, 0);
      return true;
    } else {
      close_record_fds(&record);
      die("unexpected supervisor record during Safe 3 configuration");
    }
  }
}

static void unconfigure_until_quiesced(void) {
  uint32_t request_id = g_usb.next_request_id++;
  if (request_id == 0) {
    die("Safe 3 USB configuration request ID overflow");
  }
  send_record(KIND_CONFIGURE, g_usb.generation, request_id, NULL, 0);
  for (;;) {
    struct worker_record record;
    receive_record(&record, 0);
    if (record.kind == KIND_CONFIGURATION_REJECTED &&
        record.generation == g_usb.generation &&
        record.request_id == request_id && record.fd_count == 0) {
      fprintf(stderr,
              "virtual-trezor-safe3: supervisor rejected USB ejection: %.*s\n",
              (int)record.body_length, (const char *)record.body);
      return;
    }
    if (record.kind != KIND_QUIESCE ||
        record.generation != g_usb.generation ||
        record.request_id != request_id || record.body_length != 0 ||
        record.fd_count != 0) {
      close_record_fds(&record);
      die("unexpected supervisor record during Safe 3 ejection");
    }
    close_endpoints();
    send_record(KIND_QUIESCED, g_usb.generation, request_id, NULL, 0);
    g_usb.ejected = true;
    fprintf(stderr,
            "virtual-trezor-safe3: USB ejected; waiting for KEY3 release\n");
    return;
  }
}

typedef enum {
  RECONNECT_TRANSITION_NONE = -1,
  RECONNECT_TRANSITION_RELEASED = 0,
  RECONNECT_TRANSITION_PRESSED = 1,
} reconnect_transition_t;

static reconnect_transition_t take_reconnect_transition(void) {
  for (;;) {
    struct gpio_v2_line_event event;
    ssize_t length = read(g_reconnect_button_fd, &event, sizeof(event));
    if (length == (ssize_t)sizeof(event)) {
      if (event.id == GPIO_V2_LINE_EVENT_RISING_EDGE) {
        return RECONNECT_TRANSITION_PRESSED;
      }
      if (event.id == GPIO_V2_LINE_EVENT_FALLING_EDGE) {
        return RECONNECT_TRANSITION_RELEASED;
      }
      continue;
    }
    if (length < 0 && errno == EINTR) {
      continue;
    }
    if (length < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return RECONNECT_TRANSITION_NONE;
    }
    if (length == 0) {
      die("Safe 3 reconnect button resource closed");
    }
    if (length < 0) {
      die_errno("read Safe 3 reconnect button");
    }
    die("partial Safe 3 reconnect button event");
  }
}

static void send_control_response(uint32_t request_id, uint8_t disposition,
                                  const uint8_t *data, size_t length) {
  uint8_t *body = malloc(1 + length);
  if (body == NULL) {
    die_errno("allocate Safe 3 control response");
  }
  body[0] = disposition;
  if (length != 0) {
    memcpy(body + 1, data, length);
  }
  send_record(KIND_USB_CONTROL_RESPONSE, g_usb.generation, request_id, body,
              1 + length);
  free(body);
}

static void answer_control_request(const struct worker_record *record) {
  if (record->generation != g_usb.generation || record->request_id == 0 ||
      record->body_length < 8 || record->fd_count != 0) {
    die("invalid Safe 3 USB control request");
  }
  const uint8_t *setup = record->body;
  uint8_t request_type = setup[0];
  uint8_t request = setup[1];
  uint16_t value = le16(setup + 2);
  uint16_t index = le16(setup + 4);
  uint16_t requested = le16(setup + 6);
  bool direction_in = (request_type & 0x80) != 0;
  size_t out_length = record->body_length - 8;
  if ((direction_in && out_length != 0) ||
      (!direction_in && out_length != requested)) {
    die("Safe 3 control request data mismatch");
  }
  const uint8_t *response = NULL;
  size_t response_length = 0;
  uint8_t local[64] = {0};
  bool handled = false;
  bool acknowledge = false;
  interface_t *iface = interface_for_number((uint8_t)index);
  uint8_t type = request_type & 0x60;
  uint8_t recipient = request_type & 0x1f;

  if (type == 0 && direction_in && request == USB_REQ_GET_DESCRIPTOR &&
      (value >> 8) == USB_DT_REPORT && iface != NULL &&
      iface->type == IFACE_HID) {
    response = iface->report_descriptor;
    response_length = iface->report_descriptor_len;
    handled = true;
  } else if (type == 0 && direction_in && request == USB_REQ_GET_DESCRIPTOR &&
             (value >> 8) == USB_DT_STRING && (value & 0xff) == 0xee) {
    static const uint8_t msft[] = {18, 3, 'M', 0, 'S', 0, 'F', 0, 'T', 0,
                                   '1', 0, '0', 0, '0', 0, '!', 0};
    response = msft;
    response_length = sizeof(msft);
    handled = true;
  } else if (type == 0 && direction_in && request == USB_REQ_GET_STATUS) {
    response = local;
    response_length = 2;
    handled = true;
  } else if (type == 0 && direction_in && request == USB_REQ_GET_CONFIGURATION) {
    local[0] = g_usb.enabled ? 1 : 0;
    response = local;
    response_length = 1;
    handled = true;
  } else if (type == 0 && recipient == 1 && direction_in &&
             request == USB_REQ_GET_INTERFACE && iface != NULL) {
    response = local;
    response_length = 1;
    handled = true;
  } else if (type == 0 && !direction_in &&
             (request == USB_REQ_SET_INTERFACE ||
              request == USB_REQ_SET_CONFIGURATION ||
              request == USB_REQ_CLEAR_FEATURE ||
              request == USB_REQ_SET_FEATURE)) {
    handled = acknowledge = true;
  } else if (type == 0x20 && recipient == 1 && iface != NULL &&
             iface->type == IFACE_HID) {
    if (direction_in && request == USB_HID_GET_IDLE) {
      local[0] = iface->hid_idle;
      response = local;
      response_length = 1;
      handled = true;
    } else if (direction_in && request == USB_HID_GET_PROTOCOL) {
      local[0] = iface->hid_protocol;
      response = local;
      response_length = 1;
      handled = true;
    } else if (!direction_in && request == USB_HID_SET_IDLE) {
      iface->hid_idle = (uint8_t)(value >> 8);
      handled = acknowledge = true;
    } else if (!direction_in && request == USB_HID_SET_PROTOCOL) {
      iface->hid_protocol = (uint8_t)value;
      handled = acknowledge = true;
    } else if (!direction_in && request == USB_HID_SET_REPORT) {
      handled = acknowledge = true;
    } else if (direction_in && request == USB_HID_GET_REPORT) {
      response = local;
      response_length = 0;
      handled = true;
    }
  } else if (type == 0x40 && recipient == 0 && direction_in &&
             request == 0x01 && index == 0x0002 && value == 0x0001) {
    static const uint8_t url[] = {18, 3, 1, 't', 'r', 'e', 'z', 'o', 'r', '.',
                                  'i', 'o', '/', 's', 't', 'a', 'r', 't'};
    response = url;
    response_length = sizeof(url);
    handled = true;
  }

  if (!handled) {
    send_control_response(record->request_id, RESPONSE_STALL, NULL, 0);
  } else if (acknowledge) {
    send_control_response(record->request_id, RESPONSE_ACK, NULL, 0);
  } else {
    if (response_length > requested) {
      response_length = requested;
    }
    send_control_response(record->request_id, RESPONSE_DATA, response,
                          response_length);
  }
}

static void handle_bus_event(const struct worker_record *record) {
  if (record->generation != g_usb.generation || record->request_id != 0 ||
      record->body_length != UGSP_USB_BUS_EVENT_BODY_LENGTH ||
      record->fd_count != 0) {
    die("invalid Safe 3 USB bus event");
  }
  uint64_t activation = ugsp_read_be64(record->body + 1);
  if ((record->body[0] == 5 || record->body[0] == 6) &&
      activation != g_usb.activation) {
    die("Safe 3 suspend/resume changed endpoint activation");
  }
  g_usb.activation = activation;
  switch (record->body[0]) {
  case 0: /* bind */
  case 1: /* unbind */
  case 3: /* disable */
    g_usb.enabled = false;
    g_usb.suspended = false;
    stop_endpoint_threads();
    break;
  case 2: /* enable */
    if (!g_usb.enabled) {
      start_endpoint_threads();
      g_usb.enabled = true;
    }
    g_usb.suspended = false;
    break;
  case 5: /* suspend */
    g_usb.suspended = true;
    break;
  case 6: /* resume */
    g_usb.suspended = false;
    break;
  default:
    die("unknown Safe 3 USB bus event");
  }
}

static void handle_runtime_record(struct worker_record *record) {
  if (record->kind == KIND_USB_BUS_EVENT) {
    handle_bus_event(record);
  } else if (record->kind == KIND_USB_CONTROL_REQUEST) {
    answer_control_request(record);
  } else if (record->kind == KIND_QUIESCE &&
             record->generation == g_usb.generation &&
             record->body_length == 0 && record->fd_count == 0) {
    bool shutdown = record->request_id == 0;
    close_endpoints();
    send_record(KIND_QUIESCED, g_usb.generation, record->request_id, NULL, 0);
    if (shutdown) {
      exit(0);
    }
  } else {
    close_record_fds(record);
    die("unexpected Safe 3 supervisor runtime record");
  }
}

static void service_controller(void) {
  struct pollfd fds[2 + MAX_ENDPOINT_NUMBER * 2];
  endpoint_t *ready_endpoints[MAX_ENDPOINT_NUMBER * 2];
  size_t count = 0;
  fds[count++] = (struct pollfd){.fd = CONTROL_FD,
                                 .events = POLLIN | POLLHUP | POLLERR};
  size_t reconnect_index = count;
  fds[count++] = (struct pollfd){.fd = g_reconnect_button_fd,
                                 .events = POLLIN | POLLHUP | POLLERR};
  size_t endpoint_start = count;
  size_t ready_count = 0;
  if (g_usb.enabled) {
    for (uint8_t number = 1; number <= MAX_ENDPOINT_NUMBER; number++) {
      for (size_t direction = 0; direction < 2; direction++) {
        endpoint_t *endpoint = &g_usb.endpoints[number][direction];
        if (endpoint->event_fd >= 0) {
          ready_endpoints[ready_count++] = endpoint;
          fds[count++] = (struct pollfd){.fd = endpoint->event_fd,
                                         .events = POLLIN};
        }
      }
    }
  }
  int result = poll(fds, count, 0);
  if (result < 0) {
    if (errno == EINTR) {
      return;
    }
    die_errno("poll Safe 3 virtual USB controller");
  }
  if (fds[0].revents != 0) {
    struct worker_record record;
    receive_record(&record, MSG_DONTWAIT);
    handle_runtime_record(&record);
  }
  if ((fds[reconnect_index].revents & POLLIN) != 0) {
    reconnect_transition_t transition = take_reconnect_transition();
    if (transition == RECONNECT_TRANSITION_PRESSED && g_usb.started &&
        !g_usb.ejected) {
      fprintf(stderr, "virtual-trezor-safe3: KEY3 USB eject requested\n");
      unconfigure_until_quiesced();
      return;
    }
    if (transition == RECONNECT_TRANSITION_RELEASED && g_usb.started &&
        g_usb.ejected) {
      fprintf(stderr, "virtual-trezor-safe3: KEY3 USB insertion requested\n");
      if (!configure_until_endpoints(false)) {
        die("supervisor rejected Safe 3 USB insertion");
      }
      g_usb.ejected = false;
      return;
    }
  }
  if ((fds[reconnect_index].revents & ~(POLLIN)) != 0) {
    die("Safe 3 reconnect button reported an unexpected poll event");
  }
  for (size_t i = 0; i < ready_count; i++) {
    if ((fds[endpoint_start + i].revents & POLLIN) != 0) {
      uint64_t notifications;
      if (read(ready_endpoints[i]->event_fd, &notifications,
               sizeof(notifications)) != (ssize_t)sizeof(notifications)) {
        die_errno("read Safe 3 endpoint notification");
      }
      interface_t *iface =
          interface_for_number((uint8_t)((ready_endpoints[i]->address & 0x7f) - 1));
      if (iface != NULL) {
        if ((ready_endpoints[i]->address & 0x80) != 0) {
          syshandle_signal_write_ready(iface->handle, NULL);
        } else {
          syshandle_signal_read_ready(iface->handle, NULL);
        }
      }
    }
  }
}

static void on_task_created(void *context, systask_id_t task_id) {
  (void)context;
  g_usb.task_state[task_id] = (usb_state_t){0};
}

static bool usb_state_changed(void *context, systask_id_t task_id,
                              void *parameter) {
  (void)context;
  usb_state_t *state = parameter;
  return state != NULL &&
         state->configured != g_usb.task_state[task_id].configured;
}

static bool iface_read_ready(void *context, systask_id_t task_id,
                             void *parameter) {
  (void)task_id;
  (void)parameter;
  interface_t *iface = context;
  endpoint_t *endpoint = endpoint_for((uint8_t)(1 + iface->number));
  if (!g_usb.enabled || g_usb.suspended || endpoint == NULL) {
    return false;
  }
  pthread_mutex_lock(&endpoint->mutex);
  bool ready = endpoint->occupied;
  pthread_mutex_unlock(&endpoint->mutex);
  return ready;
}

static bool iface_write_ready(void *context, systask_id_t task_id,
                              void *parameter) {
  (void)task_id;
  (void)parameter;
  interface_t *iface = context;
  endpoint_t *endpoint = endpoint_for((uint8_t)(0x80 | (1 + iface->number)));
  if (!g_usb.enabled || g_usb.suspended || endpoint == NULL) {
    return false;
  }
  pthread_mutex_lock(&endpoint->mutex);
  bool ready = !endpoint->occupied;
  pthread_mutex_unlock(&endpoint->mutex);
  return ready;
}

static void usb_event_poll(void *context, bool read_awaited,
                           bool write_awaited) {
  (void)context;
  (void)write_awaited;
  service_controller();
  if (read_awaited) {
    usb_state_t state;
    usb_get_state(&state);
    syshandle_signal_read_ready(SYSHANDLE_USB, &state);
  }
}

static void iface_event_poll(void *context, bool read_awaited,
                             bool write_awaited) {
  interface_t *iface = context;
  service_controller();
  if (read_awaited && iface_read_ready(iface, 0, NULL)) {
    syshandle_signal_read_ready(iface->handle, NULL);
  }
  if (write_awaited && iface_write_ready(iface, 0, NULL)) {
    syshandle_signal_write_ready(iface->handle, NULL);
  }
}

static ssize_t iface_read(void *context, void *buffer, size_t length) {
  interface_t *iface = context;
  endpoint_t *endpoint = endpoint_for((uint8_t)(1 + iface->number));
  if (endpoint == NULL) {
    return -1;
  }
  pthread_mutex_lock(&endpoint->mutex);
  if (!endpoint->occupied) {
    pthread_mutex_unlock(&endpoint->mutex);
    return 0;
  }
  size_t copied = endpoint->packet_length < length ? endpoint->packet_length
                                                   : length;
  if (copied != 0) {
    memcpy(buffer, endpoint->packet, copied);
  }
  endpoint->occupied = false;
  endpoint->packet_length = 0;
  pthread_cond_signal(&endpoint->consumed);
  pthread_mutex_unlock(&endpoint->mutex);
  return (ssize_t)copied;
}

static ssize_t iface_write(void *context, const void *data, size_t length) {
  interface_t *iface = context;
  endpoint_t *endpoint = endpoint_for((uint8_t)(0x80 | (1 + iface->number)));
  if (!g_usb.enabled || g_usb.suspended || endpoint == NULL || endpoint->fd < 0 ||
      length > endpoint->max_packet_size) {
    return 0;
  }
  pthread_mutex_lock(&endpoint->mutex);
  if (endpoint->occupied) {
    pthread_mutex_unlock(&endpoint->mutex);
    return 0;
  }
  if (length != 0) {
    memcpy(endpoint->packet, data, length);
  }
  endpoint->packet_length = (uint16_t)length;
  endpoint->occupied = true;
  pthread_cond_signal(&endpoint->produced);
  pthread_mutex_unlock(&endpoint->mutex);
  return (ssize_t)length;
}

static const syshandle_vmt_t usb_handle_vmt = {
    .task_created = on_task_created,
    .task_killed = NULL,
    .poll = usb_event_poll,
    .check_read_ready = usb_state_changed,
    .check_write_ready = NULL,
    .read = NULL,
    .write = NULL,
};

static const syshandle_vmt_t iface_handle_vmt = {
    .task_created = NULL,
    .task_killed = NULL,
    .poll = iface_event_poll,
    .check_read_ready = iface_read_ready,
    .check_write_ready = iface_write_ready,
    .read = iface_read,
    .write = iface_write,
};

secbool usb_init(const usb_dev_info_t *device) {
  if (g_usb.initialized) {
    return sectrue;
  }
  g_usb.device = *device;
  struct ugsp_usb_device personality_device = {
      .usb_version = device->usb21_enabled == sectrue ? 0x0210 : 0x0200,
      .vendor_id = device->vendor_id,
      .product_id = device->product_id,
      .device_version = device->release_num,
      .device_class = device->device_class,
      .device_subclass = device->device_subclass,
      .device_protocol = device->device_protocol,
      .max_packet_size_0 = 64,
      .manufacturer = device->manufacturer,
      .product = device->product,
      .serial_number = device->serial_number,
      .interface_name = device->interface,
  };
  g_usb.personality = ugsp_personality_builder_new(
      UGSP_USB_SPEED_FULL, &personality_device);
  if (g_usb.personality == NULL) {
    return secfalse;
  }
  for (uint8_t number = 1; number <= MAX_ENDPOINT_NUMBER; number++) {
    for (size_t direction = 0; direction < 2; direction++) {
      endpoint_t *endpoint = &g_usb.endpoints[number][direction];
      endpoint->fd = -1;
      endpoint->event_fd = -1;
      if (!endpoint->synchronization_initialized) {
        if (pthread_mutex_init(&endpoint->mutex, NULL) != 0 ||
            pthread_cond_init(&endpoint->produced, NULL) != 0 ||
            pthread_cond_init(&endpoint->consumed, NULL) != 0) {
          die("initialize Safe 3 endpoint synchronization");
        }
        endpoint->synchronization_initialized = true;
      }
    }
  }
  if (!syshandle_register(SYSHANDLE_USB, &usb_handle_vmt, &g_usb)) {
    ugsp_personality_builder_free(g_usb.personality);
    g_usb.personality = NULL;
    return secfalse;
  }
  g_usb.initialized = true;
  uint32_t request_id = g_usb.next_request_id++;
  if (request_id == 0) {
    die("Safe 3 USB configuration request ID overflow");
  }
  send_record(KIND_CONFIGURE, g_usb.generation, request_id, NULL, 0);
  fprintf(stderr,
          "virtual-trezor-safe3: Core USB initialized without attachment; "
          "waiting for firmware start\n");
  return sectrue;
}

void usb_deinit(void) {
  if (!g_usb.initialized) {
    return;
  }
  usb_stop();
  syshandle_unregister(SYSHANDLE_USB);
  ugsp_personality_builder_free(g_usb.personality);
  g_usb.personality = NULL;
  g_usb.initialized = false;
}

secbool usb_webusb_add(const usb_webusb_info_t *info) {
  if (info->iface_num >= MAX_INTERFACES ||
      g_usb.interfaces[info->iface_num].type != IFACE_DISABLED ||
      info->rx_buffer == NULL) {
    return secfalse;
  }
  interface_t *iface = &g_usb.interfaces[info->iface_num];
  uint8_t endpoint_number = (uint8_t)(1 + info->iface_num);
  struct ugsp_usb_interface personality_interface = {
      .number = info->iface_num,
      .class_code = 0xff,
      .subclass = info->subclass,
      .protocol = info->protocol,
      .string_index = 4,
      .endpoint_in = (uint8_t)(0x80 | endpoint_number),
      .endpoint_out = endpoint_number,
      .transfer_type = 3,
      .max_packet_size = info->max_packet_len,
      .interval = info->polling_interval,
      .class_descriptors = {.data = NULL, .length = 0},
  };
  if (!ugsp_personality_builder_add_interface(g_usb.personality,
                                              &personality_interface) ||
      (info->iface_num == 0 &&
       !ugsp_personality_builder_add_microsoft_compatible_id(
           g_usb.personality, 0x21, info->iface_num, "WINUSB", ""))) {
    return secfalse;
  }
  iface->type = IFACE_WEBUSB;
  iface->handle = info->handle;
  iface->number = info->iface_num;
  iface->subclass = info->subclass;
  iface->protocol = info->protocol;
  iface->polling_interval = info->polling_interval;
  iface->max_packet_len = info->max_packet_len;
  return sectrue;
}

secbool usb_hid_add(const usb_hid_info_t *info) {
  if (info->iface_num >= MAX_INTERFACES ||
      g_usb.interfaces[info->iface_num].type != IFACE_DISABLED ||
      info->rx_buffer == NULL || info->report_desc == NULL) {
    return secfalse;
  }
  interface_t *iface = &g_usb.interfaces[info->iface_num];
  uint8_t endpoint_number = (uint8_t)(1 + info->iface_num);
  uint8_t hid_descriptor[9] = {9, USB_DT_HID, 0x11, 0x01, 0, 1,
                               USB_DT_REPORT, info->report_desc_len, 0};
  struct ugsp_usb_interface personality_interface = {
      .number = info->iface_num,
      .class_code = 0x03,
      .subclass = info->subclass,
      .protocol = info->protocol,
      .string_index = 4,
      .endpoint_in = (uint8_t)(0x80 | endpoint_number),
      .endpoint_out = endpoint_number,
      .transfer_type = 3,
      .max_packet_size = info->max_packet_len,
      .interval = info->polling_interval,
      .class_descriptors = {
          .data = hid_descriptor,
          .length = sizeof(hid_descriptor),
      },
  };
  if (!ugsp_personality_builder_add_interface(g_usb.personality,
                                              &personality_interface)) {
    return secfalse;
  }
  iface->type = IFACE_HID;
  iface->handle = info->handle;
  iface->number = info->iface_num;
  iface->subclass = info->subclass;
  iface->protocol = info->protocol;
  iface->polling_interval = info->polling_interval;
  iface->max_packet_len = info->max_packet_len;
  iface->report_descriptor = info->report_desc;
  iface->report_descriptor_len = info->report_desc_len;
  return sectrue;
}

secbool usb_vcp_add(const usb_vcp_info_t *info) {
  (void)info;
  return secfalse;
}

secbool usb_start(const usb_start_params_t *params) {
  if (!g_usb.initialized) {
    return secfalse;
  }
  if (g_usb.started) {
    if (params == NULL ||
        (g_usb.landing_page == (params->usb21_landing == sectrue) &&
         strncmp(g_usb.device.serial_number, params->serial_number,
                 USB_MAX_STR_SIZE) == 0)) {
      return sectrue;
    }
    usb_stop();
  }
  if (params != NULL) {
    g_usb.landing_page = params->usb21_landing == sectrue;
    strncpy(g_usb.device.serial_number, params->serial_number,
            USB_MAX_STR_SIZE);
    g_usb.device.serial_number[USB_MAX_STR_SIZE] = '\0';
  }
  if (!ugsp_personality_builder_set_serial_number(
          g_usb.personality, g_usb.device.serial_number) ||
      !ugsp_personality_builder_set_webusb(
          g_usb.personality,
          g_usb.device.usb21_enabled == sectrue ? 1 : 0, 0x0100, 0x01,
          g_usb.landing_page ? "https://trezor.io/start" : "")) {
    return secfalse;
  }
  bool replacement = g_usb.generation != 0;
  if (!configure_until_endpoints(replacement)) {
    return secfalse;
  }
  for (uint8_t i = 0; i < MAX_INTERFACES; i++) {
    interface_t *iface = &g_usb.interfaces[i];
    if (iface->type != IFACE_DISABLED &&
        !syshandle_register(iface->handle, &iface_handle_vmt, iface)) {
      return secfalse;
    }
  }
  g_usb.started = true;
  fputs("virtual-trezor-safe3: Core USB is serving through supervisor "
        "endpoints\n",
        stderr);
  return sectrue;
}

void usb_stop(void) {
  if (!g_usb.started) {
    return;
  }
  for (uint8_t i = 0; i < MAX_INTERFACES; i++) {
    if (g_usb.interfaces[i].type != IFACE_DISABLED) {
      syshandle_unregister(g_usb.interfaces[i].handle);
    }
  }
  g_usb.enabled = false;
  stop_endpoint_threads();
  g_usb.started = false;
}

usb_event_t usb_get_event(void) {
  if (!g_usb.initialized) {
    return USB_EVENT_NONE;
  }
  systask_id_t task = systask_id(systask_active());
  bool configured = g_usb.enabled;
  if (configured != g_usb.task_state[task].configured) {
    g_usb.task_state[task].configured = configured;
    return configured ? USB_EVENT_CONFIGURED : USB_EVENT_DECONFIGURED;
  }
  return USB_EVENT_NONE;
}

void usb_get_state(usb_state_t *state) {
  *state = (usb_state_t){.configured = g_usb.enabled};
}
