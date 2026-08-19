/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "st7789.h"

#include <stdbool.h>

/*
 * Register values follow the ST7789VM interface and the Waveshare 1.3-inch
 * LCD HAT module requirements. MADCTL 0x70 selects the HAT's horizontal scan
 * orientation so a logical 240x120 window is physically wider than it is tall.
 */
static const st7789_init_step_t init_steps[] = {
    {0x11, {0}, 0, 120},
    {0x36, {0x70}, 1, 0},
    {0x3a, {0x05}, 1, 0},
    {0xb2, {0x0c, 0x0c, 0x00, 0x33, 0x33}, 5, 0},
    {0xb7, {0x00}, 1, 0},
    {0xbb, {0x3f}, 1, 0},
    {0xc0, {0x2c}, 1, 0},
    {0xc2, {0x01}, 1, 0},
    {0xc3, {0x0d}, 1, 0},
    {0xc6, {0x0f}, 1, 0},
    {0xd0, {0xa7}, 1, 0},
    {0xd0, {0xa4, 0xa1}, 2, 0},
    {0xd6, {0xa1}, 1, 0},
    {0xe0,
     {0xf0, 0x00, 0x02, 0x01, 0x00, 0x00, 0x27, 0x43, 0x3f, 0x33, 0x0e,
      0x0e, 0x26, 0x2e},
     14,
     0},
    {0xe1,
     {0xf0, 0x07, 0x0d, 0x0d, 0x0b, 0x16, 0x26, 0x43, 0x3e, 0x3f, 0x19,
      0x19, 0x31, 0x3a},
     14,
     0},
    {0x21, {0}, 0, 0},
    {0x29, {0}, 0, 20},
};

const st7789_init_step_t *st7789_init_steps(size_t *count) {
  *count = sizeof(init_steps) / sizeof(init_steps[0]);
  return init_steps;
}

static void encode_coordinate(uint8_t output[2], uint16_t coordinate) {
  output[0] = (uint8_t)(coordinate >> 8);
  output[1] = (uint8_t)coordinate;
}

void st7789_build_window_data(uint8_t column[4], uint8_t row[4],
                              uint16_t x, uint16_t y, uint16_t width,
                              uint16_t height) {
  encode_coordinate(column, x);
  encode_coordinate(column + 2, (uint16_t)(x + width - 1));
  encode_coordinate(row, y);
  encode_coordinate(row + 2, (uint16_t)(y + height - 1));
}

static bool legacy_pixel_is_set(
    const uint8_t source[ST7789_SOURCE_FRAMEBUFFER_SIZE], uint16_t x,
    uint16_t y) {
  size_t offset = ST7789_SOURCE_FRAMEBUFFER_SIZE - 1 - x -
                  (size_t)(y / 8) * ST7789_SOURCE_WIDTH;
  uint8_t mask = (uint8_t)(1U << (7 - (y % 8)));
  return (source[offset] & mask) != 0;
}

void st7789_encode_legacy_frame(
    uint8_t destination[ST7789_RENDER_BUFFER_SIZE],
    const uint8_t source[ST7789_SOURCE_FRAMEBUFFER_SIZE]) {
  size_t output = 0;
  for (uint16_t y = 0; y < ST7789_RENDER_HEIGHT; ++y) {
    uint16_t source_y =
        (uint16_t)((uint32_t)y * ST7789_SOURCE_HEIGHT / ST7789_RENDER_HEIGHT);
    for (uint16_t x = 0; x < ST7789_RENDER_WIDTH; ++x) {
      uint16_t source_x = (uint16_t)((uint32_t)x * ST7789_SOURCE_WIDTH /
                                     ST7789_RENDER_WIDTH);
      uint8_t component = legacy_pixel_is_set(source, source_x, source_y)
                              ? 0xff
                              : 0x00;
      destination[output++] = component;
      destination[output++] = component;
    }
  }
}
