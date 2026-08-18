/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ssd1306_stream.h"

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

  puts("SSD1306 command and framebuffer stream tests passed.");
  return 0;
}
