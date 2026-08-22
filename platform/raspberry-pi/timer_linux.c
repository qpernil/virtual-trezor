/* SPDX-License-Identifier: GPL-3.0-or-later */

#define _POSIX_C_SOURCE 200809L

/* Firmware time backed by CLOCK_MONOTONIC with USB suspend excluded. */

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "timer.h"
#include "timer_linux.h"

static struct worker_timer_state timer_state;

static uint64_t raw_monotonic_ms(void) {
  struct timespec now = {0};
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
    return 0;
  }
  return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

void timer_init(void) { worker_timer_state_init(&timer_state); }

uint32_t timer_ms(void) {
  return (uint32_t)worker_timer_state_value(&timer_state, raw_monotonic_ms());
}

void worker_timer_suspend(void) {
  worker_timer_state_suspend(&timer_state, raw_monotonic_ms());
}

void worker_timer_resume(void) {
  worker_timer_state_resume(&timer_state, raw_monotonic_ms());
}

bool worker_timer_is_suspended(void) { return timer_state.suspended; }
