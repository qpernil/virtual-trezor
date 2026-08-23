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

/* Diagnostic display-first resource acquisition. If both inherited-FD
 * variables are present, use them without taking ownership. Otherwise open
 * the configured Linux device paths and request the ST7789 control lines. */
bool safe3_display_resources_acquire(safe3_display_resources_t *resources);
void safe3_display_resources_release(safe3_display_resources_t *resources);

#endif
