/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "display_resources.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __linux__
#include <linux/gpio.h>
#include <sys/ioctl.h>
#endif

#define DEFAULT_SPI_PATH "/dev/spidev0.0"
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
static int request_st7789_control_lines(const char *path) {
  int chip_fd = open(path, O_RDWR | O_CLOEXEC);
  if (chip_fd < 0) {
    return -1;
  }

  struct gpio_v2_line_request request = {0};
  request.offsets[0] = 25;
  request.offsets[1] = 27;
  request.offsets[2] = 24;
  request.num_lines = 3;
  request.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
  request.config.num_attrs = 1;
  request.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
  request.config.attrs[0].attr.values = 0;
  request.config.attrs[0].mask = 0x7;
  snprintf(request.consumer, sizeof(request.consumer), "%s",
           "virtual-trezor-safe3-display");

  int result = ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &request);
  int saved_errno = errno;
  close(chip_fd);
  errno = saved_errno;
  return result == 0 ? request.fd : -1;
}
#endif

bool safe3_display_resources_acquire(safe3_display_resources_t *resources) {
  memset(resources, 0, sizeof(*resources));
  resources->bus_fd = -1;
  resources->control_fd = -1;

  const char *bus_fd_value = getenv("VIRTUAL_TREZOR_DISPLAY_BUS_FD");
  const char *control_fd_value = getenv("VIRTUAL_TREZOR_DISPLAY_CONTROL_FD");
  if (bus_fd_value != NULL || control_fd_value != NULL) {
    int inherited_bus = -1;
    int inherited_control = -1;
    if (!parse_fd("VIRTUAL_TREZOR_DISPLAY_BUS_FD", bus_fd_value,
                  &inherited_bus) ||
        !parse_fd("VIRTUAL_TREZOR_DISPLAY_CONTROL_FD", control_fd_value,
                  &inherited_control)) {
      return false;
    }
    resources->bus_fd = inherited_bus;
    resources->control_fd = inherited_control;
    return true;
  }

#ifndef __linux__
  fprintf(stderr,
          "virtual-trezor-safe3: physical display resources require Linux\n");
  return false;
#else
  const char *bus_path = getenv("VIRTUAL_TREZOR_DISPLAY_BUS");
  const char *gpio_path = getenv("VIRTUAL_TREZOR_DISPLAY_GPIO");
  if (bus_path == NULL || *bus_path == '\0') {
    bus_path = DEFAULT_SPI_PATH;
  }
  if (gpio_path == NULL || *gpio_path == '\0') {
    gpio_path = DEFAULT_GPIO_PATH;
  }

  resources->bus_fd = open(bus_path, O_RDWR | O_CLOEXEC);
  if (resources->bus_fd < 0) {
    fprintf(stderr, "virtual-trezor-safe3: open %s failed: %s\n", bus_path,
            strerror(errno));
    return false;
  }
  resources->owns_bus_fd = true;

  resources->control_fd = request_st7789_control_lines(gpio_path);
  if (resources->control_fd < 0) {
    fprintf(stderr, "virtual-trezor-safe3: request %s lines failed: %s\n",
            gpio_path, strerror(errno));
    safe3_display_resources_release(resources);
    return false;
  }
  resources->owns_control_fd = true;
  return true;
#endif
}

void safe3_display_resources_release(safe3_display_resources_t *resources) {
  if (resources->owns_control_fd && resources->control_fd >= 0) {
    close(resources->control_fd);
  }
  if (resources->owns_bus_fd && resources->bus_fd >= 0) {
    close(resources->bus_fd);
  }
  resources->bus_fd = -1;
  resources->control_fd = -1;
  resources->owns_bus_fd = false;
  resources->owns_control_fd = false;
}
