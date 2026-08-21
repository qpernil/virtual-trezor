/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef VIRTUAL_TREZOR_BUTTONS_GPIO_H
#define VIRTUAL_TREZOR_BUTTONS_GPIO_H

#include <stdbool.h>

int buttonEventFd(void);
void buttonDrainEvents(void);

#endif
