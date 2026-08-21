/* SPDX-License-Identifier: GPL-3.0-or-later */

/* GPIO-only active-low button backend for the Raspberry Pi worker. */

#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "button_gpio_state.h"
#include "buttons_gpio.h"
#include "buttons.h"
#include "usb_functionfs.h"

static int button_lines_fd = -1;
static uint8_t cached_gpio_pressed = 0;

static uint8_t read_gpio_pressed(void);

static void fail(const char *operation) {
  fprintf(stderr, "virtual-trezor: %s: %s\n", operation, strerror(errno));
  exit(1);
}

static void initialize_gpio_buttons(void) {
  if (button_lines_fd >= 0) {
    return;
  }

  button_lines_fd = workerButtonLinesFd();
  int flags = fcntl(button_lines_fd, F_GETFL);
  if (flags < 0 || fcntl(button_lines_fd, F_SETFL, flags | O_NONBLOCK) != 0) {
    fail("make button event descriptor nonblocking");
  }
  fputs("virtual-trezor: inherited event-driven logical button lines "
        "(left/No, right/Yes, center/both)\n",
        stderr);
  cached_gpio_pressed = read_gpio_pressed();
}

static uint8_t read_gpio_pressed(void) {
  struct gpio_v2_line_values values = {
      .mask =
          BUTTON_GPIO_NO_LINE | BUTTON_GPIO_YES_LINE | BUTTON_GPIO_CENTER_LINE,
  };
  int result;
  do {
    result = ioctl(button_lines_fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &values);
  } while (result != 0 && errno == EINTR);
  if (result != 0) {
    fail("read button GPIOs");
  }
  return button_gpio_pressed(values.bits);
}

int buttonEventFd(void) {
  initialize_gpio_buttons();
  return button_lines_fd;
}

void buttonDrainEvents(void) {
  initialize_gpio_buttons();
  struct gpio_v2_line_event event;
  for (;;) {
    ssize_t length = read(button_lines_fd, &event, sizeof(event));
    if (length == (ssize_t)sizeof(event)) {
      continue;
    }
    if (length < 0 && errno == EINTR) {
      continue;
    }
    if (length < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      break;
    }
    if (length < 0) {
      fail("read button edge event");
    }
    errno = EIO;
    fail("read complete button edge event");
  }
  cached_gpio_pressed = read_gpio_pressed();
}

uint16_t buttonRead(void) {
  initialize_gpio_buttons();
  cached_gpio_pressed = read_gpio_pressed();

  uint16_t pressed = 0;
  if ((cached_gpio_pressed & BUTTON_GPIO_NO_PRESSED) != 0) {
    pressed |= BTN_PIN_NO;
  }
  if ((cached_gpio_pressed & BUTTON_GPIO_YES_PRESSED) != 0) {
    pressed |= BTN_PIN_YES;
  }
  return ~pressed;
}
