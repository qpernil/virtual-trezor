/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef VIRTUAL_TREZOR_DISPLAY_LINUX_H
#define VIRTUAL_TREZOR_DISPLAY_LINUX_H

#include <stddef.h>
#include <stdint.h>

/* The producer retains ownership and keeps framebuffer valid until it submits
 * a replacement frame or the worker shuts down. */
void worker_display_write_frame(uint32_t pixel_format, size_t width,
                                size_t height, size_t stride,
                                const uint8_t *framebuffer, size_t length);
void worker_display_shutdown(void);
void worker_display_suspend(void);
void worker_display_resume(void);
int worker_display_retry_timeout_ms(void);

#endif
