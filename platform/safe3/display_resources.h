/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef VIRTUAL_TREZOR_SAFE3_DISPLAY_RESOURCES_H
#define VIRTUAL_TREZOR_SAFE3_DISPLAY_RESOURCES_H

#include <stdbool.h>

typedef struct {
  int bus_fd;
  int control_fd;
  bool owns_bus_fd;
  bool owns_control_fd;
} safe3_display_resources_t;

/* Acquire the bus and control handles selected by the supervisor. Standalone
 * display/input diagnostic builds retain an ST7789 SPI fallback. */
bool safe3_display_resources_acquire(safe3_display_resources_t *resources);
void safe3_display_resources_release(safe3_display_resources_t *resources);

#endif
