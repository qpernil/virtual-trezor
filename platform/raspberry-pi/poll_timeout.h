/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef VIRTUAL_TREZOR_POLL_TIMEOUT_H
#define VIRTUAL_TREZOR_POLL_TIMEOUT_H

#include <stdbool.h>
#include <limits.h>
#include <stdint.h>

/*
 * Preserve the deadline requested by the upstream firmware. Platform events
 * and display recovery may wake it earlier, but an idle transport must not
 * suppress firmware timer processing such as auto-lock and busy-screen expiry.
 */
static inline int worker_poll_timeout_ms(bool suspended,
                                         uint32_t firmware_timeout_ms,
                                         int display_retry_timeout_ms) {
  if (suspended) {
    return -1;
  }
  int timeout = firmware_timeout_ms > INT_MAX ? INT_MAX
                                               : (int)firmware_timeout_ms;
  if (display_retry_timeout_ms >= 0 && display_retry_timeout_ms < timeout) {
    timeout = display_retry_timeout_ms;
  }
  return timeout;
}

#endif
