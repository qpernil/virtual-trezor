/* SPDX-License-Identifier: GPL-3.0-or-later */

/*
 * Transitional Raspberry Pi display backend.
 *
 * Upstream still composes the genuine Trezor One framebuffer. The upstream
 * SDL renderer is compiled under private symbol names, and this file provides
 * the public platform symbols while optionally mirroring every refresh to an
 * SSD1306-compatible I2C display supplied by usb-gadget-supervisor.
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/i2c-dev.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "oled.h"
#include "ssd1306_stream.h"

#define DISPLAY_I2C_FD_ENV "USB_GADGET_RESOURCE_DISPLAY_I2C_FD"
#define RETRY_DELAY_MS 1000

void sdlOledInit(void);
void sdlOledRefresh(void);
void sdlEmulatorPoll(void);

static int display_fd = -1;
static bool environment_checked = false;
static bool display_ready = false;
static bool error_reported = false;
static uint64_t retry_after_ms = 0;

static uint64_t monotonic_ms(void) {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
    return 0;
  }
  return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

static void report_error(const char *operation) {
  if (!error_reported) {
    fprintf(stderr, "virtual-trezor: I2C display %s failed: %s; "
                    "SDL display remains active\n",
            operation, strerror(errno));
    error_reported = true;
  }
  display_ready = false;
  retry_after_ms = monotonic_ms() + RETRY_DELAY_MS;
}

static bool write_message(const uint8_t *message, size_t length) {
  ssize_t written;
  do {
    written = write(display_fd, message, length);
  } while (written < 0 && errno == EINTR);

  if (written < 0) {
    return false;
  }
  if ((size_t)written != length) {
    errno = EIO;
    return false;
  }
  return true;
}

static void read_display_resource(void) {
  if (environment_checked) {
    return;
  }
  environment_checked = true;

  const char *value = getenv(DISPLAY_I2C_FD_ENV);
  if (value == NULL || *value == '\0') {
    return;
  }

  char *end = NULL;
  errno = 0;
  long parsed = strtol(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' || parsed < 0 ||
      parsed > INT_MAX || fcntl((int)parsed, F_GETFD) < 0) {
    fprintf(stderr, "virtual-trezor: ignoring invalid %s=%s\n",
            DISPLAY_I2C_FD_ENV, value);
    return;
  }
  display_fd = (int)parsed;
}

static bool initialize_display(void) {
  read_display_resource();
  if (display_fd < 0) {
    return false;
  }

  uint64_t now = monotonic_ms();
  if (retry_after_ms != 0 && now < retry_after_ms) {
    return false;
  }

  if (ioctl(display_fd, I2C_SLAVE, SSD1306_I2C_ADDRESS) < 0) {
    report_error("address selection");
    return false;
  }

  size_t init_length = 0;
  const uint8_t *init = ssd1306_init_message(&init_length);
  if (!write_message(init, init_length)) {
    report_error("initialization");
    return false;
  }

  if (error_reported) {
    fprintf(stderr, "virtual-trezor: I2C display recovered at address 0x%02x\n",
            SSD1306_I2C_ADDRESS);
  } else {
    fprintf(stderr, "virtual-trezor: mirroring display to I2C address 0x%02x\n",
            SSD1306_I2C_ADDRESS);
  }
  error_reported = false;
  retry_after_ms = 0;
  display_ready = true;
  return true;
}

static void mirror_framebuffer(void) {
  if (!display_ready && !initialize_display()) {
    return;
  }

  size_t address_length = 0;
  const uint8_t *address = ssd1306_address_message(&address_length);
  if (!write_message(address, address_length)) {
    report_error("address-window write");
    return;
  }

  uint8_t frame[SSD1306_FRAME_MESSAGE_SIZE];
  ssd1306_build_frame_message(frame, oledGetBuffer());
  if (!write_message(frame, sizeof(frame))) {
    report_error("frame write");
  }
}

void oledInit(void) {
  sdlOledInit();
  mirror_framebuffer();
}

void oledRefresh(void) {
  sdlOledRefresh();
  mirror_framebuffer();
}

void emulatorPoll(void) { sdlEmulatorPoll(); }
