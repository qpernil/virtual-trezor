/* SPDX-License-Identifier: GPL-3.0-or-later */

/* I2C and SPI display backends for the Raspberry Pi worker. */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/gpio.h>
#include <linux/i2c-dev.h>
#include <linux/spi/spidev.h>
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
#define DISPLAY_SPI_FD_ENV "USB_GADGET_RESOURCE_DISPLAY_SPI_FD"
#define DISPLAY_GPIO_FD_ENV "USB_GADGET_RESOURCE_DISPLAY_GPIO_FD"
#define SH1106_DC_GPIO 24
#define SH1106_RESET_GPIO 25
#define SH1106_SPI_SPEED_HZ 4000000U
#define RETRY_DELAY_MS 1000

static int display_fd = -1;
static bool display_ready = false;
static bool error_reported = false;
static uint64_t retry_after_ms = 0;
static int dc_line_fd = -1;
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
            "virtual-trezor: %s display %s failed: %s; retrying\n",
            worker_display_backend_name(), operation, strerror(errno));
    error_reported = true;
  }
  display_ready = false;
  retry_after_ms = monotonic_ms() + RETRY_DELAY_MS;
}

static bool write_bytes(const uint8_t *message, size_t length) {
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

static bool set_output_line(int line_fd, bool high) {
  struct gpio_v2_line_values values = {
      .bits = high ? 1 : 0,
      .mask = 1,
  };
  return ioctl(line_fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &values) == 0;
}

static int request_output_line(unsigned int offset, const char *consumer) {
  int gpiochip_fd = required_fd_from_environment(DISPLAY_GPIO_FD_ENV);
  struct gpio_v2_line_request request = {
      .offsets = {offset},
      .config.flags = GPIO_V2_LINE_FLAG_OUTPUT,
      .num_lines = 1,
  };
  snprintf(request.consumer, sizeof(request.consumer), "%s", consumer);
  if (ioctl(gpiochip_fd, GPIO_V2_GET_LINE_IOCTL, &request) != 0) {
    return -1;
  }
  return request.fd;
}

static void pulse_sh1106_reset(void) {
  if (!reset_resource_checked) {
    reset_resource_checked = true;
    reset_line_fd = request_output_line(SH1106_RESET_GPIO,
                                        "virtual-trezor-reset");
    if (reset_line_fd < 0) {
      fprintf(stderr,
              "virtual-trezor: cannot request SH1106 reset GPIO %d: %s; "
              "relying on power-on reset\n",
              SH1106_RESET_GPIO, strerror(errno));
    }
  }

  if (reset_line_fd < 0) {
    return;
  }
  if (!set_output_line(reset_line_fd, true)) {
    fprintf(stderr, "virtual-trezor: cannot drive SH1106 reset GPIO: %s\n",
            strerror(errno));
    close(reset_line_fd);
    reset_line_fd = -1;
    return;
  }
  usleep(100000);
  set_output_line(reset_line_fd, false);
  usleep(100000);
  set_output_line(reset_line_fd, true);
  usleep(100000);
}

static bool initialize_spi(void) {
  uint8_t mode = SPI_MODE_0;
  uint8_t bits_per_word = 8;
  uint32_t speed_hz = SH1106_SPI_SPEED_HZ;
  if (ioctl(display_fd, SPI_IOC_WR_MODE, &mode) != 0 ||
      ioctl(display_fd, SPI_IOC_WR_BITS_PER_WORD, &bits_per_word) != 0 ||
      ioctl(display_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed_hz) != 0) {
    return false;
  }
  if (dc_line_fd < 0) {
    dc_line_fd =
        request_output_line(SH1106_DC_GPIO, "virtual-trezor-display-dc");
    if (dc_line_fd < 0) {
      return false;
    }
  }
  return set_output_line(dc_line_fd, false);
}

static bool write_sh1106_message(const uint8_t *message, size_t length) {
  if (!worker_display_uses_spi()) {
    return write_bytes(message, length);
  }
  if (length < 2 || (message[0] != 0x00 && message[0] != 0x40)) {
    errno = EINVAL;
    return false;
  }
  if (!set_output_line(dc_line_fd, message[0] == 0x40)) {
    return false;
  }
  return write_bytes(message + 1, length - 1);
}

static bool send_initialization(void) {
  size_t length = 0;
  const uint8_t *message;
  if (worker_display_is_sh1106()) {
    pulse_sh1106_reset();
    message = sh1106_init_message(&length);
    if (!write_sh1106_message(message, length)) {
      return false;
    }
    usleep(100000);
    message = sh1106_display_on_message(&length);
    return write_sh1106_message(message, length);
  } else {
    message = ssd1306_init_message(&length);
    return write_bytes(message, length);
  }
}

static bool initialize_display(void) {
  if (display_fd < 0) {
    display_fd = required_fd_from_environment(
        worker_display_uses_spi() ? DISPLAY_SPI_FD_ENV : DISPLAY_I2C_FD_ENV);
  }
  uint64_t now = monotonic_ms();
  if (retry_after_ms != 0 && now < retry_after_ms) {
    return false;
  }

  if (worker_display_uses_spi()) {
    if (!initialize_spi()) {
      report_error("SPI configuration");
      return false;
    }
  } else {
    uint8_t address = worker_display_is_sh1106() ? SH1106_I2C_ADDRESS
                                                 : SSD1306_I2C_ADDRESS;
    if (ioctl(display_fd, I2C_SLAVE, address) < 0) {
      report_error("I2C address selection");
      return false;
    }
  }
  if (!send_initialization()) {
    report_error("initialization");
    return false;
  }

  if (worker_display_uses_spi()) {
    fprintf(stderr, "virtual-trezor: %s display %s at %u Hz\n",
            worker_display_backend_name(),
            error_reported ? "recovered" : "ready", SH1106_SPI_SPEED_HZ);
  } else {
    uint8_t address = worker_display_is_sh1106() ? SH1106_I2C_ADDRESS
                                                 : SSD1306_I2C_ADDRESS;
    fprintf(stderr, "virtual-trezor: %s display %s at address 0x%02x\n",
            worker_display_backend_name(),
            error_reported ? "recovered" : "ready", address);
  }
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
  if (worker_display_is_sh1106()) {
    uint8_t command[SH1106_PAGE_COMMAND_SIZE];
    uint8_t data[SH1106_PAGE_DATA_SIZE];
    for (uint8_t page = 0; page < SH1106_PAGE_COUNT; ++page) {
      sh1106_build_page_command(command, page);
      sh1106_build_page_data(data, framebuffer, page);
      if (!write_sh1106_message(command, sizeof(command)) ||
          !write_sh1106_message(data, sizeof(data))) {
        report_error("SH1106 page write");
        return;
      }
    }
  } else {
    size_t address_length = 0;
    const uint8_t *address = ssd1306_address_message(&address_length);
    if (!write_bytes(address, address_length)) {
      report_error("SSD1306 address-window write");
      return;
    }
    uint8_t frame[SSD1306_FRAME_MESSAGE_SIZE];
    ssd1306_build_frame_message(frame, framebuffer);
    if (!write_bytes(frame, sizeof(frame))) {
      report_error("SSD1306 frame write");
    }
  }
}

void oledInit(void) { write_framebuffer(); }

void oledRefresh(void) { write_framebuffer(); }

void emulatorPoll(void) {
  if (display_ready || retry_after_ms == 0) {
    return;
  }
  if (monotonic_ms() < retry_after_ms) {
    return;
  }
  write_framebuffer();
}
