/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef VIRTUAL_TREZOR_TIMER_LINUX_H
#define VIRTUAL_TREZOR_TIMER_LINUX_H

#include <stdbool.h>
#include <stdint.h>

struct worker_timer_state {
  uint64_t suspended_at_ms;
  uint64_t suspended_total_ms;
  bool suspended;
};

static inline void worker_timer_state_init(struct worker_timer_state *state) {
  state->suspended_at_ms = 0;
  state->suspended_total_ms = 0;
  state->suspended = false;
}

static inline void worker_timer_state_suspend(struct worker_timer_state *state,
                                              uint64_t raw_ms) {
  if (state->suspended) {
    return;
  }
  state->suspended_at_ms = raw_ms;
  state->suspended = true;
}

static inline void worker_timer_state_resume(struct worker_timer_state *state,
                                             uint64_t raw_ms) {
  if (!state->suspended) {
    return;
  }
  state->suspended_total_ms += raw_ms - state->suspended_at_ms;
  state->suspended = false;
}

static inline uint64_t worker_timer_state_value(
    const struct worker_timer_state *state, uint64_t raw_ms) {
  uint64_t effective_raw_ms =
      state->suspended ? state->suspended_at_ms : raw_ms;
  return effective_raw_ms - state->suspended_total_ms;
}

void worker_timer_suspend(void);
void worker_timer_resume(void);
bool worker_timer_is_suspended(void);

#endif
