/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "button_resources.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef VIRTUAL_TREZOR_SUPERVISOR_USB
#include "supervisor_resources.h"
#endif

#ifdef __linux__
#include <linux/gpio.h>
#include <sys/ioctl.h>
#endif

#define DEFAULT_GPIO_PATH "/dev/gpiochip0"

static bool parse_fd(const char *name, const char *value, int *fd) {
  if (value == NULL || *value == '\0') {
    fprintf(stderr, "virtual-trezor-safe3: missing %s\n", name);
    return false;
  }
  char *end = NULL;
  errno = 0;
  long parsed = strtol(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' || parsed < 0 ||
      parsed > INT_MAX) {
    fprintf(stderr, "virtual-trezor-safe3: invalid %s=%s\n", name, value);
    return false;
  }
  *fd = (int)parsed;
  return true;
}

#ifdef __linux__
static int request_button_lines(const char *path) {
  int chip_fd = open(path, O_RDWR | O_CLOEXEC);
  if (chip_fd < 0) {
    return -1;
  }

  struct gpio_v2_line_request request = {0};
  request.offsets[0] = 5;
  request.offsets[1] = 26;
  request.offsets[2] = 13;
  request.num_lines = 3;
  request.config.flags = GPIO_V2_LINE_FLAG_INPUT |
                         GPIO_V2_LINE_FLAG_ACTIVE_LOW |
                         GPIO_V2_LINE_FLAG_BIAS_PULL_UP |
                         GPIO_V2_LINE_FLAG_EDGE_RISING |
                         GPIO_V2_LINE_FLAG_EDGE_FALLING;
  snprintf(request.consumer, sizeof(request.consumer), "%s",
           "virtual-trezor-safe3-buttons");

  int result = ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &request);
  int saved_errno = errno;
  close(chip_fd);
  errno = saved_errno;
  return result == 0 ? request.fd : -1;
}
#endif

bool safe3_button_resources_acquire(safe3_button_resources_t *resources) {
  resources->lines_fd = -1;
  resources->owns_lines_fd = false;

#ifdef VIRTUAL_TREZOR_SUPERVISOR_USB
  resources->lines_fd = safe3_supervisor_button_lines_fd();
  return resources->lines_fd >= 0;
#endif

  const char *fd_value = getenv("VIRTUAL_TREZOR_BUTTONS_FD");
  if (fd_value != NULL) {
    return parse_fd("VIRTUAL_TREZOR_BUTTONS_FD", fd_value,
                    &resources->lines_fd);
  }

#ifndef __linux__
  fputs("virtual-trezor-safe3: button resources require Linux\n", stderr);
  return false;
#else
  const char *gpio_path = getenv("VIRTUAL_TREZOR_BUTTON_GPIO");
  if (gpio_path == NULL || *gpio_path == '\0') {
    gpio_path = DEFAULT_GPIO_PATH;
  }

  resources->lines_fd = request_button_lines(gpio_path);
  if (resources->lines_fd < 0) {
    fprintf(stderr, "virtual-trezor-safe3: request %s button lines failed: %s\n",
            gpio_path, strerror(errno));
    return false;
  }
  resources->owns_lines_fd = true;
  return true;
#endif
}

bool safe3_button_resources_read(const safe3_button_resources_t *resources,
                                 uint64_t *values) {
#ifndef __linux__
  (void)resources;
  (void)values;
  return false;
#else
  struct gpio_v2_line_values line_values = {
      .bits = 0,
      .mask = 0x7,
  };
  int result;
  do {
    result = ioctl(resources->lines_fd, GPIO_V2_LINE_GET_VALUES_IOCTL,
                   &line_values);
  } while (result != 0 && errno == EINTR);
  if (result != 0) {
    return false;
  }
  *values = line_values.bits;
  return true;
#endif
}

void safe3_button_resources_release(safe3_button_resources_t *resources) {
  if (resources->owns_lines_fd && resources->lines_fd >= 0) {
    close(resources->lines_fd);
  }
  resources->lines_fd = -1;
  resources->owns_lines_fd = false;
}
