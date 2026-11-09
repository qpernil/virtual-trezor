/* SPDX-License-Identifier: GPL-3.0-or-later */

/* Exercise the actual controller with a socketpair supervisor and pipe GPIO
 * edges. Only hardware/firmware boundaries are substituted. No USB or wallet
 * hardware is accessed by this test. */
#define getenv test_getenv
#define chdir test_chdir
#define geteuid test_geteuid
#define ioctl test_ioctl
#include "../platform/raspberry-pi/usb_functionfs.c"
#undef getenv
#undef chdir
#undef geteuid
#undef ioctl

#include <assert.h>
#include <stdarg.h>

static bool gpio_pressed;
static bool timer_suspended;
static bool display_suspended;
static int gpio_writer;
static int peer_fd;

char *test_getenv(const char *name) { (void)name; return "/"; }
int test_chdir(const char *path) { (void)path; return 0; }
uid_t test_geteuid(void) { return 1000; }
int test_ioctl(int fd, unsigned long request, ...) {
  assert(fd == reconnect_button_fd);
  assert(request == GPIO_V2_LINE_GET_VALUES_IOCTL);
  va_list args;
  va_start(args, request);
  struct gpio_v2_line_values *values = va_arg(args, void *);
  assert(values->mask == 1);
  values->bits = gpio_pressed;
  va_end(args);
  return 0;
}

uint8_t *emulator_flash_base;
void _usbd_reset(usbd_device *device) { device->current_config = 0; }
void firmwareUsbInit(void) { virtual_init(); }
void firmwareUsbPoll(void) {}
char firmwareUsbTiny(char set) { return set; }
int buttonEventFd(void) { return -1; }
void buttonDrainEvents(void) {}
void emulatorPoll(void) {}
uint32_t timer_ms(void) { return 0; }
void worker_timer_suspend(void) { timer_suspended = true; }
void worker_timer_resume(void) { timer_suspended = false; }
bool worker_timer_is_suspended(void) { return timer_suspended; }
void worker_display_suspend(void) { display_suspended = true; }
void worker_display_resume(void) { display_suspended = false; }
int worker_display_retry_timeout_ms(void) { return -1; }

bool ugsp_discover_usb_personality(uint8_t speed,
    ugsp_control_transfer_fn transfer, void *context, uint8_t **output,
    size_t *length) {
  (void)transfer; (void)context;
  assert(speed == UGSP_USB_SPEED_FULL);
  *output = malloc(1);
  assert(*output);
  **output = 0xa0;
  *length = 1;
  return true;
}
void ugsp_personality_cbor_free(uint8_t *bytes, size_t length) {
  (void)length; free(bytes);
}

static uint32_t peer_receive(uint8_t kind, uint32_t expected_generation,
                             bool empty) {
  uint8_t packet[64];
  ssize_t length = recv(peer_fd, packet, sizeof(packet), 0);
  assert(length >= PROTOCOL_HEADER_LENGTH);
  assert(memcmp(packet, "UGSP", 4) == 0 && packet[4] == 1);
  assert(packet[5] == kind && be16(packet + 6) == 0);
  assert(be32(packet + 8) == expected_generation);
  assert(be32(packet + 16) == (size_t)length - PROTOCOL_HEADER_LENGTH);
  assert((length == PROTOCOL_HEADER_LENGTH) == empty);
  return be32(packet + 12);
}

static void peer_send(uint8_t kind, uint32_t record_generation,
                      uint32_t request, const uint8_t *body, size_t length) {
  uint8_t packet[64] = {'U', 'G', 'S', 'P', 1};
  assert(PROTOCOL_HEADER_LENGTH + length <= sizeof(packet));
  packet[5] = kind;
  put_be32(packet + 8, record_generation);
  put_be32(packet + 12, request);
  put_be32(packet + 16, length);
  if (length) memcpy(packet + PROTOCOL_HEADER_LENGTH, body, length);
  assert(send(peer_fd, packet, PROTOCOL_HEADER_LENGTH + length, 0) ==
         (ssize_t)(PROTOCOL_HEADER_LENGTH + length));
}

struct exchange { uint32_t current_generation; bool eject; };
static void *supervisor_exchange(void *arg) {
  struct exchange *exchange = arg;
  uint32_t current = exchange->current_generation;
  uint32_t request = peer_receive(KIND_CONFIGURE, current, exchange->eject);
  assert(request != 0);
  if (exchange->eject) {
    /* A pending suspend can precede the quiesce reply. Ejection must process
     * it, close old descriptors, and resume the parked firmware afterwards. */
    uint8_t bus[UGSP_USB_BUS_EVENT_BODY_LENGTH] = {5};
    peer_send(KIND_USB_BUS_EVENT, current, 0, bus, sizeof(bus));
    peer_send(KIND_QUIESCE, current, request, NULL, 0);
    assert(peer_receive(KIND_QUIESCED, current, true) == request);
  } else {
    uint8_t empty_map[2] = {0};
    peer_send(KIND_USB_ENDPOINTS, current + 1, request, empty_map, 2);
    assert(peer_receive(KIND_SERVING, current + 1, true) == request);
  }
  return NULL;
}

static void press(bool pressed) {
  gpio_pressed = pressed;
  struct gpio_v2_line_event edge = {
      .id = pressed ? GPIO_V2_LINE_EVENT_RISING_EDGE : GPIO_V2_LINE_EVENT_FALLING_EDGE};
  assert(write(gpio_writer, &edge, sizeof(edge)) == sizeof(edge));
}
static void assert_no_request(void) {
  struct pollfd fd = {.fd = peer_fd, .events = POLLIN};
  assert(poll(&fd, 1, 0) == 0);
}
static void transition(bool eject) {
  struct exchange exchange = {generation, eject};
  pthread_t thread;
  assert(pthread_create(&thread, NULL, supervisor_exchange, &exchange) == 0);
  press(eject);
  service_virtual_usb_until_resumed(0);
  assert(pthread_join(thread, NULL) == 0);
  assert(usb_ejected == eject);
  assert(!worker_timer_is_suspended());
  assert(!display_suspended);
  assert_no_request();
}

int main(void) {
  alarm(10); /* Turn a broken handshake into a bounded test failure. */
  int pair[2], gpio[2];
  assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, pair) == 0);
  control_fd = pair[0]; peer_fd = pair[1];
  assert(pipe2(gpio, O_NONBLOCK | O_CLOEXEC) == 0);
  reconnect_button_fd = gpio[0]; gpio_writer = gpio[1];

  /* Holding KEY3 at startup declares readiness but must never attach. */
  gpio_pressed = true;
  usbInit();
  assert(usb_ejected && !usb_enabled && generation == 0);
  assert(peer_receive(KIND_CONFIGURE, 0, true) != 0);
  usbReconnect();
  service_virtual_usb(0);
  assert_no_request();
  transition(false);
  assert(generation == 1);

  for (unsigned cycle = 0; cycle < 3; cycle++) {
    int endpoint[2];
    assert(pipe(endpoint) == 0);
    endpoints[1][0].functionfs_fd = endpoint[0];
    usb_enabled = true;
    worker_timer_suspend();
    worker_display_suspend();
    transition(true);
    assert(!usb_enabled);
    assert(fcntl(endpoint[0], F_GETFD) == -1 && errno == EBADF);
    close(endpoint[1]);
    usbReconnect();
    for (unsigned hold = 0; hold < 3; hold++) service_virtual_usb(0);
    assert_no_request();
    transition(false);
    assert(generation == cycle + 2);
  }
  close(pair[0]); close(pair[1]); close(gpio[0]); close(gpio[1]);
  puts("Legacy KEY3: startup hold, quiesce, descriptor retirement, suspended eject, firmware reconnect suppression, and repeated insertion passed.");
  return 0;
}
