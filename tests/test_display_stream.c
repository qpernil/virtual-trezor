/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "button_gpio_state.h"
#include "sh1106_stream.h"
#include "ssd1306_stream.h"
#include "st7789.h"
#include "worker_config.h"

static void set_legacy_pixel(uint8_t framebuffer[ST7789_SOURCE_FRAMEBUFFER_SIZE],
                             uint16_t x, uint16_t y) {
  size_t offset = ST7789_SOURCE_FRAMEBUFFER_SIZE - 1 - x -
                  (size_t)(y / 8) * ST7789_SOURCE_WIDTH;
  framebuffer[offset] |= (uint8_t)(1U << (7 - (y % 8)));
}

int main(void) {
  assert(button_gpio_pressed(BUTTON_GPIO_NO_LINE | BUTTON_GPIO_YES_LINE |
                             BUTTON_GPIO_CENTER_LINE) == 0);
  assert(button_gpio_pressed(BUTTON_GPIO_YES_LINE |
                             BUTTON_GPIO_CENTER_LINE) ==
         BUTTON_GPIO_NO_PRESSED);
  assert(button_gpio_pressed(BUTTON_GPIO_NO_LINE |
                             BUTTON_GPIO_CENTER_LINE) ==
         BUTTON_GPIO_YES_PRESSED);
  assert(button_gpio_pressed(BUTTON_GPIO_NO_LINE | BUTTON_GPIO_YES_LINE) ==
         (BUTTON_GPIO_NO_PRESSED | BUTTON_GPIO_YES_PRESSED));
  assert(button_gpio_pressed(0) ==
         (BUTTON_GPIO_NO_PRESSED | BUTTON_GPIO_YES_PRESSED));

  static const uint8_t expected_init[] = {
      0x00, 0xae, 0xd5, 0x80, 0xa8, 0x3f, 0xd3, 0x00, 0x40,
      0x8d, 0x14, 0x20, 0x00, 0xa1, 0xc8, 0xda, 0x12, 0x81,
      0xcf, 0xd9, 0xf1, 0xdb, 0x40, 0xa4, 0xa6, 0xaf,
  };
  static const uint8_t expected_address[] = {
      0x00, 0x21, 0x00, 0x7f, 0x22, 0x00, 0x07,
  };

  size_t length = 0;
  const uint8_t *message = ssd1306_init_message(&length);
  assert(length == sizeof(expected_init));
  assert(memcmp(message, expected_init, length) == 0);

  message = ssd1306_address_message(&length);
  assert(length == sizeof(expected_address));
  assert(memcmp(message, expected_address, length) == 0);

  uint8_t framebuffer[SSD1306_FRAMEBUFFER_SIZE];
  for (size_t i = 0; i < sizeof(framebuffer); ++i) {
    framebuffer[i] = (uint8_t)(i ^ (i >> 8));
  }

  uint8_t frame_message[SSD1306_FRAME_MESSAGE_SIZE];
  memset(frame_message, 0xa5, sizeof(frame_message));
  ssd1306_build_frame_message(frame_message, framebuffer);
  assert(frame_message[0] == 0x40);
  assert(memcmp(frame_message + 1, framebuffer, sizeof(framebuffer)) == 0);

  static const uint8_t expected_sh1106_init[] = {
      0x00, 0xae, 0x02, 0x10, 0x40, 0x81, 0xa0, 0xa1, 0xc8,
      0xa6, 0xa8, 0x3f, 0xd3, 0x00, 0xd5, 0x80, 0xd9, 0xf1,
      0xda, 0x12, 0xdb, 0x40, 0x20, 0x02, 0xa4, 0xa6,
  };
  message = sh1106_init_message(&length);
  assert(length == sizeof(expected_sh1106_init));
  assert(memcmp(message, expected_sh1106_init, length) == 0);

  static const uint8_t expected_display_on[] = {0x00, 0xaf};
  message = sh1106_display_on_message(&length);
  assert(length == sizeof(expected_display_on));
  assert(memcmp(message, expected_display_on, length) == 0);

  for (uint8_t page = 0; page < SH1106_PAGE_COUNT; ++page) {
    uint8_t command[SH1106_PAGE_COMMAND_SIZE];
    sh1106_build_page_command(command, page);
    assert(command[0] == 0x00);
    assert(command[1] == (uint8_t)(0xb0 | page));
    assert(command[2] == 0x02);
    assert(command[3] == 0x10);

    uint8_t data[SH1106_PAGE_DATA_SIZE];
    sh1106_build_page_data(data, framebuffer, page);
    assert(data[0] == 0x40);
    assert(memcmp(data + 1, framebuffer + (size_t)page * SH1106_WIDTH,
                  SH1106_WIDTH) == 0);
  }

  size_t init_step_count = 0;
  const st7789_init_step_t *init_steps =
      st7789_init_steps(&init_step_count);
  assert(init_step_count == 17);
  assert(init_steps[0].command == 0x11);
  assert(init_steps[0].delay_ms == 120);
  assert(init_steps[2].command == 0x3a);
  assert(init_steps[2].data_length == 1);
  assert(init_steps[2].data[0] == 0x05);
  assert(init_steps[init_step_count - 1].command == 0x29);

  uint8_t column[4];
  uint8_t row[4];
  st7789_build_window_data(column, row, ST7789_RENDER_X, ST7789_RENDER_Y,
                           ST7789_RENDER_WIDTH, ST7789_RENDER_HEIGHT);
  static const uint8_t expected_column[] = {0x00, 0x00, 0x00, 0xef};
  static const uint8_t expected_row[] = {0x00, 0x3c, 0x00, 0xb3};
  assert(memcmp(column, expected_column, sizeof(column)) == 0);
  assert(memcmp(row, expected_row, sizeof(row)) == 0);

  uint8_t legacy_frame[ST7789_SOURCE_FRAMEBUFFER_SIZE] = {0};
  for (uint16_t y = 0; y < ST7789_SOURCE_HEIGHT; ++y) {
    for (uint16_t x = 0; x < ST7789_SOURCE_WIDTH; ++x) {
      if (((x / 7) + (y / 5)) % 2 != 0) {
        set_legacy_pixel(legacy_frame, x, y);
      }
    }
  }
  static uint8_t rgb_frame[ST7789_RENDER_BUFFER_SIZE];
  st7789_encode_legacy_frame(rgb_frame, legacy_frame);
  for (uint16_t y = 0; y < ST7789_RENDER_HEIGHT; ++y) {
    uint16_t source_y =
        (uint16_t)((uint32_t)y * ST7789_SOURCE_HEIGHT / ST7789_RENDER_HEIGHT);
    for (uint16_t x = 0; x < ST7789_RENDER_WIDTH; ++x) {
      uint16_t source_x = (uint16_t)((uint32_t)x * ST7789_SOURCE_WIDTH /
                                     ST7789_RENDER_WIDTH);
      uint8_t expected =
          ((source_x / 7) + (source_y / 5)) % 2 != 0 ? 0xff : 0x00;
      size_t output = ((size_t)y * ST7789_RENDER_WIDTH + x) * 2;
      assert(rgb_frame[output] == expected);
      assert(rgb_frame[output + 1] == expected);
    }
  }

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

  puts("Button mapping, SSD1306/SH1106/ST7789 stream, and option tests "
       "passed.");
  return 0;
}
