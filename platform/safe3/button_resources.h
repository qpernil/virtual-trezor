/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef VIRTUAL_TREZOR_SAFE3_BUTTON_RESOURCES_H
#define VIRTUAL_TREZOR_SAFE3_BUTTON_RESOURCES_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  int lines_fd;
  bool owns_lines_fd;
} safe3_button_resources_t;

bool safe3_button_resources_acquire(safe3_button_resources_t *resources);
bool safe3_button_resources_read(const safe3_button_resources_t *resources,
                                 uint64_t *values);
void safe3_button_resources_release(safe3_button_resources_t *resources);

#endif
