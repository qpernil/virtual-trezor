// SPDX-License-Identifier: GPL-3.0-or-later

#include <assert.h>
#include <stdint.h>

#include <usb_worker_protocol.h>

int main(void) {
  const uint8_t activation[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  assert(ugsp_read_be64(activation) == UINT64_C(0x0102030405060708));
  return 0;
}
