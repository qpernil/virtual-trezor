/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef VIRTUAL_TREZOR_USB_FUNCTIONFS_H
#define VIRTUAL_TREZOR_USB_FUNCTIONFS_H

void workerReceiveSupervisorResources(void);
int workerDisplayResourceFd(void);
int workerDisplayControlFd(void);
int workerButtonLinesFd(void);

#endif
