/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef VIRTUAL_TREZOR_SAFE3_SUPERVISOR_RESOURCES_H
#define VIRTUAL_TREZOR_SAFE3_SUPERVISOR_RESOURCES_H

#include <stdint.h>

int safe3_supervisor_display_bus_fd(void);
int safe3_supervisor_display_control_fd(void);
int safe3_supervisor_button_lines_fd(void);
void virtual_trezor_wait_for_interrupt(uint32_t deadline);

#endif
