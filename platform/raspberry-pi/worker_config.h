/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef VIRTUAL_TREZOR_WORKER_CONFIG_H
#define VIRTUAL_TREZOR_WORKER_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
  DISPLAY_SSD1306_I2C,
  DISPLAY_SH1106_I2C,
  DISPLAY_SH1106_SPI,
} display_backend_t;

bool worker_config_parse(int argc, char *const argv[], char *error,
                         size_t error_size);
display_backend_t worker_display_backend(void);
bool worker_display_is_sh1106(void);
bool worker_display_uses_spi(void);
const char *worker_display_backend_name(void);

#endif
