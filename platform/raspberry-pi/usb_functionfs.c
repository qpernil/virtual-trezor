// SPDX-License-Identifier: GPL-3.0-or-later

/* Linux virtual USB controller for the genuine legacy firmware USB stack. */

#define _GNU_SOURCE

#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <libopencm3/usb/usbd.h>

#include <usb_personality_ffi.h>

#include "buttons_gpio.h"
#include "display_linux.h"
#include "memory.h"
#include "poll_timeout.h"
#include "usb.h"
#include "usb_functionfs.h"
#include "usb_private.h"

#define CONTROL_FD 3
#define PROTOCOL_HEADER_LENGTH 20
#define MAX_RECORD_LENGTH (1024U * 1024U)
#define MAX_TRANSFERRED_FDS 32
#define MAX_ENDPOINT_NUMBER 7
#define CONTROL_BUFFER_LENGTH 65535

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

struct worker_record {
  uint8_t kind;
  uint32_t generation;
  uint32_t request_id;
  uint8_t *body;
  size_t body_length;
  int fds[MAX_TRANSFERRED_FDS];
  size_t fd_count;
};

struct virtual_endpoint {
  int fd;
  uint8_t address;
  uint8_t type;
  uint16_t max_packet_size;
  bool stalled;
};

static int control_fd = CONTROL_FD;
static int display_resource_fd = -1;
static int display_control_fd = -1;
static int button_lines_fd = -1;
static uint32_t generation = 0;
static uint32_t next_configuration_request = 1;
static bool usb_enabled = false;
static bool controller_already_serviced = false;
static struct virtual_endpoint endpoints[MAX_ENDPOINT_NUMBER + 1][2];
static usbd_device virtual_device;

static bool control_active = false;
static bool control_stalled = false;
static const uint8_t *control_out_data = NULL;
static size_t control_out_length = 0;
static size_t control_out_offset = 0;
static uint8_t control_in_data[CONTROL_BUFFER_LENGTH];
static size_t control_in_length = 0;

void firmwareUsbInit(void);
void firmwareUsbPoll(void);
void firmwareUsbReconnect(void);
char firmwareUsbTiny(char set);

static void die_errno(const char *operation) {
  fprintf(stderr, "virtual-trezor: %s: %s\n", operation, strerror(errno));
  exit(1);
}

static void die_message(const char *message) {
  fprintf(stderr, "virtual-trezor: %s\n", message);
  exit(1);
}

__attribute__((constructor)) static void worker_process_setup(void) {
  if (geteuid() == 0) {
    die_message("firmware worker must not run as root");
  }
  const char *state_directory = getenv("STATE_DIRECTORY");
  if (state_directory == NULL || state_directory[0] != '/') {
    die_message("STATE_DIRECTORY is missing or invalid");
  }
  if (chdir(state_directory) != 0) {
    die_errno("enter worker state directory");
  }
}

static uint16_t be16(const uint8_t *bytes) {
  return ((uint16_t)bytes[0] << 8) | bytes[1];
}

static uint32_t be32(const uint8_t *bytes) {
  return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
         ((uint32_t)bytes[2] << 8) | bytes[3];
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
    close(record->fds[i]);
  }
  record->fd_count = 0;
}

static void receive_record(struct worker_record *record, int flags) {
  static uint8_t packet[PROTOCOL_HEADER_LENGTH + MAX_RECORD_LENGTH + 1];
  union {
    struct cmsghdr alignment;
    uint8_t bytes[CMSG_SPACE(MAX_TRANSFERRED_FDS * sizeof(int))];
  } control;
  memset(record, 0, sizeof(*record));
  memset(&control, 0, sizeof(control));
  struct iovec iov = {.iov_base = packet, .iov_len = sizeof(packet)};
  struct msghdr message = {
      .msg_iov = &iov,
      .msg_iovlen = 1,
      .msg_control = control.bytes,
      .msg_controllen = sizeof(control.bytes),
  };
  ssize_t length = recvmsg(control_fd, &message, flags | MSG_CMSG_CLOEXEC);
  if (length < 0) {
    die_errno("receive supervisor record");
  }
  if (length == 0) {
    exit(0);
  }
  if ((message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) != 0 ||
      length < PROTOCOL_HEADER_LENGTH || memcmp(packet, "UGSP", 4) != 0 ||
      packet[4] != 1) {
    die_message("invalid supervisor record header");
  }
  size_t body_length = be32(packet + 16);
  size_t declared_fds = be16(packet + 6);
  if (body_length > MAX_RECORD_LENGTH ||
      (size_t)length != PROTOCOL_HEADER_LENGTH + body_length ||
      declared_fds > MAX_TRANSFERRED_FDS) {
    die_message("invalid supervisor record length");
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
      die_message("invalid supervisor descriptor transfer");
    }
    size_t count = (item->cmsg_len - CMSG_LEN(0)) / sizeof(int);
    if (record->fd_count + count > MAX_TRANSFERRED_FDS) {
      die_message("too many supervisor descriptors");
    }
    memcpy(record->fds + record->fd_count, CMSG_DATA(item),
           count * sizeof(int));
    record->fd_count += count;
  }
  if (record->fd_count != declared_fds) {
    close_record_fds(record);
    die_message("supervisor descriptor count mismatch");
  }
}

static void send_record(uint8_t kind, uint32_t record_generation,
                        uint32_t request_id, const uint8_t *body,
                        size_t body_length) {
  if (body_length > MAX_RECORD_LENGTH) {
    die_message("worker record is too large");
  }
  uint8_t *packet = malloc(PROTOCOL_HEADER_LENGTH + body_length);
  if (packet == NULL) {
    die_errno("allocate worker record");
  }
  memcpy(packet, "UGSP", 4);
  packet[4] = 1;
  packet[5] = kind;
  put_be16(packet + 6, 0);
  put_be32(packet + 8, record_generation);
  put_be32(packet + 12, request_id);
  put_be32(packet + 16, (uint32_t)body_length);
  if (body_length != 0) {
    memcpy(packet + PROTOCOL_HEADER_LENGTH, body, body_length);
  }
  ssize_t length = send(control_fd, packet, PROTOCOL_HEADER_LENGTH + body_length,
                        MSG_NOSIGNAL);
  free(packet);
  if (length != (ssize_t)(PROTOCOL_HEADER_LENGTH + body_length)) {
    die_errno("send worker record");
  }
}

static void assign_resource(const char *name, size_t name_length, int fd) {
  if ((name_length == strlen("display-spi") &&
       memcmp(name, "display-spi", name_length) == 0) ||
      (name_length == strlen("display-i2c") &&
       memcmp(name, "display-i2c", name_length) == 0)) {
    display_resource_fd = fd;
  } else if (name_length == strlen("display-control") &&
             memcmp(name, "display-control", name_length) == 0) {
    display_control_fd = fd;
  } else if (name_length == strlen("buttons") &&
             memcmp(name, "buttons", name_length) == 0) {
    button_lines_fd = fd;
  } else {
    close(fd);
    die_message("unknown supervisor resource name");
  }
}

void workerReceiveSupervisorResources(void) {
  struct worker_record record;
  receive_record(&record, 0);
  if (record.kind != KIND_INITIAL_RESOURCES || record.generation != 0 ||
      record.request_id != 0 || record.body_length < 2) {
    close_record_fds(&record);
    die_message("expected initial supervisor resources");
  }
  size_t count = be16(record.body);
  size_t offset = 2;
  if (count != record.fd_count) {
    close_record_fds(&record);
    die_message("resource name and descriptor counts differ");
  }
  for (size_t i = 0; i < count; i++) {
    if (offset + 2 > record.body_length) {
      close_record_fds(&record);
      die_message("truncated resource name");
    }
    size_t name_length = be16(record.body + offset);
    offset += 2;
    if (name_length == 0 || offset + name_length > record.body_length) {
      close_record_fds(&record);
      die_message("invalid resource name");
    }
    assign_resource((const char *)record.body + offset, name_length,
                    record.fds[i]);
    record.fds[i] = -1;
    offset += name_length;
  }
  record.fd_count = 0;
  if (offset != record.body_length || display_resource_fd < 0 ||
      display_control_fd < 0 || button_lines_fd < 0) {
    die_message("incomplete supervisor resource set");
  }
}

int workerDisplayResourceFd(void) { return display_resource_fd; }
int workerDisplayControlFd(void) { return display_control_fd; }
int workerButtonLinesFd(void) { return button_lines_fd; }

static struct virtual_endpoint *endpoint_for(uint8_t address) {
  uint8_t number = address & 0x7f;
  if (number > MAX_ENDPOINT_NUMBER) {
    return NULL;
  }
  return &endpoints[number][(address & 0x80) != 0];
}

static usbd_device *virtual_init(void) {
  memset(&virtual_device, 0, sizeof(virtual_device));
  for (size_t number = 0; number <= MAX_ENDPOINT_NUMBER; number++) {
    endpoints[number][0].fd = -1;
    endpoints[number][1].fd = -1;
  }
  return &virtual_device;
}

static void virtual_set_address(usbd_device *device, uint8_t address) {
  (void)device;
  (void)address;
}

static void virtual_ep_setup(usbd_device *device, uint8_t address, uint8_t type,
                             uint16_t max_size,
                             usbd_endpoint_callback callback) {
  uint8_t number = address & 0x7f;
  if (number > MAX_ENDPOINT_NUMBER) {
    die_message("firmware configured unsupported endpoint number");
  }
  if (number == 0) {
    /* libopencm3 owns EP0's SETUP/OUT/IN state-machine callbacks. Real USB
     * drivers configure the physical control endpoint here without replacing
     * those callbacks, including when _usbd_reset() passes NULL. */
    return;
  }
  struct virtual_endpoint *endpoint = endpoint_for(address);
  endpoint->address = address;
  endpoint->type = type;
  endpoint->max_packet_size = max_size;
  if (callback != NULL) {
    device->user_callback_ctr[number][(address & 0x80) != 0
                                          ? USB_TRANSACTION_IN
                                          : USB_TRANSACTION_OUT] = callback;
  }
}

static void virtual_ep_reset(usbd_device *device) {
  (void)device;
  for (size_t number = 0; number <= MAX_ENDPOINT_NUMBER; number++) {
    endpoints[number][0].stalled = false;
    endpoints[number][1].stalled = false;
  }
}

static void virtual_ep_stall_set(usbd_device *device, uint8_t address,
                                 uint8_t stalled) {
  (void)device;
  if ((address & 0x7f) == 0 && control_active) {
    control_stalled = stalled != 0;
    return;
  }
  struct virtual_endpoint *endpoint = endpoint_for(address);
  if (endpoint != NULL) {
    endpoint->stalled = stalled != 0;
  }
}

static uint8_t virtual_ep_stall_get(usbd_device *device, uint8_t address) {
  (void)device;
  if ((address & 0x7f) == 0 && control_active) {
    return control_stalled;
  }
  struct virtual_endpoint *endpoint = endpoint_for(address);
  return endpoint == NULL ? 1 : endpoint->stalled;
}

static void virtual_ep_nak_set(usbd_device *device, uint8_t address,
                               uint8_t nak) {
  (void)device;
  (void)address;
  (void)nak;
}

static uint16_t virtual_ep_write_packet(usbd_device *device, uint8_t address,
                                        const void *data, uint16_t length) {
  (void)device;
  if ((address & 0x7f) == 0 && control_active) {
    if (control_in_length + length > sizeof(control_in_data)) {
      control_stalled = true;
      return 0;
    }
    if (length != 0) {
      memcpy(control_in_data + control_in_length, data, length);
    }
    control_in_length += length;
    return length;
  }
  struct virtual_endpoint *endpoint = endpoint_for(address | 0x80);
  if (!usb_enabled || endpoint == NULL || endpoint->fd < 0 ||
      endpoint->stalled) {
    return length;
  }
  uint8_t encoded_length[2];
  put_be16(encoded_length, length);
  struct iovec vectors[2] = {
      {.iov_base = encoded_length, .iov_len = sizeof(encoded_length)},
      {.iov_base = (void *)data, .iov_len = length},
  };
  struct msghdr message = {.msg_iov = vectors, .msg_iovlen = 2};
  ssize_t queued = sendmsg(endpoint->fd, &message, MSG_DONTWAIT | MSG_NOSIGNAL);
  if (queued == (ssize_t)(sizeof(encoded_length) + length)) {
    return length;
  }
  if (queued < 0 && (errno == EINTR || errno == EAGAIN ||
                     errno == EWOULDBLOCK)) {
    return 0;
  }
  if (queued < 0 && (errno == ENODEV || errno == EPIPE ||
                     errno == ESHUTDOWN)) {
    usb_enabled = false;
    return length;
  }
  die_message("short or failed virtual endpoint IN transfer");
  return 0;
}

static uint16_t virtual_ep_read_packet(usbd_device *device, uint8_t address,
                                       void *data, uint16_t length) {
  (void)device;
  if ((address & 0x7f) == 0 && control_active) {
    size_t available = control_out_length - control_out_offset;
    size_t copied = available < length ? available : length;
    if (copied != 0 && data != NULL) {
      memcpy(data, control_out_data + control_out_offset, copied);
    }
    control_out_offset += copied;
    return (uint16_t)copied;
  }
  struct virtual_endpoint *endpoint = endpoint_for(address & 0x7f);
  if (!usb_enabled || endpoint == NULL || endpoint->fd < 0 ||
      endpoint->stalled) {
    return 0;
  }
  uint8_t encoded_length[2];
  struct iovec vectors[2] = {
      {.iov_base = encoded_length, .iov_len = sizeof(encoded_length)},
      {.iov_base = data, .iov_len = length},
  };
  struct msghdr message = {.msg_iov = vectors, .msg_iovlen = 2};
  ssize_t received = recvmsg(endpoint->fd, &message, MSG_DONTWAIT | MSG_TRUNC);
  if (received >= 0) {
    if (received < (ssize_t)sizeof(encoded_length) ||
        (size_t)received != sizeof(encoded_length) + be16(encoded_length) ||
        be16(encoded_length) > length) {
      die_message("invalid virtual endpoint OUT packet");
    }
    return be16(encoded_length);
  }
  if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK ||
      errno == ENODEV || errno == EPIPE || errno == ESHUTDOWN) {
    return 0;
  }
  die_errno("receive virtual endpoint OUT packet");
  return 0;
}

static void service_virtual_usb(int timeout_ms);

static void virtual_poll(usbd_device *device) {
  (void)device;
  if (!controller_already_serviced) {
    service_virtual_usb(0);
  }
}

static void virtual_disconnect(usbd_device *device, bool disconnected) {
  (void)device;
  if (disconnected) {
    usb_enabled = false;
  }
}

const usbd_driver stm32f107_usb_driver = {
    .init = virtual_init,
    .set_address = virtual_set_address,
    .ep_setup = virtual_ep_setup,
    .ep_reset = virtual_ep_reset,
    .ep_stall_set = virtual_ep_stall_set,
    .ep_nak_set = virtual_ep_nak_set,
    .ep_stall_get = virtual_ep_stall_get,
    .ep_write_packet = virtual_ep_write_packet,
    .ep_read_packet = virtual_ep_read_packet,
    .poll = virtual_poll,
    .disconnect = virtual_disconnect,
    .base_address = 0,
    .set_address_before_status = false,
    .rx_fifo_size = 0,
};

static void run_control_transfer(const uint8_t setup_bytes[8],
                                 const uint8_t *out_data, size_t out_length,
                                 uint8_t **response, size_t *response_length,
                                 bool *stalled) {
  struct usb_setup_data setup;
  memcpy(&setup, setup_bytes, sizeof(setup));
  if (((setup.bmRequestType & 0x80) == 0 && out_length != setup.wLength) ||
      ((setup.bmRequestType & 0x80) != 0 && out_length != 0)) {
    *stalled = true;
    *response = NULL;
    *response_length = 0;
    return;
  }
  control_active = true;
  control_stalled = false;
  control_out_data = out_data;
  control_out_length = out_length;
  control_out_offset = 0;
  control_in_length = 0;
  virtual_device.control_state.req = setup;
  virtual_device.user_callback_ctr[0][USB_TRANSACTION_SETUP](&virtual_device,
                                                              0);
  if (!control_stalled && (setup.bmRequestType & 0x80) == 0 &&
      setup.wLength != 0) {
    while (!control_stalled &&
           (virtual_device.control_state.state == DATA_OUT ||
            virtual_device.control_state.state == LAST_DATA_OUT)) {
      virtual_device.user_callback_ctr[0][USB_TRANSACTION_OUT](&virtual_device,
                                                                0);
    }
  }
  if (!control_stalled && (setup.bmRequestType & 0x80) != 0) {
    while (virtual_device.control_state.state == DATA_IN) {
      virtual_device.user_callback_ctr[0][USB_TRANSACTION_IN](&virtual_device,
                                                               0);
    }
    if (virtual_device.control_state.state == LAST_DATA_IN) {
      virtual_device.user_callback_ctr[0][USB_TRANSACTION_IN](&virtual_device,
                                                               0);
    }
    if (virtual_device.control_state.state == STATUS_OUT) {
      virtual_device.user_callback_ctr[0][USB_TRANSACTION_OUT](&virtual_device,
                                                                0);
    }
  } else if (!control_stalled &&
             virtual_device.control_state.state == STATUS_IN) {
    virtual_device.user_callback_ctr[0][USB_TRANSACTION_IN](&virtual_device,
                                                             0);
  }
  *stalled = control_stalled ||
             virtual_device.control_state.state == STALLED;
  *response = control_in_data;
  *response_length = control_in_length;
  control_active = false;
}

static bool firmware_control_transfer(void *context, const uint8_t setup[8],
                                      const uint8_t *out_data,
                                      size_t out_length,
                                      const uint8_t **response,
                                      size_t *response_length) {
  (void)context;
  uint8_t *firmware_response;
  bool stalled;
  run_control_transfer(setup, out_data, out_length, &firmware_response,
                       response_length, &stalled);
  *response = firmware_response;
  return !stalled;
}

static uint32_t send_configuration(void) {
  uint8_t *bundle = NULL;
  size_t bundle_length = 0;
  if (!ugsp_discover_usb_personality(
          UGSP_USB_SPEED_FULL, firmware_control_transfer, NULL, &bundle,
          &bundle_length)) {
    die_message("USB personality discovery failed");
  }
  _usbd_reset(&virtual_device);
  uint32_t request_id = next_configuration_request++;
  if (request_id == 0) {
    die_message("USB configuration request ID overflow");
  }
  send_record(KIND_CONFIGURE, generation, request_id, bundle, bundle_length);
  ugsp_personality_cbor_free(bundle, bundle_length);
  return request_id;
}

static void close_usb_endpoints(void) {
  usb_enabled = false;
  for (size_t number = 1; number <= MAX_ENDPOINT_NUMBER; number++) {
    for (size_t direction = 0; direction < 2; direction++) {
      if (endpoints[number][direction].fd >= 0) {
        close(endpoints[number][direction].fd);
        endpoints[number][direction].fd = -1;
      }
    }
  }
}

static void install_usb_endpoints(struct worker_record *record) {
  if (record->body_length < 2) {
    close_record_fds(record);
    die_message("invalid USB endpoint map");
  }
  size_t count = be16(record->body);
  if (record->body_length != 2 + count * 4 || record->fd_count != count) {
    close_record_fds(record);
    die_message("USB endpoint map and descriptors differ");
  }
  close_usb_endpoints();
  for (size_t i = 0; i < count; i++) {
    const uint8_t *entry = record->body + 2 + i * 4;
    struct virtual_endpoint *endpoint = endpoint_for(entry[0]);
    if (endpoint == NULL || (entry[0] & 0x7f) == 0 || endpoint->fd >= 0) {
      close_record_fds(record);
      die_message("invalid or duplicate USB endpoint address");
    }
    endpoint->address = entry[0];
    endpoint->type = entry[1];
    endpoint->max_packet_size = be16(entry + 2);
    endpoint->fd = record->fds[i];
    record->fds[i] = -1;
  }
  record->fd_count = 0;
  generation = record->generation;
}

static bool configure_until_endpoints(bool replacement) {
  uint32_t request_id = send_configuration();
  bool quiesced = !replacement;
  for (;;) {
    struct worker_record record;
    receive_record(&record, 0);
    if (record.kind == KIND_CONFIGURATION_REJECTED &&
        record.generation == generation && record.request_id == request_id &&
        record.fd_count == 0) {
      fprintf(stderr,
              "virtual-trezor: supervisor rejected USB configuration: %.*s\n",
              (int)record.body_length, (const char *)record.body);
      return false;
    }
    if (record.kind == KIND_QUIESCE && replacement && !quiesced &&
        record.generation == generation && record.request_id == request_id &&
        record.body_length == 0 && record.fd_count == 0) {
      close_usb_endpoints();
      send_record(KIND_QUIESCED, generation, request_id, NULL, 0);
      _usbd_reset(&virtual_device);
      quiesced = true;
    } else if (record.kind == KIND_USB_ENDPOINTS && quiesced &&
               record.request_id == request_id) {
      install_usb_endpoints(&record);
      send_record(KIND_SERVING, generation, request_id, NULL, 0);
      return true;
    } else {
      close_record_fds(&record);
      die_message("unexpected supervisor record during USB configuration");
    }
  }
}

static void handle_usb_bus_event(const struct worker_record *record) {
  if (record->generation != generation || record->request_id != 0 ||
      record->body_length != 1 || record->fd_count != 0) {
    die_message("invalid supervisor USB bus event");
  }
  switch (record->body[0]) {
  case 0: /* bind */
    usb_enabled = false;
    _usbd_reset(&virtual_device);
    break;
  case 1: /* unbind */
  case 3: /* disable */
    usb_enabled = false;
    _usbd_reset(&virtual_device);
    break;
  case 2: { /* enable */
    if (usb_enabled) {
      break;
    }
    const uint8_t set_configuration[8] = {0x00, 0x09, 0x01, 0x00,
                                          0x00, 0x00, 0x00, 0x00};
    uint8_t *response;
    size_t response_length;
    bool stalled;
    run_control_transfer(set_configuration, NULL, 0, &response,
                         &response_length, &stalled);
    if (stalled) {
      die_message("firmware rejected SET_CONFIGURATION(1)");
    }
    usb_enabled = true;
    worker_display_resume();
    break;
  }
  case 5: /* suspend */
    if (virtual_device.user_callback_suspend != NULL) {
      virtual_device.user_callback_suspend();
    }
    if (emulator_flash_base != NULL &&
        msync(emulator_flash_base, FLASH_TOTAL_SIZE, MS_SYNC) != 0) {
      fprintf(stderr,
              "virtual-trezor: flash checkpoint on USB suspend failed: %s\n",
              strerror(errno));
    } else if (emulator_flash_base != NULL) {
      fputs("virtual-trezor: flash checkpointed on USB suspend\n", stderr);
    }
    worker_display_suspend();
    break;
  case 6: /* resume */
    if (virtual_device.user_callback_resume != NULL) {
      virtual_device.user_callback_resume();
    }
    worker_display_resume();
    break;
  default:
    die_message("unknown supervisor USB bus event");
  }
}

static void handle_usb_control_request(const struct worker_record *record) {
  if (record->generation != generation || record->request_id == 0 ||
      record->body_length < 8 || record->fd_count != 0) {
    die_message("invalid supervisor USB control request");
  }
  const uint8_t *setup = record->body;
  bool direction_in = (setup[0] & 0x80) != 0;
  size_t requested_length = (size_t)setup[6] | ((size_t)setup[7] << 8);
  size_t out_length = record->body_length - 8;
  if ((direction_in && out_length != 0) ||
      (!direction_in && out_length != requested_length)) {
    die_message("USB control request data does not match its setup packet");
  }
  uint8_t *firmware_response;
  size_t firmware_response_length;
  bool stalled;
  run_control_transfer(setup, record->body + 8, out_length,
                       &firmware_response, &firmware_response_length,
                       &stalled);
  if (stalled) {
    uint8_t disposition = RESPONSE_STALL;
    send_record(KIND_USB_CONTROL_RESPONSE, generation, record->request_id,
                &disposition, 1);
    return;
  }
  if (!direction_in) {
    uint8_t disposition = RESPONSE_ACK;
    send_record(KIND_USB_CONTROL_RESPONSE, generation, record->request_id,
                &disposition, 1);
    return;
  }
  if (firmware_response_length > requested_length) {
    firmware_response_length = requested_length;
  }
  uint8_t *body = malloc(1 + firmware_response_length);
  if (body == NULL) {
    die_errno("allocate USB control response");
  }
  body[0] = RESPONSE_DATA;
  memcpy(body + 1, firmware_response, firmware_response_length);
  send_record(KIND_USB_CONTROL_RESPONSE, generation, record->request_id, body,
              1 + firmware_response_length);
  free(body);
}

static void handle_quiesce(const struct worker_record *record) {
  if (record->generation != generation || record->body_length != 0 ||
      record->fd_count != 0) {
    die_message("invalid supervisor quiesce request");
  }
  close_usb_endpoints();
  send_record(KIND_QUIESCED, generation, record->request_id, NULL, 0);
  _usbd_reset(&virtual_device);
}

static void service_virtual_usb(int timeout_ms) {
  struct pollfd pollfds[2 + MAX_ENDPOINT_NUMBER];
  size_t count = 0;
  size_t control_index = count;
  pollfds[count++] = (struct pollfd){
      .fd = control_fd, .events = POLLIN | POLLHUP | POLLERR};
  int button_index = -1;
  if (buttonEventFd() >= 0) {
    button_index = (int)count;
    pollfds[count++] = (struct pollfd){
        .fd = buttonEventFd(), .events = POLLIN | POLLPRI};
  }
  uint8_t endpoint_numbers[MAX_ENDPOINT_NUMBER];
  size_t endpoint_indices[MAX_ENDPOINT_NUMBER];
  size_t endpoint_count = 0;
  if (usb_enabled) {
    for (uint8_t number = 1; number <= MAX_ENDPOINT_NUMBER; number++) {
      struct virtual_endpoint *endpoint = &endpoints[number][0];
      if (endpoint->fd >= 0 &&
          virtual_device.user_callback_ctr[number][USB_TRANSACTION_OUT] !=
              NULL) {
        endpoint_numbers[endpoint_count] = number;
        endpoint_indices[endpoint_count++] = count;
        pollfds[count++] =
            (struct pollfd){.fd = endpoint->fd, .events = POLLIN};
      }
    }
  }
  int ready = poll(pollfds, count, timeout_ms);
  if (ready < 0) {
    if (errno == EINTR) {
      return;
    }
    die_errno("poll virtual USB controller");
  }
  if (pollfds[control_index].revents != 0) {
    struct worker_record record;
    receive_record(&record, MSG_DONTWAIT);
    if (record.kind == KIND_QUIESCE) {
      handle_quiesce(&record);
    } else if (record.kind == KIND_USB_BUS_EVENT) {
      handle_usb_bus_event(&record);
    } else if (record.kind == KIND_USB_CONTROL_REQUEST) {
      handle_usb_control_request(&record);
    } else {
      close_record_fds(&record);
      die_message("unexpected runtime supervisor record");
    }
  }
  if (button_index >= 0) {
    if (pollfds[button_index].revents != 0) {
      buttonDrainEvents();
    }
  }
  for (size_t i = 0; usb_enabled && i < endpoint_count; i++) {
    short events = pollfds[endpoint_indices[i]].revents;
    if ((events & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
      usb_enabled = false;
      break;
    }
    if ((events & POLLIN) != 0) {
      uint8_t number = endpoint_numbers[i];
      virtual_device.user_callback_ctr[number][USB_TRANSACTION_OUT](
          &virtual_device, number);
    }
  }
}

void usbInit(void) {
  firmwareUsbInit();
  if (!configure_until_endpoints(false)) {
    die_message("initial USB configuration was rejected");
  }
  fputs("virtual-trezor: legacy USB stack is serving through virtual endpoints\n",
        stderr);
}

void usbPoll(void) { firmwareUsbPoll(); }

void waitAndProcessUSBRequests(uint32_t millis) {
  emulatorPoll();
  int timeout =
      worker_poll_timeout_ms(millis, worker_display_retry_timeout_ms());
  service_virtual_usb(timeout);
  /* firmwareUsbPoll() also invokes the controller poll hook before flushing
   * pending replies. The service above already performed that pass, so avoid
   * a second immediate poll while retaining the genuine firmware's
   * output-flush behavior. */
  controller_already_serviced = true;
  firmwareUsbPoll();
  controller_already_serviced = false;
}

void usbReconnect(void) {
  (void)configure_until_endpoints(true);
}

char usbTiny(char set) { return firmwareUsbTiny(set); }

void usbFlush(uint32_t millis) {
  struct timespec start;
  if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
    die_errno("read monotonic clock");
  }
  for (;;) {
    firmwareUsbPoll();
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
      die_errno("read monotonic clock");
    }
    uint64_t elapsed = (uint64_t)(now.tv_sec - start.tv_sec) * 1000U +
                       (uint64_t)(now.tv_nsec - start.tv_nsec) / 1000000U;
    if (elapsed >= millis) {
      return;
    }
    service_virtual_usb((int)(millis - elapsed));
  }
}
