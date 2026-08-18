/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <stdio.h>

#include "worker_config.h"

int trezorFirmwareMain(void);

int main(int argc, char **argv) {
  char error[160];
  if (!worker_config_parse(argc, argv, error, sizeof(error))) {
    fprintf(stderr, "virtual-trezor: %s\n", error);
    fprintf(stderr,
            "usage: virtual-trezor-worker "
            "[--display=ssd1306-i2c|sh1106-i2c|sh1106-spi]\n");
    return 2;
  }
  return trezorFirmwareMain();
}
