/* SPDX-License-Identifier: GPL-3.0-or-later */

/* Display-only build: keep Core's button API present without SDL. */

#include <trezor_bsp.h>

#include <io/button.h>

#include "button_poll.h"

static bool initialized;

bool button_init(void) {
  if (initialized) {
    return true;
  }
  if (!button_poll_init()) {
    return false;
  }
  initialized = true;
  return true;
}

void button_deinit(void) {
  if (initialized) {
    button_poll_deinit();
  }
  initialized = false;
}

uint32_t button_get_state(void) { return 0; }

bool button_is_down(button_t button) {
  (void)button;
  return false;
}
