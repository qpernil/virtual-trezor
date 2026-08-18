/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef VIRTUAL_TREZOR_SH1106_STREAM_H
#define VIRTUAL_TREZOR_SH1106_STREAM_H

#include <stddef.h>
#include <stdint.h>

#define SH1106_I2C_ADDRESS 0x3c
#define SH1106_WIDTH 128
#define SH1106_PAGE_COUNT 8
#define SH1106_FRAMEBUFFER_SIZE (SH1106_WIDTH * SH1106_PAGE_COUNT)
#define SH1106_PAGE_COMMAND_SIZE 4
#define SH1106_PAGE_DATA_SIZE (SH1106_WIDTH + 1)

const uint8_t *sh1106_init_message(size_t *length);
const uint8_t *sh1106_display_on_message(size_t *length);
void sh1106_build_page_command(uint8_t output[SH1106_PAGE_COMMAND_SIZE],
                               uint8_t page);
void sh1106_build_page_data(uint8_t output[SH1106_PAGE_DATA_SIZE],
                            const uint8_t framebuffer[SH1106_FRAMEBUFFER_SIZE],
                            uint8_t page);

#endif
