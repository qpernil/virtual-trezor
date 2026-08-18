/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "ssd1306_stream.h"

#include <string.h>

/*
 * The leading byte in each message is the SSD1306 I2C control byte:
 * 0x00 selects commands and 0x40 selects display RAM data.
 */
static const uint8_t init_message[] = {
    0x00, 0xae, 0xd5, 0x80, 0xa8, 0x3f, 0xd3, 0x00, 0x40,
    0x8d, 0x14, 0x20, 0x00, 0xa1, 0xc8, 0xda, 0x12, 0x81,
    0xcf, 0xd9, 0xf1, 0xdb, 0x40, 0xa4, 0xa6, 0xaf,
};

/* Horizontal addressing: all 128 columns and all eight pages. */
static const uint8_t address_message[] = {
    0x00, 0x21, 0x00, 0x7f, 0x22, 0x00, 0x07,
};

const uint8_t *ssd1306_init_message(size_t *length) {
  *length = sizeof(init_message);
  return init_message;
}

const uint8_t *ssd1306_address_message(size_t *length) {
  *length = sizeof(address_message);
  return address_message;
}

void ssd1306_build_frame_message(
    uint8_t output[SSD1306_FRAME_MESSAGE_SIZE],
    const uint8_t framebuffer[SSD1306_FRAMEBUFFER_SIZE]) {
  output[0] = 0x40;
  memcpy(output + 1, framebuffer, SSD1306_FRAMEBUFFER_SIZE);
}
