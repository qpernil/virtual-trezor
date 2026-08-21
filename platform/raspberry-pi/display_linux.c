/* SPDX-License-Identifier: GPL-3.0-or-later */

/* Firmware framebuffer policy over the shared display-backends C ABI. */

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "display_backends.h"
#include "oled.h"
#include "usb_functionfs.h"
#include "worker_config.h"

#define RETRY_DELAY_MS 1000
#define FRAME_WIDTH 128
#define FRAME_HEIGHT 64
#define FRAME_STRIDE 128
#define TREZOR_FRAMEBUFFER_SIZE 1024

static DisplayBackendsHandle *display = NULL;
static bool error_reported = false;
static uint64_t retry_after_ms = 0;

static uint64_t monotonic_ms(void) {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
    return 0;
  }
  return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

static uint32_t display_backend_kind(void) {
  switch (worker_display_backend()) {
    case DISPLAY_SSD1306_I2C:
      return DISPLAY_BACKENDS_SSD1306_I2C;
    case DISPLAY_SH1106_I2C:
      return DISPLAY_BACKENDS_SH1106_I2C;
    case DISPLAY_SH1106_SPI:
      return DISPLAY_BACKENDS_SH1106_SPI;
    case DISPLAY_ST7789_SPI:
      return DISPLAY_BACKENDS_ST7789_SPI;
  }
  return UINT32_MAX;
}

static void release_display(void) {
  display_backends_destroy(display);
  display = NULL;
}

static void report_error(const char *operation, int error) {
  if (!error_reported) {
    fprintf(stderr,
            "virtual-trezor: %s display %s failed: %s; retrying\n",
            worker_display_backend_name(), operation, strerror(error));
    error_reported = true;
  }
  release_display();
  retry_after_ms = monotonic_ms() + RETRY_DELAY_MS;
}

static bool initialize_display(void) {
  uint64_t now = monotonic_ms();
  if (retry_after_ms != 0 && now < retry_after_ms) {
    return false;
  }

  int error = display_backends_create(
      display_backend_kind(), workerDisplayResourceFd(),
      workerDisplayControlFd(), &display);
  if (error != 0) {
    report_error("initialization", error);
    return false;
  }

  fprintf(stderr, "virtual-trezor: %s display %s through display-backends\n",
          worker_display_backend_name(),
          error_reported ? "recovered" : "ready");
  error_reported = false;
  retry_after_ms = 0;
  return true;
}

static void write_framebuffer(void) {
  if (display == NULL && !initialize_display()) {
    return;
  }

  int error = display_backends_write_frame(
      display, DISPLAY_BACKENDS_MONO1_MSB_REVERSE_PAGE, FRAME_WIDTH,
      FRAME_HEIGHT, FRAME_STRIDE, oledGetBuffer(), TREZOR_FRAMEBUFFER_SIZE);
  if (error != 0) {
    report_error("frame write", error);
  }
}

void worker_display_shutdown(void) {
  if (display == NULL) {
    return;
  }

  int error = display_backends_shutdown(display);
  if (error != 0) {
    fprintf(stderr, "virtual-trezor: %s display shutdown failed: %s\n",
            worker_display_backend_name(), strerror(error));
  } else {
    fprintf(stderr, "virtual-trezor: %s display cleared\n",
            worker_display_backend_name());
  }
  release_display();
}

void oledInit(void) { write_framebuffer(); }

void oledRefresh(void) { write_framebuffer(); }

void emulatorPoll(void) {
  if (display != NULL || retry_after_ms == 0) {
    return;
  }
  if (monotonic_ms() < retry_after_ms) {
    return;
  }
  write_framebuffer();
}

int worker_display_retry_timeout_ms(void) {
  if (retry_after_ms == 0) {
    return -1;
  }
  uint64_t now = monotonic_ms();
  if (now >= retry_after_ms) {
    return 0;
  }
  uint64_t remaining = retry_after_ms - now;
  return remaining > INT_MAX ? INT_MAX : (int)remaining;
}
