/* SPDX-License-Identifier: GPL-3.0-or-later */

/* I2C-only display backend for the Raspberry Pi worker. */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/gpio.h>
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
#include "sh1106_stream.h"
#include "ssd1306_stream.h"
#include "worker_config.h"

#define DISPLAY_I2C_FD_ENV "USB_GADGET_RESOURCE_DISPLAY_I2C_FD"
#define DISPLAY_GPIO_FD_ENV "USB_GADGET_RESOURCE_DISPLAY_GPIO_FD"
#define SH1106_RESET_GPIO 25
#define RETRY_DELAY_MS 1000

static int display_fd = -1;
static bool display_ready = false;
static bool error_reported = false;
static uint64_t retry_after_ms = 0;
static int reset_line_fd = -1;
static bool reset_resource_checked = false;

static uint64_t monotonic_ms(void) {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
    return 0;
  }
  return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

static int required_fd_from_environment(const char *name) {
  const char *value = getenv(name);
  if (value == NULL || *value == '\0') {
    fprintf(stderr, "virtual-trezor: missing required %s\n", name);
    exit(1);
  }

  char *end = NULL;
  errno = 0;
  long parsed = strtol(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' || parsed < 0 ||
      parsed > INT_MAX || fcntl((int)parsed, F_GETFD) < 0) {
    fprintf(stderr, "virtual-trezor: invalid required %s=%s\n", name, value);
    exit(1);
  }
  return (int)parsed;
}

static void report_error(const char *operation) {
  if (!error_reported) {
    fprintf(stderr,
            "virtual-trezor: I2C display %s failed: %s; retrying\n",
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

static bool set_reset_line(bool high) {
  struct gpio_v2_line_values values = {
      .bits = high ? 1 : 0,
      .mask = 1,
  };
  return ioctl(reset_line_fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &values) == 0;
}

static void pulse_sh1106_reset(void) {
  if (!reset_resource_checked) {
    reset_resource_checked = true;
    int gpiochip_fd = required_fd_from_environment(DISPLAY_GPIO_FD_ENV);
    struct gpio_v2_line_request request = {
        .offsets = {SH1106_RESET_GPIO},
        .consumer = "virtual-trezor-display",
        .config.flags = GPIO_V2_LINE_FLAG_OUTPUT,
        .num_lines = 1,
    };
    if (ioctl(gpiochip_fd, GPIO_V2_GET_LINE_IOCTL, &request) == 0) {
      reset_line_fd = request.fd;
    } else {
      fprintf(stderr,
              "virtual-trezor: cannot request SH1106 reset GPIO %d: %s; "
              "relying on power-on reset\n",
              SH1106_RESET_GPIO, strerror(errno));
    }
  }

  if (reset_line_fd < 0) {
    return;
  }
  if (!set_reset_line(true)) {
    fprintf(stderr, "virtual-trezor: cannot drive SH1106 reset GPIO: %s\n",
            strerror(errno));
    close(reset_line_fd);
    reset_line_fd = -1;
    return;
  }
  usleep(100000);
  set_reset_line(false);
  usleep(100000);
  set_reset_line(true);
  usleep(100000);
}

static uint8_t display_address(void) {
  if (worker_i2c_display_controller() == I2C_DISPLAY_SH1106) {
    return SH1106_I2C_ADDRESS;
  }
  return SSD1306_I2C_ADDRESS;
}

static bool send_initialization(void) {
  size_t length = 0;
  const uint8_t *message;
  if (worker_i2c_display_controller() == I2C_DISPLAY_SH1106) {
    pulse_sh1106_reset();
    message = sh1106_init_message(&length);
    if (!write_message(message, length)) {
      return false;
    }
    usleep(100000);
    message = sh1106_display_on_message(&length);
  } else {
    message = ssd1306_init_message(&length);
  }
  return write_message(message, length);
}

static bool initialize_display(void) {
  if (display_fd < 0) {
    display_fd = required_fd_from_environment(DISPLAY_I2C_FD_ENV);
  }
  uint64_t now = monotonic_ms();
  if (retry_after_ms != 0 && now < retry_after_ms) {
    return false;
  }

  uint8_t address = display_address();
  if (ioctl(display_fd, I2C_SLAVE, address) < 0) {
    report_error("address selection");
    return false;
  }
  if (!send_initialization()) {
    report_error("initialization");
    return false;
  }

  fprintf(stderr, "virtual-trezor: %s I2C display %s at address 0x%02x\n",
          worker_i2c_display_controller_name(),
          error_reported ? "recovered" : "ready", address);
  error_reported = false;
  retry_after_ms = 0;
  display_ready = true;
  return true;
}

static void write_framebuffer(void) {
  if (!display_ready && !initialize_display()) {
    return;
  }

  const uint8_t *framebuffer = oledGetBuffer();
  if (worker_i2c_display_controller() == I2C_DISPLAY_SH1106) {
    uint8_t command[SH1106_PAGE_COMMAND_SIZE];
    uint8_t data[SH1106_PAGE_DATA_SIZE];
    for (uint8_t page = 0; page < SH1106_PAGE_COUNT; ++page) {
      sh1106_build_page_command(command, page);
      sh1106_build_page_data(data, framebuffer, page);
      if (!write_message(command, sizeof(command)) ||
          !write_message(data, sizeof(data))) {
        report_error("SH1106 page write");
        return;
      }
    }
  } else {
    size_t address_length = 0;
    const uint8_t *address = ssd1306_address_message(&address_length);
    if (!write_message(address, address_length)) {
      report_error("SSD1306 address-window write");
      return;
    }
    uint8_t frame[SSD1306_FRAME_MESSAGE_SIZE];
    ssd1306_build_frame_message(frame, framebuffer);
    if (!write_message(frame, sizeof(frame))) {
      report_error("SSD1306 frame write");
    }
  }
}

void oledInit(void) { write_framebuffer(); }

void oledRefresh(void) { write_framebuffer(); }

void emulatorPoll(void) {}
