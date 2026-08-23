/* SPDX-License-Identifier: GPL-3.0-or-later */

/* GPIO state provider for Core's genuine button poller. Core retains ownership
 * of per-task state, debouncing, and press/release event generation. */

#include <trezor_bsp.h>

#include <io/button.h>

#include "button_poll.h"
#include "button_resources.h"
#include "../raspberry-pi/button_gpio_state.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct {
  bool initialized;
  bool trace;
  bool traced_state_valid;
  uint32_t traced_state;
  safe3_button_resources_t resources;
} safe3_button_driver_t;

static safe3_button_driver_t g_button_driver = {
    .resources = {.lines_fd = -1},
};

static uint32_t read_button_state(void) {
  uint64_t values = 0;
  if (!safe3_button_resources_read(&g_button_driver.resources, &values)) {
    fputs("virtual-trezor-safe3: read button GPIOs failed\n", stderr);
    exit(1);
  }

  uint8_t pressed = button_gpio_pressed(values);
  uint32_t state = 0;
  if ((pressed & BUTTON_GPIO_NO_PRESSED) != 0) {
    state |= (1U << BTN_LEFT);
  }
  if ((pressed & BUTTON_GPIO_YES_PRESSED) != 0) {
    state |= (1U << BTN_RIGHT);
  }

  safe3_button_driver_t *drv = &g_button_driver;
  if (drv->trace &&
      (!drv->traced_state_valid || state != drv->traced_state)) {
    fprintf(stderr,
            "virtual-trezor-safe3: button state left=%s right=%s\n",
            (state & (1U << BTN_LEFT)) != 0 ? "down" : "up",
            (state & (1U << BTN_RIGHT)) != 0 ? "down" : "up");
    drv->traced_state = state;
    drv->traced_state_valid = true;
  }
  return state;
}

bool button_init(void) {
  safe3_button_driver_t *drv = &g_button_driver;
  if (drv->initialized) {
    return true;
  }

  if (!safe3_button_resources_acquire(&drv->resources)) {
    return false;
  }
  drv->trace = getenv("VIRTUAL_TREZOR_BUTTON_TRACE") != NULL;
  if (!button_poll_init()) {
    safe3_button_resources_release(&drv->resources);
    return false;
  }

  drv->initialized = true;
  fputs("virtual-trezor-safe3: GPIO buttons ready "
        "(left, right, center/both)\n",
        stderr);
  return true;
}

void button_deinit(void) {
  safe3_button_driver_t *drv = &g_button_driver;
  if (drv->initialized) {
    button_poll_deinit();
  }
  safe3_button_resources_release(&drv->resources);
  drv->initialized = false;
}

uint32_t button_get_state(void) {
  if (!g_button_driver.initialized) {
    return 0;
  }
  return read_button_state();
}

bool button_is_down(button_t button) {
  if (button < BTN_LEFT || button > BTN_POWER) {
    return false;
  }
  return (button_get_state() & (1U << button)) != 0;
}
