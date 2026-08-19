// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * FunctionFS transport for the unmodified Trezor One emulator firmware.
 *
 * This file deliberately implements the same firmware-facing functions as
 * legacy/firmware/udp.c. It is linked in its place; no upstream source is
 * patched.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/usb/functionfs.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/eventfd.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "messages.h"
#include "usb.h"

#define CONTROL_FD_ENV "USB_GADGET_CONTROL_FD"
#define STATE_DIRECTORY_ENV "USB_GADGET_STATE_DIRECTORY"
#define FUNCTIONFS_ENV "USB_GADGET_FUNCTIONFS_TREZOR"

#define UGSP_PACKET_SIZE 8
#define UGSP_VERSION 1
#define UGSP_RESOURCES_READY 0x01
#define UGSP_USB_ATTACHED 0x02
#define UGSP_USB_DETACHED 0x03
#define UGSP_SHUTDOWN 0x04
#define UGSP_FUNCTIONFS_READY 0x81
#define UGSP_RECONNECT_REQUEST 0x82
#define UGSP_STOPPED 0x83
#define UGSP_FATAL 0x84

#define OUT_QUEUE_CAPACITY 64

static int control_fd = -1;
static int ep0_fd = -1;
static int main_out_fd = -1;
static int main_in_fd = -1;
static int packet_event_fd = -1;
static bool supervisor_attached = false;
static bool functionfs_enabled = false;
static volatile char tiny = 0;

static pthread_mutex_t endpoint_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t endpoint_enabled = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_space = PTHREAD_COND_INITIALIZER;
static uint8_t out_queue[OUT_QUEUE_CAPACITY][USB_PACKET_SIZE];
static size_t out_queue_start = 0;
static size_t out_queue_count = 0;

static void die_errno(const char *operation);

static bool usb_available(void) {
  return supervisor_attached && functionfs_enabled;
}

static void set_functionfs_enabled(bool enabled) {
  int result = pthread_mutex_lock(&endpoint_mutex);
  if (result != 0) {
    errno = result;
    die_errno("lock FunctionFS endpoint state");
  }
  functionfs_enabled = enabled;
  if (enabled) {
    result = pthread_cond_broadcast(&endpoint_enabled);
  }
  int unlock_result = pthread_mutex_unlock(&endpoint_mutex);
  if (result == 0) {
    result = unlock_result;
  }
  if (result != 0) {
    errno = result;
    die_errno("update FunctionFS endpoint state");
  }
}

static void die_errno(const char *operation) {
  fprintf(stderr, "virtual-trezor: %s: %s\n", operation, strerror(errno));
  if (control_fd >= 0) {
    const uint8_t packet[UGSP_PACKET_SIZE] = {'U', 'G', 'S', 'P', UGSP_VERSION,
                                              UGSP_FATAL, 0, 0};
    (void)send(control_fd, packet, sizeof(packet), MSG_NOSIGNAL);
  }
  exit(1);
}

static void die_message(const char *message) {
  fprintf(stderr, "virtual-trezor: %s\n", message);
  if (control_fd >= 0) {
    const uint8_t packet[UGSP_PACKET_SIZE] = {'U', 'G', 'S', 'P', UGSP_VERSION,
                                              UGSP_FATAL, 0, 0};
    (void)send(control_fd, packet, sizeof(packet), MSG_NOSIGNAL);
  }
  exit(1);
}

static int parse_fd_environment(const char *name) {
  const char *value = getenv(name);
  if (value == NULL || *value == '\0') {
    fprintf(stderr, "virtual-trezor: missing inherited %s\n", name);
    exit(1);
  }
  errno = 0;
  char *end = NULL;
  long descriptor = strtol(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' || descriptor < 3 ||
      descriptor > INT_MAX) {
    fprintf(stderr, "virtual-trezor: invalid inherited %s=%s\n", name, value);
    exit(1);
  }
  return (int)descriptor;
}

static void set_worker_environment(void) __attribute__((constructor));

static void set_worker_environment(void) {
  if (getenv(CONTROL_FD_ENV) == NULL) {
    return;
  }
  if (geteuid() == 0) {
    fputs("virtual-trezor: refusing to run the firmware worker as root\n",
          stderr);
    _exit(1);
  }

  const char *state_directory = getenv(STATE_DIRECTORY_ENV);
  if (state_directory == NULL || state_directory[0] != '/' ||
      chdir(state_directory) != 0) {
    fprintf(stderr, "virtual-trezor: enter state directory: %s\n",
            strerror(errno));
    _exit(1);
  }

}

static void push_u16_le(uint8_t *buffer, size_t *position, uint16_t value) {
  buffer[(*position)++] = (uint8_t)value;
  buffer[(*position)++] = (uint8_t)(value >> 8);
}

static void push_u32_le(uint8_t *buffer, size_t *position, uint32_t value) {
  buffer[(*position)++] = (uint8_t)value;
  buffer[(*position)++] = (uint8_t)(value >> 8);
  buffer[(*position)++] = (uint8_t)(value >> 16);
  buffer[(*position)++] = (uint8_t)(value >> 24);
}

static void push_descriptor_set(uint8_t *buffer, size_t *position,
                                uint8_t interval) {
  const uint8_t interface[] = {9, 4, 0, 0, 2, 0xff, 0, 0, 1};
  const uint8_t out_prefix[] = {7, 5, 0x01, 3};
  const uint8_t in_prefix[] = {7, 5, 0x81, 3};
  memcpy(buffer + *position, interface, sizeof(interface));
  *position += sizeof(interface);
  memcpy(buffer + *position, out_prefix, sizeof(out_prefix));
  *position += sizeof(out_prefix);
  push_u16_le(buffer, position, USB_PACKET_SIZE);
  buffer[(*position)++] = interval;
  memcpy(buffer + *position, in_prefix, sizeof(in_prefix));
  *position += sizeof(in_prefix);
  push_u16_le(buffer, position, USB_PACKET_SIZE);
  buffer[(*position)++] = interval;
}

static size_t build_descriptors(uint8_t *buffer, size_t capacity) {
  const size_t descriptor_length = 20 + (9 + 7 + 7) * 2;
  if (capacity < descriptor_length) {
    die_message("internal FunctionFS descriptor buffer is too small");
  }
  size_t position = 0;
  push_u32_le(buffer, &position, FUNCTIONFS_DESCRIPTORS_MAGIC_V2);
  push_u32_le(buffer, &position, (uint32_t)descriptor_length);
  push_u32_le(buffer, &position,
              FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC);
  push_u32_le(buffer, &position, 3);
  push_u32_le(buffer, &position, 3);
  push_descriptor_set(buffer, &position, 1);
  push_descriptor_set(buffer, &position, 4);
  return position;
}

static size_t build_strings(uint8_t *buffer, size_t capacity) {
  static const char interface_name[] = "TREZOR Interface";
  const size_t strings_length = 16 + 2 + sizeof(interface_name);
  if (capacity < strings_length) {
    die_message("internal FunctionFS string buffer is too small");
  }
  size_t position = 0;
  push_u32_le(buffer, &position, FUNCTIONFS_STRINGS_MAGIC);
  push_u32_le(buffer, &position, (uint32_t)strings_length);
  push_u32_le(buffer, &position, 1);
  push_u32_le(buffer, &position, 1);
  push_u16_le(buffer, &position, 0x0409);
  memcpy(buffer + position, interface_name, sizeof(interface_name));
  position += sizeof(interface_name);
  return position;
}

static void write_all(int descriptor, const uint8_t *data, size_t length,
                      const char *operation) {
  while (length > 0) {
    ssize_t written = write(descriptor, data, length);
    if (written > 0) {
      data += (size_t)written;
      length -= (size_t)written;
    } else if (written < 0 && errno == EINTR) {
      continue;
    } else {
      die_errno(operation);
    }
  }
}

static void send_control(uint8_t kind) {
  const uint8_t packet[UGSP_PACKET_SIZE] = {'U', 'G', 'S', 'P', UGSP_VERSION,
                                            kind, 0, 0};
  ssize_t length = send(control_fd, packet, sizeof(packet), MSG_NOSIGNAL);
  if (length != (ssize_t)sizeof(packet)) {
    die_errno("send supervisor-control message");
  }
}

static uint8_t receive_control(int flags) {
  uint8_t packet[UGSP_PACKET_SIZE + 1];
  ssize_t length = recv(control_fd, packet, sizeof(packet), flags);
  if (length < 0) {
    if ((flags & MSG_DONTWAIT) != 0 &&
        (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return 0;
    }
    die_errno("receive supervisor-control message");
  }
  if (length == 0) {
    die_message("supervisor-control channel closed");
  }
  if (length != UGSP_PACKET_SIZE || memcmp(packet, "UGSP", 4) != 0 ||
      packet[4] != UGSP_VERSION || packet[6] != 0 || packet[7] != 0) {
    die_message("invalid supervisor-control message");
  }
  return packet[5];
}

static void stop_worker(void) __attribute__((noreturn));

static void stop_worker(void) {
  send_control(UGSP_STOPPED);
  exit(0);
}

static void handle_control_message(uint8_t kind) {
  switch (kind) {
    case 0:
      return;
    case UGSP_USB_ATTACHED:
      supervisor_attached = true;
      return;
    case UGSP_USB_DETACHED:
      supervisor_attached = false;
      return;
    case UGSP_SHUTDOWN:
      stop_worker();
    default:
      die_message("unexpected supervisor-control message");
  }
}

static int open_endpoint(const char *root, const char *name, int flags) {
  char path[PATH_MAX];
  if (snprintf(path, sizeof(path), "%s/%s", root, name) >= (int)sizeof(path)) {
    die_message("FunctionFS endpoint path is too long");
  }
  int descriptor = open(path, flags | O_NONBLOCK | O_CLOEXEC);
  if (descriptor < 0) {
    die_errno(path);
  }
  return descriptor;
}

static void drain_ep0(void) {
  struct usb_functionfs_event events[8];
  ssize_t length;
  do {
    length = read(ep0_fd, events, sizeof(events));
  } while (length < 0 && errno == EINTR);

  if (length < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    return;
  }
  if (length == 0 || (length < 0 && (errno == ENODEV || errno == ESHUTDOWN))) {
    set_functionfs_enabled(false);
    return;
  }
  if (length < 0) {
    die_errno("read FunctionFS control endpoint");
  }
  if ((size_t)length % sizeof(events[0]) != 0) {
    die_message("misaligned FunctionFS control event");
  }

  size_t count = (size_t)length / sizeof(events[0]);
  for (size_t i = 0; i < count; i++) {
    switch (events[i].type) {
      case FUNCTIONFS_ENABLE:
      case FUNCTIONFS_RESUME:
        set_functionfs_enabled(true);
        break;
      case FUNCTIONFS_DISABLE:
      case FUNCTIONFS_UNBIND:
        set_functionfs_enabled(false);
        break;
      case FUNCTIONFS_BIND:
      case FUNCTIONFS_SETUP:
      case FUNCTIONFS_SUSPEND:
        break;
      default:
        die_message("unknown FunctionFS control event");
    }
  }
}

static void wait_for_enabled_endpoint(void) {
  int result = pthread_mutex_lock(&endpoint_mutex);
  if (result == 0) {
    while (!functionfs_enabled && result == 0) {
      result = pthread_cond_wait(&endpoint_enabled, &endpoint_mutex);
    }
    int unlock_result = pthread_mutex_unlock(&endpoint_mutex);
    if (result == 0) {
      result = unlock_result;
    }
  }
  if (result != 0) {
    errno = result;
    die_errno("wait for FunctionFS endpoint enable");
  }
}

static void enqueue_out_packet(const uint8_t *packet) {
  int result = pthread_mutex_lock(&queue_mutex);
  while (result == 0 && out_queue_count == OUT_QUEUE_CAPACITY) {
    result = pthread_cond_wait(&queue_space, &queue_mutex);
  }
  if (result == 0) {
    size_t position = (out_queue_start + out_queue_count) % OUT_QUEUE_CAPACITY;
    memcpy(out_queue[position], packet, USB_PACKET_SIZE);
    out_queue_count++;
  }
  int unlock_result = pthread_mutex_unlock(&queue_mutex);
  if (result == 0) {
    result = unlock_result;
  }
  if (result != 0) {
    errno = result;
    die_errno("queue FunctionFS OUT packet");
  }

  uint64_t notification = 1;
  ssize_t length = write(packet_event_fd, &notification, sizeof(notification));
  if (length < 0 && errno == EAGAIN) {
    return;
  }
  if (length != (ssize_t)sizeof(notification)) {
    die_errno("signal FunctionFS OUT packet");
  }
}

static void *out_reader(void *unused) {
  (void)unused;
  uint8_t packet[USB_PACKET_SIZE];
  for (;;) {
    wait_for_enabled_endpoint();
    ssize_t length = read(main_out_fd, packet, sizeof(packet));
    if (length == USB_PACKET_SIZE) {
      enqueue_out_packet(packet);
    } else if (length < 0 && errno == EINTR) {
      continue;
    } else if (length < 0 &&
               (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENODEV ||
                errno == EPIPE || errno == ESHUTDOWN)) {
      usleep(1000);
    } else if (length == 0) {
      usleep(1000);
    } else if (length > 0) {
      fprintf(stderr,
              "virtual-trezor: ignored FunctionFS OUT packet length %zd\n",
              length);
    } else {
      die_errno("read FunctionFS OUT endpoint");
    }
  }
  return NULL;
}

static bool dequeue_out_packet(uint8_t *packet) {
  int result = pthread_mutex_lock(&queue_mutex);
  bool available = result == 0 && out_queue_count > 0;
  if (available) {
    memcpy(packet, out_queue[out_queue_start], USB_PACKET_SIZE);
    out_queue_start = (out_queue_start + 1) % OUT_QUEUE_CAPACITY;
    out_queue_count--;
    result = pthread_cond_signal(&queue_space);
  }
  int unlock_result = pthread_mutex_unlock(&queue_mutex);
  if (result == 0) {
    result = unlock_result;
  }
  if (result != 0) {
    errno = result;
    die_errno("dequeue FunctionFS OUT packet");
  }
  return available;
}

static void process_out_packets(void) {
  uint64_t notifications;
  while (read(packet_event_fd, &notifications, sizeof(notifications)) < 0 &&
         errno == EINTR) {
  }

  uint8_t packet[USB_PACKET_SIZE];
  while (dequeue_out_packet(packet)) {
    if (!tiny) {
      msg_read_common('n', packet, sizeof(packet));
    } else {
      msg_read_tiny(packet, sizeof(packet));
    }
  }
}

static bool write_packet(const uint8_t *packet) {
  while (usb_available()) {
    ssize_t length = write(main_in_fd, packet, USB_PACKET_SIZE);
    if (length == USB_PACKET_SIZE) {
      return true;
    }
    if (length < 0 && errno == EINTR) {
      continue;
    }
    if (length < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      struct pollfd fds[2] = {{.fd = control_fd, .events = POLLIN},
                              {.fd = main_in_fd, .events = POLLOUT}};
      if (poll(fds, 2, 10) < 0 && errno != EINTR) {
        die_errno("poll FunctionFS input endpoint");
      }
      if ((fds[0].revents & POLLIN) != 0) {
        handle_control_message(receive_control(MSG_DONTWAIT));
      }
      continue;
    }
    if (length < 0 &&
        (errno == ENODEV || errno == EPIPE || errno == ESHUTDOWN)) {
      set_functionfs_enabled(false);
      return false;
    }
    die_message("short or failed FunctionFS input transfer");
  }
  return false;
}

static void flush_messages(void) {
  if (!usb_available()) {
    return;
  }
  const uint8_t *data;
  while ((data = msg_out_data()) != NULL) {
    if (!write_packet(data)) {
      return;
    }
  }
}

void usbInit(void) {
  control_fd = parse_fd_environment(CONTROL_FD_ENV);
  if (receive_control(0) != UGSP_RESOURCES_READY) {
    die_message("supervisor did not send RESOURCES_READY");
  }

  const char *functionfs = getenv(FUNCTIONFS_ENV);
  if (functionfs == NULL || functionfs[0] != '/') {
    die_message("missing absolute FunctionFS mount path");
  }
  ep0_fd = open_endpoint(functionfs, "ep0", O_RDWR);

  uint8_t descriptors[80];
  size_t descriptor_length = build_descriptors(descriptors, sizeof(descriptors));
  write_all(ep0_fd, descriptors, descriptor_length,
            "write FunctionFS descriptors");

  uint8_t strings[64];
  size_t strings_length = build_strings(strings, sizeof(strings));
  write_all(ep0_fd, strings, strings_length, "write FunctionFS strings");

  main_out_fd = open_endpoint(functionfs, "ep1", O_RDONLY);
  main_in_fd = open_endpoint(functionfs, "ep2", O_WRONLY);
  packet_event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (packet_event_fd < 0) {
    die_errno("create FunctionFS packet event");
  }
  pthread_t reader;
  int thread_result = pthread_create(&reader, NULL, out_reader, NULL);
  if (thread_result != 0) {
    errno = thread_result;
    die_errno("create FunctionFS OUT reader");
  }
  thread_result = pthread_detach(reader);
  if (thread_result != 0) {
    errno = thread_result;
    die_errno("detach FunctionFS OUT reader");
  }

  send_control(UGSP_FUNCTIONFS_READY);
  if (receive_control(0) != UGSP_USB_ATTACHED) {
    die_message("supervisor did not send USB_ATTACHED");
  }
  supervisor_attached = true;
  fputs("virtual-trezor: FunctionFS main interface attached\n", stderr);
}

void waitAndProcessUSBRequests(uint32_t millis) {
  emulatorPoll();

  struct pollfd fds[3] = {{.fd = control_fd, .events = POLLIN},
                          {.fd = ep0_fd, .events = POLLIN},
                          {.fd = packet_event_fd, .events = POLLIN}};
  int ready = poll(fds, 3, (int)millis);
  if (ready < 0 && errno != EINTR) {
    die_errno("poll FunctionFS endpoints");
  }
  if (ready > 0 && (fds[0].revents & POLLIN) != 0) {
    handle_control_message(receive_control(MSG_DONTWAIT));
  }
  if (ready > 0 && (fds[1].revents & POLLIN) != 0) {
    drain_ep0();
  }

  if (ready > 0 && (fds[2].revents & POLLIN) != 0) {
    process_out_packets();
  }
  flush_messages();
}

void usbPoll(void) { waitAndProcessUSBRequests(0); }

void usbReconnect(void) {
  if (control_fd >= 0) {
    send_control(UGSP_RECONNECT_REQUEST);
  }
}

char usbTiny(char set) {
  char old = tiny;
  tiny = set;
  return old;
}

void usbFlush(uint32_t millis) {
  flush_messages();
  struct pollfd control = {.fd = control_fd, .events = POLLIN};
  int ready = poll(&control, 1, (int)millis);
  if (ready < 0 && errno != EINTR) {
    die_errno("wait while flushing FunctionFS endpoint");
  }
  if (ready > 0 && (control.revents & POLLIN) != 0) {
    handle_control_message(receive_control(MSG_DONTWAIT));
  }
}
