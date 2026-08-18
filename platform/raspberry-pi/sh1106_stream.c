/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "sh1106_stream.h"

#include <assert.h>
#include <string.h>

/*
 * Waveshare's SH1106 initialization sequence, adapted to use segment and COM
 * remapping for the genuine Trezor framebuffer's reversed physical ordering.
 * Display-on is delayed and sent separately, as in the vendor implementation.
 */
static const uint8_t init_message[] = {
    0x00, 0xae, 0x02, 0x10, 0x40, 0x81, 0xa0, 0xa1, 0xc8,
    0xa6, 0xa8, 0x3f, 0xd3, 0x00, 0xd5, 0x80, 0xd9, 0xf1,
    0xda, 0x12, 0xdb, 0x40, 0x20, 0x02, 0xa4, 0xa6,
};

static const uint8_t display_on_message[] = {0x00, 0xaf};

const uint8_t *sh1106_init_message(size_t *length) {
  *length = sizeof(init_message);
  return init_message;
}

const uint8_t *sh1106_display_on_message(size_t *length) {
  *length = sizeof(display_on_message);
  return display_on_message;
}

void sh1106_build_page_command(uint8_t output[SH1106_PAGE_COMMAND_SIZE],
                               uint8_t page) {
  assert(page < SH1106_PAGE_COUNT);
  output[0] = 0x00;
  output[1] = (uint8_t)(0xb0 | page);
  output[2] = 0x02;
  output[3] = 0x10;
}

void sh1106_build_page_data(uint8_t output[SH1106_PAGE_DATA_SIZE],
                            const uint8_t framebuffer[SH1106_FRAMEBUFFER_SIZE],
                            uint8_t page) {
  assert(page < SH1106_PAGE_COUNT);
  output[0] = 0x40;
  memcpy(output + 1, framebuffer + (size_t)page * SH1106_WIDTH, SH1106_WIDTH);
}
