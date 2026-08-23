// SPDX-License-Identifier: GPL-3.0-or-later

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "display_backends.h"
#include "display_linux.h"
#include "oled.h"
#include "worker_config.h"

struct DisplayBackendsHandle {
  int unused;
};

static struct DisplayBackendsHandle mock_handle;
static uint32_t observed_format;
static size_t observed_width;
static size_t observed_height;
static size_t observed_stride;
static const uint8_t *observed_framebuffer;
static size_t observed_length;
static unsigned write_count;
static uint8_t legacy_framebuffer[1024];

int display_backends_create(uint32_t backend, int bus_fd, int control_fd,
                            DisplayBackendsHandle **output) {
  assert(backend == DISPLAY_BACKENDS_ST7789_SPI);
  assert(bus_fd == 10);
  assert(control_fd == 11);
  *output = &mock_handle;
  return 0;
}

int display_backends_write_frame(DisplayBackendsHandle *handle,
                                 uint32_t pixel_format, size_t width,
                                 size_t height, size_t stride,
                                 const uint8_t *framebuffer, size_t length) {
  assert(handle == &mock_handle);
  observed_format = pixel_format;
  observed_width = width;
  observed_height = height;
  observed_stride = stride;
  observed_framebuffer = framebuffer;
  observed_length = length;
  write_count++;
  return 0;
}

int display_backends_shutdown(DisplayBackendsHandle *handle) {
  assert(handle == &mock_handle);
  return 0;
}

void display_backends_destroy(DisplayBackendsHandle *handle) {
  assert(handle == NULL || handle == &mock_handle);
}

display_backend_t worker_display_backend(void) { return DISPLAY_ST7789_SPI; }
const char *worker_display_backend_name(void) { return "st7789-spi"; }
int workerDisplayResourceFd(void) { return 10; }
int workerDisplayControlFd(void) { return 11; }
const uint8_t *oledGetBuffer(void) { return legacy_framebuffer; }

int main(void) {
  oledRefresh();
  assert(write_count == 1);
  assert(observed_format == DISPLAY_BACKENDS_MONO1_MSB_REVERSE_PAGE);
  assert(observed_width == 128);
  assert(observed_height == 64);
  assert(observed_stride == 128);
  assert(observed_framebuffer == legacy_framebuffer);
  assert(observed_length == sizeof(legacy_framebuffer));

  uint8_t safe3_framebuffer[128 * 64];
  memset(safe3_framebuffer, 0x5a, sizeof(safe3_framebuffer));
  worker_display_write_frame(DISPLAY_BACKENDS_MONO8, 128, 64, 128,
                             safe3_framebuffer, sizeof(safe3_framebuffer));
  assert(write_count == 2);
  assert(observed_format == DISPLAY_BACKENDS_MONO8);
  assert(observed_width == 128);
  assert(observed_height == 64);
  assert(observed_stride == 128);
  assert(observed_framebuffer == safe3_framebuffer);
  assert(observed_length == sizeof(safe3_framebuffer));

  worker_display_shutdown();
  return 0;
}
