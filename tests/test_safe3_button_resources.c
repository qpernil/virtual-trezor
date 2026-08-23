/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "button_resources.h"

int main(void) {
  safe3_button_resources_t resources;

  assert(setenv("VIRTUAL_TREZOR_BUTTONS_FD", "bad", 1) == 0);
  assert(!safe3_button_resources_acquire(&resources));

  int pipe_fds[2];
  assert(pipe(pipe_fds) == 0);
  char fd_value[32];
  int length = snprintf(fd_value, sizeof(fd_value), "%d", pipe_fds[0]);
  assert(length > 0 && (size_t)length < sizeof(fd_value));
  assert(setenv("VIRTUAL_TREZOR_BUTTONS_FD", fd_value, 1) == 0);
  assert(safe3_button_resources_acquire(&resources));
  assert(resources.lines_fd == pipe_fds[0]);
  assert(!resources.owns_lines_fd);
  safe3_button_resources_release(&resources);
  assert(resources.lines_fd == -1);
  assert(fcntl(pipe_fds[0], F_GETFD) >= 0);

  close(pipe_fds[0]);
  close(pipe_fds[1]);
  unsetenv("VIRTUAL_TREZOR_BUTTONS_FD");
  return 0;
}
