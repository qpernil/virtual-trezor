/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "worker_config.h"

#include <stdio.h>
#include <string.h>

static i2c_display_controller_t display_controller = I2C_DISPLAY_SSD1306;

static bool select_display(const char *value, char *error, size_t error_size) {
  if (strcmp(value, "ssd1306") == 0) {
    display_controller = I2C_DISPLAY_SSD1306;
    return true;
  }
  if (strcmp(value, "sh1106") == 0) {
    display_controller = I2C_DISPLAY_SH1106;
    return true;
  }
  snprintf(error, error_size,
           "--i2c-display must be ssd1306 or sh1106, not %s", value);
  return false;
}

bool worker_config_parse(int argc, char *const argv[], char *error,
                         size_t error_size) {
  display_controller = I2C_DISPLAY_SSD1306;
  bool display_seen = false;

  for (int index = 1; index < argc; ++index) {
    const char *argument = argv[index];
    const char *value = NULL;
    if (strcmp(argument, "--i2c-display") == 0) {
      if (++index >= argc) {
        snprintf(error, error_size, "--i2c-display requires a value");
        return false;
      }
      value = argv[index];
    } else if (strncmp(argument, "--i2c-display=", 14) == 0) {
      value = argument + 14;
    } else {
      snprintf(error, error_size, "unknown worker argument: %s", argument);
      return false;
    }

    if (display_seen) {
      snprintf(error, error_size, "--i2c-display may be specified only once");
      return false;
    }
    display_seen = true;
    if (!select_display(value, error, error_size)) {
      return false;
    }
  }
  return true;
}

i2c_display_controller_t worker_i2c_display_controller(void) {
  return display_controller;
}

const char *worker_i2c_display_controller_name(void) {
  switch (display_controller) {
    case I2C_DISPLAY_SSD1306:
      return "ssd1306";
    case I2C_DISPLAY_SH1106:
      return "sh1106";
  }
  return "unknown";
}
