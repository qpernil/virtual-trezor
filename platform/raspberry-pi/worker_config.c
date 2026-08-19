/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "worker_config.h"

#include <stdio.h>
#include <string.h>

static display_backend_t display_backend = DISPLAY_SH1106_SPI;

static bool select_backend(const char *value, char *error, size_t error_size) {
  if (strcmp(value, "ssd1306-i2c") == 0) {
    display_backend = DISPLAY_SSD1306_I2C;
    return true;
  }
  if (strcmp(value, "sh1106-i2c") == 0) {
    display_backend = DISPLAY_SH1106_I2C;
    return true;
  }
  if (strcmp(value, "sh1106-spi") == 0) {
    display_backend = DISPLAY_SH1106_SPI;
    return true;
  }
  if (strcmp(value, "st7789-spi") == 0) {
    display_backend = DISPLAY_ST7789_SPI;
    return true;
  }
  snprintf(error, error_size, "unsupported --display backend: %s", value);
  return false;
}

static bool select_legacy_i2c_display(const char *value, char *error,
                                      size_t error_size) {
  if (strcmp(value, "ssd1306") == 0) {
    display_backend = DISPLAY_SSD1306_I2C;
    return true;
  }
  if (strcmp(value, "sh1106") == 0) {
    display_backend = DISPLAY_SH1106_I2C;
    return true;
  }
  snprintf(error, error_size,
           "--i2c-display must be ssd1306 or sh1106, not %s", value);
  return false;
}

bool worker_config_parse(int argc, char *const argv[], char *error,
                         size_t error_size) {
  display_backend = DISPLAY_SH1106_SPI;
  bool display_seen = false;

  for (int index = 1; index < argc; ++index) {
    const char *argument = argv[index];
    const char *value = NULL;
    bool legacy_i2c = false;
    if (strcmp(argument, "--display") == 0) {
      if (++index >= argc) {
        snprintf(error, error_size, "--display requires a value");
        return false;
      }
      value = argv[index];
    } else if (strncmp(argument, "--display=", 10) == 0) {
      value = argument + 10;
    } else if (strcmp(argument, "--i2c-display") == 0) {
      legacy_i2c = true;
      if (++index >= argc) {
        snprintf(error, error_size, "--i2c-display requires a value");
        return false;
      }
      value = argv[index];
    } else if (strncmp(argument, "--i2c-display=", 14) == 0) {
      legacy_i2c = true;
      value = argument + 14;
    } else {
      snprintf(error, error_size, "unknown worker argument: %s", argument);
      return false;
    }

    if (display_seen) {
      snprintf(error, error_size,
               "a display backend may be specified only once");
      return false;
    }
    display_seen = true;
    if (legacy_i2c
            ? !select_legacy_i2c_display(value, error, error_size)
            : !select_backend(value, error, error_size)) {
      return false;
    }
  }
  return true;
}

display_backend_t worker_display_backend(void) { return display_backend; }

bool worker_display_is_sh1106(void) {
  return display_backend == DISPLAY_SH1106_I2C ||
         display_backend == DISPLAY_SH1106_SPI;
}

bool worker_display_is_st7789(void) {
  return display_backend == DISPLAY_ST7789_SPI;
}

bool worker_display_uses_spi(void) {
  return display_backend == DISPLAY_SH1106_SPI ||
         display_backend == DISPLAY_ST7789_SPI;
}

const char *worker_display_backend_name(void) {
  switch (display_backend) {
    case DISPLAY_SSD1306_I2C:
      return "ssd1306-i2c";
    case DISPLAY_SH1106_I2C:
      return "sh1106-i2c";
    case DISPLAY_SH1106_SPI:
      return "sh1106-spi";
    case DISPLAY_ST7789_SPI:
      return "st7789-spi";
  }
  return "unknown";
}
