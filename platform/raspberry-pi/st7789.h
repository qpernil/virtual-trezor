/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef VIRTUAL_TREZOR_ST7789_H
#define VIRTUAL_TREZOR_ST7789_H

#include <stddef.h>
#include <stdint.h>

#define ST7789_PANEL_WIDTH 240
#define ST7789_PANEL_HEIGHT 240
#define ST7789_SOURCE_WIDTH 128
#define ST7789_SOURCE_HEIGHT 64
#define ST7789_SOURCE_FRAMEBUFFER_SIZE 1024
#define ST7789_RENDER_WIDTH 240
#define ST7789_RENDER_HEIGHT 120
#define ST7789_RENDER_X 0
#define ST7789_RENDER_Y 60
#define ST7789_RENDER_BUFFER_SIZE \
  (ST7789_RENDER_WIDTH * ST7789_RENDER_HEIGHT * 2)
#define ST7789_MAX_INIT_DATA 14

typedef struct {
  uint8_t command;
  uint8_t data[ST7789_MAX_INIT_DATA];
  uint8_t data_length;
  unsigned int delay_ms;
} st7789_init_step_t;

const st7789_init_step_t *st7789_init_steps(size_t *count);
void st7789_build_window_data(uint8_t column[4], uint8_t row[4],
                              uint16_t x, uint16_t y, uint16_t width,
                              uint16_t height);
void st7789_encode_legacy_frame(
    uint8_t destination[ST7789_RENDER_BUFFER_SIZE],
    const uint8_t source[ST7789_SOURCE_FRAMEBUFFER_SIZE]);

#endif
