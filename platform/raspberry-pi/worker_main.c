/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <stdio.h>
#include <stdlib.h>

#include "display_linux.h"
#include "worker_config.h"

int trezorFirmwareMain(void);

int main(int argc, char **argv) {
  char error[160];
  if (!worker_config_parse(argc, argv, error, sizeof(error))) {
    fprintf(stderr, "virtual-trezor: %s\n", error);
    fprintf(stderr,
            "usage: virtual-trezor-worker "
            "[--display=ssd1306-i2c|sh1106-i2c|sh1106-spi|st7789-spi]\n");
    return 2;
  }
  if (atexit(worker_display_shutdown) != 0) {
    fputs("virtual-trezor: cannot register display shutdown\n", stderr);
    return 1;
  }
  return trezorFirmwareMain();
}
