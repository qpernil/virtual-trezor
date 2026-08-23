/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <assert.h>
#include <stdlib.h>

#include "display_resources.h"

int main(void) {
  safe3_display_resources_t resources;

  assert(setenv("VIRTUAL_TREZOR_DISPLAY_BUS_FD", "17", 1) == 0);
  assert(setenv("VIRTUAL_TREZOR_DISPLAY_CONTROL_FD", "18", 1) == 0);
  assert(safe3_display_resources_acquire(&resources));
  assert(resources.bus_fd == 17);
  assert(resources.control_fd == 18);
  assert(!resources.owns_bus_fd);
  assert(!resources.owns_control_fd);
  safe3_display_resources_release(&resources);

  assert(setenv("VIRTUAL_TREZOR_DISPLAY_BUS_FD", "bad", 1) == 0);
  assert(!safe3_display_resources_acquire(&resources));

  assert(unsetenv("VIRTUAL_TREZOR_DISPLAY_BUS_FD") == 0);
  assert(!safe3_display_resources_acquire(&resources));

  return 0;
}
