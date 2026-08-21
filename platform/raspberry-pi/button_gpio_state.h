/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef VIRTUAL_TREZOR_BUTTON_GPIO_STATE_H
#define VIRTUAL_TREZOR_BUTTON_GPIO_STATE_H

#include <stdint.h>

enum {
  BUTTON_GPIO_NO_LINE = 1u << 0,
  BUTTON_GPIO_YES_LINE = 1u << 1,
  BUTTON_GPIO_CENTER_LINE = 1u << 2,
};

enum {
  BUTTON_GPIO_NO_PRESSED = 1u << 0,
  BUTTON_GPIO_YES_PRESSED = 1u << 1,
};

static inline uint8_t button_gpio_pressed(uint64_t line_values) {
  const int center_pressed = (line_values & BUTTON_GPIO_CENTER_LINE) != 0;
  uint8_t pressed = 0;

  if (center_pressed || (line_values & BUTTON_GPIO_NO_LINE) != 0) {
    pressed |= BUTTON_GPIO_NO_PRESSED;
  }
  if (center_pressed || (line_values & BUTTON_GPIO_YES_LINE) != 0) {
    pressed |= BUTTON_GPIO_YES_PRESSED;
  }
  return pressed;
}

#endif
