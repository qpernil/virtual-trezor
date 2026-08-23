#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Legacy adapter for Core's secure Unix RNG path. This deliberately follows
 * core/embed/sys/rng/unix/rng.c: the firmware keeps calling random_buffer(),
 * while the Unix platform supplies bytes from the kernel CSPRNG.
 */
void random_buffer(uint8_t *buffer, size_t length) {
  static FILE *random_source = NULL;

  if (random_source == NULL) {
    random_source = fopen("/dev/urandom", "rb");
    if (random_source == NULL) {
      perror("fopen /dev/urandom");
      abort();
    }
  }

  if (fread(buffer, 1, length, random_source) != length) {
    fputs("short read from /dev/urandom\n", stderr);
    abort();
  }
}
