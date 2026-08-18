/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sh1106_stream.h"
#include "ssd1306_stream.h"
#include "worker_config.h"

int main(void) {
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

  char error[160];
  char *default_arguments[] = {"virtual-trezor-worker"};
  assert(worker_config_parse(1, default_arguments, error, sizeof(error)));
  assert(worker_i2c_display_controller() == I2C_DISPLAY_SSD1306);

  char *sh1106_arguments[] = {"virtual-trezor-worker",
                              "--i2c-display=sh1106"};
  assert(worker_config_parse(2, sh1106_arguments, error, sizeof(error)));
  assert(worker_i2c_display_controller() == I2C_DISPLAY_SH1106);
  assert(strcmp(worker_i2c_display_controller_name(), "sh1106") == 0);

  char *separate_arguments[] = {"virtual-trezor-worker", "--i2c-display",
                                "ssd1306"};
  assert(worker_config_parse(3, separate_arguments, error, sizeof(error)));
  assert(worker_i2c_display_controller() == I2C_DISPLAY_SSD1306);
  assert(strcmp(worker_i2c_display_controller_name(), "ssd1306") == 0);

  char *bad_arguments[] = {"virtual-trezor-worker",
                           "--i2c-display=unknown"};
  assert(!worker_config_parse(2, bad_arguments, error, sizeof(error)));
  assert(strstr(error, "ssd1306 or sh1106") != NULL);

  char *missing_arguments[] = {"virtual-trezor-worker", "--i2c-display"};
  assert(!worker_config_parse(2, missing_arguments, error, sizeof(error)));
  assert(strstr(error, "requires a value") != NULL);

  char *duplicate_arguments[] = {"virtual-trezor-worker",
                                 "--i2c-display=ssd1306",
                                 "--i2c-display=sh1106"};
  assert(!worker_config_parse(3, duplicate_arguments, error, sizeof(error)));
  assert(strstr(error, "only once") != NULL);

  char *unknown_arguments[] = {"virtual-trezor-worker", "--unknown"};
  assert(!worker_config_parse(2, unknown_arguments, error, sizeof(error)));
  assert(strstr(error, "unknown worker argument") != NULL);

  puts("SSD1306 and SH1106 command, framebuffer, and option tests passed.");
  return 0;
}
