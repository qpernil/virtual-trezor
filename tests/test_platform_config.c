/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "button_gpio_state.h"
#include "worker_config.h"

int main(void) {
  assert(button_gpio_pressed(0) == 0);
  assert(button_gpio_pressed(BUTTON_GPIO_NO_LINE) == BUTTON_GPIO_NO_PRESSED);
  assert(button_gpio_pressed(BUTTON_GPIO_YES_LINE) ==
         BUTTON_GPIO_YES_PRESSED);
  assert(button_gpio_pressed(BUTTON_GPIO_CENTER_LINE) ==
         (BUTTON_GPIO_NO_PRESSED | BUTTON_GPIO_YES_PRESSED));

  char error[160];
  char *default_arguments[] = {"virtual-trezor-worker"};
  assert(worker_config_parse(1, default_arguments, error, sizeof(error)));
  assert(worker_display_backend() == DISPLAY_SH1106_SPI);
  assert(worker_display_is_sh1106());
  assert(worker_display_uses_spi());
  assert(strcmp(worker_display_backend_name(), "sh1106-spi") == 0);

  char *st7789_arguments[] = {"virtual-trezor-worker",
                              "--display=st7789-spi"};
  assert(worker_config_parse(2, st7789_arguments, error, sizeof(error)));
  assert(worker_display_backend() == DISPLAY_ST7789_SPI);
  assert(!worker_display_is_sh1106());
  assert(worker_display_is_st7789());
  assert(worker_display_uses_spi());
  assert(strcmp(worker_display_backend_name(), "st7789-spi") == 0);

  char *sh1106_arguments[] = {"virtual-trezor-worker",
                              "--display=sh1106-i2c"};
  assert(worker_config_parse(2, sh1106_arguments, error, sizeof(error)));
  assert(worker_display_backend() == DISPLAY_SH1106_I2C);
  assert(worker_display_is_sh1106());
  assert(!worker_display_uses_spi());
  assert(strcmp(worker_display_backend_name(), "sh1106-i2c") == 0);

  char *spi_arguments[] = {"virtual-trezor-worker",
                           "--display=sh1106-spi"};
  assert(worker_config_parse(2, spi_arguments, error, sizeof(error)));
  assert(worker_display_backend() == DISPLAY_SH1106_SPI);
  assert(worker_display_is_sh1106());
  assert(worker_display_uses_spi());
  assert(strcmp(worker_display_backend_name(), "sh1106-spi") == 0);

  char *separate_arguments[] = {"virtual-trezor-worker", "--i2c-display",
                                "ssd1306"};
  assert(worker_config_parse(3, separate_arguments, error, sizeof(error)));
  assert(worker_display_backend() == DISPLAY_SSD1306_I2C);
  assert(strcmp(worker_display_backend_name(), "ssd1306-i2c") == 0);

  char *bad_arguments[] = {"virtual-trezor-worker",
                           "--i2c-display=unknown"};
  assert(!worker_config_parse(2, bad_arguments, error, sizeof(error)));
  assert(strstr(error, "ssd1306 or sh1106") != NULL);

  char *missing_arguments[] = {"virtual-trezor-worker", "--i2c-display"};
  assert(!worker_config_parse(2, missing_arguments, error, sizeof(error)));
  assert(strstr(error, "requires a value") != NULL);

  char *bad_display_arguments[] = {"virtual-trezor-worker",
                                   "--display=unknown"};
  assert(!worker_config_parse(2, bad_display_arguments, error, sizeof(error)));
  assert(strstr(error, "unsupported --display backend") != NULL);

  char *missing_display_arguments[] = {"virtual-trezor-worker", "--display"};
  assert(!worker_config_parse(2, missing_display_arguments, error,
                              sizeof(error)));
  assert(strstr(error, "requires a value") != NULL);

  char *duplicate_arguments[] = {"virtual-trezor-worker",
                                 "--display=ssd1306-i2c",
                                 "--i2c-display=sh1106"};
  assert(!worker_config_parse(3, duplicate_arguments, error, sizeof(error)));
  assert(strstr(error, "only once") != NULL);

  char *unknown_arguments[] = {"virtual-trezor-worker", "--unknown"};
  assert(!worker_config_parse(2, unknown_arguments, error, sizeof(error)));
  assert(strstr(error, "unknown worker argument") != NULL);

  puts("Button mapping and display option tests passed.");
  return 0;
}
