#include <assert.h>
#include <stdint.h>

#include "timer_linux.h"

int main(void) {
  struct worker_timer_state state;
  worker_timer_state_init(&state);
  assert(worker_timer_state_value(&state, 1000) == 1000);

  worker_timer_state_suspend(&state, 1050);
  assert(state.suspended);
  assert(worker_timer_state_value(&state, 1050) == 1050);
  assert(worker_timer_state_value(&state, 1300) == 1050);

  worker_timer_state_suspend(&state, 1200);
  assert(worker_timer_state_value(&state, 1400) == 1050);

  worker_timer_state_resume(&state, 1300);
  assert(!state.suspended);
  assert(worker_timer_state_value(&state, 1300) == 1050);
  assert(worker_timer_state_value(&state, 1325) == 1075);

  worker_timer_state_resume(&state, 1400);
  assert(worker_timer_state_value(&state, 1400) == 1150);

  worker_timer_state_suspend(&state, 1500);
  worker_timer_state_resume(&state, 1510);
  assert(worker_timer_state_value(&state, 1520) == 1260);
  return 0;
}
