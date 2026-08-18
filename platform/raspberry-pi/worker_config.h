/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef VIRTUAL_TREZOR_WORKER_CONFIG_H
#define VIRTUAL_TREZOR_WORKER_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
  I2C_DISPLAY_SSD1306,
  I2C_DISPLAY_SH1106,
} i2c_display_controller_t;

bool worker_config_parse(int argc, char *const argv[], char *error,
                         size_t error_size);
i2c_display_controller_t worker_i2c_display_controller(void);
const char *worker_i2c_display_controller_name(void);

#endif
