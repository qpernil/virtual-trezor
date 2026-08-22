/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef VIRTUAL_TREZOR_DISPLAY_LINUX_H
#define VIRTUAL_TREZOR_DISPLAY_LINUX_H

void worker_display_shutdown(void);
void worker_display_suspend(void);
void worker_display_resume(void);
int worker_display_retry_timeout_ms(void);

#endif
