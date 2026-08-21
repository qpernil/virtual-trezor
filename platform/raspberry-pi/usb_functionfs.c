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
#include <linux/usb/functionfs.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "messages.h"
#include "buttons_gpio.h"
#include "display_linux.h"
#include "poll_timeout.h"
#include "usb.h"
#include "usb_functionfs.h"

#define STATE_DIRECTORY_ENV "USB_GADGET_STATE_DIRECTORY"
#define CONTROL_FD 3
#define UGSP_PACKET_SIZE 8
#define UGSP_VERSION 1
#define UGSP_PREBIND_RESOURCES 0x01
#define UGSP_POSTBIND_RESOURCES 0x02
#define UGSP_PREPARED 0x81
#define UGSP_SERVING 0x82

#define OUT_QUEUE_CAPACITY 64

static int control_fd = CONTROL_FD;
static int ep0_fd = -1;
static int main_out_fd = -1;
static int main_in_fd = -1;
static int display_resource_fd = -1;
static int display_control_fd = -1;
static int button_lines_fd = -1;
static int packet_event_fd = -1;
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

static bool usb_available(void) { return functionfs_enabled; }

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
  exit(1);
}

static void die_message(const char *message) {
  fprintf(stderr, "virtual-trezor: %s\n", message);
  exit(1);
}

static void set_worker_environment(void) __attribute__((constructor));

static void set_worker_environment(void) {
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

static void receive_fd_bundle(uint8_t expected_kind, size_t expected_count,
                              int *descriptors) {
  uint8_t packet[UGSP_PACKET_SIZE + 1] = {0};
  struct iovec iov = {.iov_base = packet, .iov_len = sizeof(packet)};
  union {
    struct cmsghdr alignment;
    uint8_t bytes[CMSG_SPACE(6 * sizeof(int))];
  } control;
  memset(&control, 0, sizeof(control));
  struct msghdr message = {
      .msg_iov = &iov,
      .msg_iovlen = 1,
      .msg_control = control.bytes,
      .msg_controllen = sizeof(control.bytes),
  };
  ssize_t length = recvmsg(control_fd, &message, MSG_CMSG_CLOEXEC);
  if (length < 0) {
    die_errno("receive supervisor resource bundle");
  }
  if (length == 0) {
    exit(0);
  }
  size_t declared_count = ((size_t)packet[6] << 8) | packet[7];
  if (length != UGSP_PACKET_SIZE || memcmp(packet, "UGSP", 4) != 0 ||
      packet[4] != UGSP_VERSION || packet[5] != expected_kind ||
      declared_count != expected_count ||
      (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) != 0) {
    die_message("invalid supervisor resource bundle");
  }
  struct cmsghdr *ancillary = CMSG_FIRSTHDR(&message);
  if (expected_count == 0) {
    if (ancillary != NULL) {
      die_message("unexpected descriptors in empty supervisor resource bundle");
    }
    return;
  }
  if (ancillary == NULL || ancillary->cmsg_level != SOL_SOCKET ||
      ancillary->cmsg_type != SCM_RIGHTS ||
      ancillary->cmsg_len != CMSG_LEN(expected_count * sizeof(int)) ||
      CMSG_NXTHDR(&message, ancillary) != NULL) {
    die_message("malformed SCM_RIGHTS supervisor resource bundle");
  }
  memcpy(descriptors, CMSG_DATA(ancillary), expected_count * sizeof(int));
}

void workerReceiveSupervisorResources(void) {
  if (ep0_fd >= 0) {
    die_message("supervisor resources were received more than once");
  }
  int prebind[6];
  receive_fd_bundle(UGSP_PREBIND_RESOURCES, 6, prebind);
  ep0_fd = prebind[0];
  main_out_fd = prebind[1];
  main_in_fd = prebind[2];
  display_resource_fd = prebind[3];
  display_control_fd = prebind[4];
  button_lines_fd = prebind[5];
}

int workerDisplayResourceFd(void) {
  if (display_resource_fd < 0) {
    die_message("display resource requested before supervisor handoff");
  }
  return display_resource_fd;
}

int workerDisplayControlFd(void) {
  if (display_control_fd < 0) {
    die_message("display-control lines requested before supervisor handoff");
  }
  return display_control_fd;
}

int workerButtonLinesFd(void) {
  if (button_lines_fd < 0) {
    die_message("button lines requested before supervisor handoff");
  }
  return button_lines_fd;
}

static void send_control(uint8_t kind) {
  const uint8_t packet[UGSP_PACKET_SIZE] = {'U',          'G',  'S', 'P',
                                            UGSP_VERSION, kind, 0,   0};
  ssize_t length = send(control_fd, packet, sizeof(packet), MSG_NOSIGNAL);
  if (length != (ssize_t)sizeof(packet)) {
    die_errno("send supervisor-control message");
  }
}

static uint8_t receive_control(int flags) {
  uint8_t packet[UGSP_PACKET_SIZE + 1] = {0};
  ssize_t length = recv(control_fd, packet, sizeof(packet), flags);
  if (length < 0) {
    if ((flags & MSG_DONTWAIT) != 0 &&
        (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return 0;
    }
    die_errno("receive supervisor-control message");
  }
  if (length == 0) {
    exit(0);
  }
  if (length != UGSP_PACKET_SIZE || memcmp(packet, "UGSP", 4) != 0 ||
      packet[4] != UGSP_VERSION || packet[6] != 0 || packet[7] != 0) {
    die_message("invalid supervisor-control message");
  }
  return packet[5];
}

static void handle_control_message(uint8_t kind) {
  if (kind != 0) {
    die_message("unexpected runtime supervisor-control message");
  }
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
      struct pollfd fds[2] = {
          {.fd = control_fd, .events = POLLIN | POLLHUP | POLLERR},
          {.fd = main_in_fd, .events = POLLOUT}};
      if (poll(fds, 2, 10) < 0 && errno != EINTR) {
        die_errno("poll FunctionFS input endpoint");
      }
      if ((fds[0].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
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
  if (ep0_fd < 0 || main_out_fd < 0 || main_in_fd < 0) {
    die_message("USB initialized before supervisor resource handoff");
  }
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

  send_control(UGSP_PREPARED);
  receive_fd_bundle(UGSP_POSTBIND_RESOURCES, 0, NULL);
  send_control(UGSP_SERVING);
  fputs("virtual-trezor: inherited FunctionFS interface is serving\n", stderr);
}

void waitAndProcessUSBRequests(uint32_t millis) {
  emulatorPoll();

  struct pollfd fds[4] = {
      {.fd = control_fd, .events = POLLIN | POLLHUP | POLLERR},
      {.fd = ep0_fd, .events = POLLIN},
      {.fd = packet_event_fd, .events = POLLIN},
      {.fd = buttonEventFd(), .events = POLLIN | POLLPRI}};
  int timeout =
      worker_poll_timeout_ms(millis, worker_display_retry_timeout_ms());
  int ready = poll(fds, 4, timeout);
  if (ready < 0 && errno != EINTR) {
    die_errno("poll FunctionFS endpoints");
  }
  if (ready > 0 && (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
    handle_control_message(receive_control(MSG_DONTWAIT));
  }
  if (ready > 0 && (fds[1].revents & POLLIN) != 0) {
    drain_ep0();
  }

  if (ready > 0 && (fds[2].revents & POLLIN) != 0) {
    process_out_packets();
  }
  if (ready > 0 && (fds[3].revents & (POLLIN | POLLPRI)) != 0) {
    buttonDrainEvents();
  }
  flush_messages();
}

void usbPoll(void) { waitAndProcessUSBRequests(0); }

void usbReconnect(void) {
  fputs("virtual-trezor: USB reconnect requested; ending worker incarnation\n",
        stderr);
  exit(0);
}

char usbTiny(char set) {
  char old = tiny;
  tiny = set;
  return old;
}

void usbFlush(uint32_t millis) {
  flush_messages();
  struct pollfd control = {.fd = control_fd,
                           .events = POLLIN | POLLHUP | POLLERR};
  int ready = poll(&control, 1, (int)millis);
  if (ready < 0 && errno != EINTR) {
    die_errno("wait while flushing FunctionFS endpoint");
  }
  if (ready > 0 && (control.revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
    handle_control_message(receive_control(MSG_DONTWAIT));
  }
}
