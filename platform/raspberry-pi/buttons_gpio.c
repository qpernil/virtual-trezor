/* SPDX-License-Identifier: GPL-3.0-or-later */

/* GPIO-only active-low button backend for the Raspberry Pi worker. */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/gpio.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "buttons.h"

#define DISPLAY_GPIO_FD_ENV "USB_GADGET_RESOURCE_DISPLAY_GPIO_FD"
#define BUTTON_NO_GPIO 5
#define BUTTON_YES_GPIO 26

static int button_lines_fd = -1;

static void fail(const char *operation) {
  fprintf(stderr, "virtual-trezor: %s: %s\n", operation, strerror(errno));
  exit(1);
}

static int required_fd_from_environment(const char *name) {
  const char *value = getenv(name);
  if (value == NULL || *value == '\0') {
    fprintf(stderr, "virtual-trezor: missing required %s\n", name);
    exit(1);
  }

  char *end = NULL;
  errno = 0;
  long parsed = strtol(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' || parsed < 0 ||
      parsed > INT_MAX || fcntl((int)parsed, F_GETFD) < 0) {
    fprintf(stderr, "virtual-trezor: invalid required %s=%s\n", name, value);
    exit(1);
  }
  return (int)parsed;
}

static void initialize_gpio_buttons(void) {
  if (button_lines_fd >= 0) {
    return;
  }

  int gpiochip_fd = required_fd_from_environment(DISPLAY_GPIO_FD_ENV);
  struct gpio_v2_line_request request = {
      .offsets = {BUTTON_NO_GPIO, BUTTON_YES_GPIO},
      .consumer = "virtual-trezor-buttons",
      .config.flags =
          GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_BIAS_PULL_UP,
      .num_lines = 2,
  };
  if (ioctl(gpiochip_fd, GPIO_V2_GET_LINE_IOCTL, &request) != 0) {
    fail("request button GPIOs");
  }
  button_lines_fd = request.fd;
  fprintf(stderr,
          "virtual-trezor: reading active-low buttons on GPIO%d (left/No) "
          "and GPIO%d (right/Yes)\n",
          BUTTON_NO_GPIO, BUTTON_YES_GPIO);
}

uint16_t buttonRead(void) {
  initialize_gpio_buttons();
  struct gpio_v2_line_values values = {
      .mask = 0x3,
  };
  int result;
  do {
    result = ioctl(button_lines_fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &values);
  } while (result != 0 && errno == EINTR);
  if (result != 0) {
    fail("read button GPIOs");
  }

  uint16_t pressed = 0;
  if ((values.bits & 0x1) == 0) {
    pressed |= BTN_PIN_NO;
  }
  if ((values.bits & 0x2) == 0) {
    pressed |= BTN_PIN_YES;
  }
  return ~pressed;
}
