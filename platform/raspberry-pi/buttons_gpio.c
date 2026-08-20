/* SPDX-License-Identifier: GPL-3.0-or-later */

/* GPIO-only active-low button backend for the Raspberry Pi worker. */

#include <errno.h>
#include <linux/gpio.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "button_gpio_state.h"
#include "buttons.h"
#include "usb_functionfs.h"

#define BUTTON_NO_GPIO 5
#define BUTTON_YES_GPIO 26
#define BUTTON_CENTER_GPIO 13

static int button_lines_fd = -1;

static void fail(const char *operation) {
  fprintf(stderr, "virtual-trezor: %s: %s\n", operation, strerror(errno));
  exit(1);
}

static void initialize_gpio_buttons(void) {
  if (button_lines_fd >= 0) {
    return;
  }

  int gpiochip_fd = workerGpioResourceFd();
  struct gpio_v2_line_request request = {
      .offsets = {BUTTON_NO_GPIO, BUTTON_YES_GPIO, BUTTON_CENTER_GPIO},
      .consumer = "virtual-trezor-buttons",
      .config.flags = GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_BIAS_PULL_UP,
      .num_lines = 3,
  };
  if (ioctl(gpiochip_fd, GPIO_V2_GET_LINE_IOCTL, &request) != 0) {
    fail("request button GPIOs");
  }
  button_lines_fd = request.fd;
  fprintf(stderr,
          "virtual-trezor: reading active-low buttons on GPIO%d (left/No) "
          "GPIO%d (right/Yes), and GPIO%d (center/both)\n",
          BUTTON_NO_GPIO, BUTTON_YES_GPIO, BUTTON_CENTER_GPIO);
}

uint16_t buttonRead(void) {
  initialize_gpio_buttons();
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

  const uint8_t gpio_pressed = button_gpio_pressed(values.bits);
  uint16_t pressed = 0;
  if ((gpio_pressed & BUTTON_GPIO_NO_PRESSED) != 0) {
    pressed |= BTN_PIN_NO;
  }
  if ((gpio_pressed & BUTTON_GPIO_YES_PRESSED) != 0) {
    pressed |= BTN_PIN_YES;
  }
  return ~pressed;
}
