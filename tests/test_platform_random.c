#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

void random_buffer(uint8_t *buffer, size_t length);

static void sample_fresh_process(uint8_t sample[32]) {
  int pipe_fds[2];
  assert(pipe(pipe_fds) == 0);

  pid_t child = fork();
  assert(child >= 0);
  if (child == 0) {
    uint8_t child_sample[32];
    close(pipe_fds[0]);
    random_buffer(child_sample, sizeof(child_sample));
    if (write(pipe_fds[1], child_sample, sizeof(child_sample)) !=
        (ssize_t)sizeof(child_sample)) {
      _exit(1);
    }
    _exit(0);
  }

  close(pipe_fds[1]);
  assert(read(pipe_fds[0], sample, 32) == 32);
  close(pipe_fds[0]);

  int status = 0;
  assert(waitpid(child, &status, 0) == child);
  assert(WIFEXITED(status));
  assert(WEXITSTATUS(status) == 0);
}

int main(void) {
  uint8_t first[32];
  uint8_t second[32];
  sample_fresh_process(first);
  sample_fresh_process(second);
  assert(memcmp(first, second, sizeof(first)) != 0);
  return 0;
}
