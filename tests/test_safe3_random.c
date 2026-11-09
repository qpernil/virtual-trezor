#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <trezor_types.h>
#include <sys/rng.h>
#include <sys/rng_use_flags.h>
#include <sec/optiga.h>
#include <sec/tropic.h>

void random_buffer(uint8_t *, size_t);
static int failure_mode;
static bool tropic_available = true;

FILE *test_fopen(const char *path, const char *mode) {
  assert(strcmp(path, "/dev/urandom") == 0);
  return failure_mode == 1 ? NULL : fopen(path, mode);
}
size_t test_fread(void *buffer, size_t size, size_t count, FILE *stream) {
  return failure_mode == 2 ? 0 : fread(buffer, size, count, stream);
}
bool tropic_session_start(void) { return tropic_available; }

static void sample_child(uint8_t sample[32]) {
  int fds[2];
  assert(pipe(fds) == 0);
  pid_t child = fork();
  assert(child >= 0);
  if (child == 0) {
    close(fds[0]);
    uint8_t bytes[32];
    random_buffer(bytes, sizeof(bytes));
    assert(write(fds[1], bytes, sizeof(bytes)) == sizeof(bytes));
    _exit(0);
  }
  close(fds[1]);
  assert(read(fds[0], sample, 32) == 32);
  close(fds[0]);
  int status;
  assert(waitpid(child, &status, 0) == child);
  assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

static void check_failure(int mode) {
  pid_t child = fork();
  assert(child >= 0);
  if (child == 0) {
    struct rlimit limit = {0, 0};
    assert(setrlimit(RLIMIT_CORE, &limit) == 0);
    failure_mode = mode;
    uint8_t bytes[32];
    random_buffer(bytes, sizeof(bytes));
    _exit(1);
  }
  int status;
  assert(waitpid(child, &status, 0) == child);
  assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
}

int main(void) {
  uint8_t first[32], second[32];
  sample_child(first);
  sample_child(second);
  assert(memcmp(first, second, sizeof(first)) != 0);
  check_failure(1);
  check_failure(2);

  rng_use_flags_clear();
  assert(!rng_use_flag_is_set(RNG_TYPE_MCU));
  rng_fill_buffer(first, sizeof(first));
  assert(rng_use_flag_is_set(RNG_TYPE_MCU));
  assert(!rng_use_flag_is_set(RNG_TYPE_OPTIGA));
  rng_use_flags_clear();
  assert(optiga_random_buffer(second, sizeof(second)));
  assert(rng_use_flag_is_set(RNG_TYPE_OPTIGA));
  assert(memcmp(first, second, sizeof(first)) != 0);
  rng_use_flags_clear();
  tropic_available = false;
  memcpy(second, first, sizeof(first));
  assert(!tropic_random_buffer(second, sizeof(second)));
  assert(memcmp(first, second, sizeof(first)) == 0);
  assert(!rng_use_flag_is_set(RNG_TYPE_TROPIC));
  tropic_available = true;
  assert(tropic_random_buffer(second, sizeof(second)));
  assert(rng_use_flag_is_set(RNG_TYPE_TROPIC));
  assert(memcmp(first, second, sizeof(first)) != 0);
  return 0;
}
