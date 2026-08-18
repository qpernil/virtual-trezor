/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef VIRTUAL_TREZOR_SSD1306_STREAM_H
#define VIRTUAL_TREZOR_SSD1306_STREAM_H

#include <stddef.h>
#include <stdint.h>

#define SSD1306_I2C_ADDRESS 0x3c
#define SSD1306_FRAMEBUFFER_SIZE 1024
#define SSD1306_FRAME_MESSAGE_SIZE (SSD1306_FRAMEBUFFER_SIZE + 1)

const uint8_t *ssd1306_init_message(size_t *length);
const uint8_t *ssd1306_address_message(size_t *length);
void ssd1306_build_frame_message(
    uint8_t output[SSD1306_FRAME_MESSAGE_SIZE],
    const uint8_t framebuffer[SSD1306_FRAMEBUFFER_SIZE]);

#endif
