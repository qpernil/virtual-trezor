/* SPDX-License-Identifier: GPL-3.0-or-later */

/* I2C and SPI display backends for the Raspberry Pi worker. */

#include <errno.h>
#include <linux/gpio.h>
#include <linux/i2c-dev.h>
#include <linux/spi/spidev.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "oled.h"
#include "sh1106_stream.h"
#include "ssd1306_stream.h"
#include "st7789.h"
#include "usb_functionfs.h"
#include "worker_config.h"

#define SH1106_DC_GPIO 24
#define SH1106_RESET_GPIO 25
#define SH1106_SPI_SPEED_HZ 4000000U
#define ST7789_DC_GPIO 25
#define ST7789_RESET_GPIO 27
#define ST7789_BACKLIGHT_GPIO 24
#define ST7789_SPI_SPEED_HZ 62500000U
#define SPI_WRITE_CHUNK_SIZE 4096
#define RETRY_DELAY_MS 1000

static int display_fd = -1;
static bool display_ready = false;
static bool error_reported = false;
static uint64_t retry_after_ms = 0;
static int dc_line_fd = -1;
static int reset_line_fd = -1;
static int backlight_line_fd = -1;
static bool reset_resource_checked = false;
static uint8_t st7789_frame[ST7789_RENDER_BUFFER_SIZE];

static uint64_t monotonic_ms(void) {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
    return 0;
  }
  return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

static void report_error(const char *operation) {
  if (!error_reported) {
    fprintf(stderr, "virtual-trezor: %s display %s failed: %s; retrying\n",
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

static bool write_bytes_chunked(const uint8_t *message, size_t length) {
  while (length > 0) {
    size_t chunk =
        length < SPI_WRITE_CHUNK_SIZE ? length : SPI_WRITE_CHUNK_SIZE;
    if (!write_bytes(message, chunk)) {
      return false;
    }
    message += chunk;
    length -= chunk;
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
  int gpiochip_fd = workerGpioResourceFd();
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

static unsigned int display_dc_gpio(void) {
  return worker_display_is_st7789() ? ST7789_DC_GPIO : SH1106_DC_GPIO;
}

static unsigned int display_reset_gpio(void) {
  return worker_display_is_st7789() ? ST7789_RESET_GPIO : SH1106_RESET_GPIO;
}

static uint32_t display_spi_speed_hz(void) {
  return worker_display_is_st7789() ? ST7789_SPI_SPEED_HZ : SH1106_SPI_SPEED_HZ;
}

static void pulse_display_reset(void) {
  unsigned int reset_gpio = display_reset_gpio();
  if (!reset_resource_checked) {
    reset_resource_checked = true;
    reset_line_fd = request_output_line(reset_gpio, "virtual-trezor-reset");
    if (reset_line_fd < 0) {
      fprintf(stderr,
              "virtual-trezor: cannot request %s reset GPIO %u: %s; "
              "relying on power-on reset\n",
              worker_display_backend_name(), reset_gpio, strerror(errno));
    }
  }

  if (reset_line_fd < 0) {
    return;
  }
  if (!set_output_line(reset_line_fd, true)) {
    fprintf(stderr, "virtual-trezor: cannot drive %s reset GPIO: %s\n",
            worker_display_backend_name(), strerror(errno));
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
  uint32_t speed_hz = display_spi_speed_hz();
  if (ioctl(display_fd, SPI_IOC_WR_MODE, &mode) != 0 ||
      ioctl(display_fd, SPI_IOC_WR_BITS_PER_WORD, &bits_per_word) != 0 ||
      ioctl(display_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed_hz) != 0) {
    return false;
  }
  if (dc_line_fd < 0) {
    dc_line_fd =
        request_output_line(display_dc_gpio(), "virtual-trezor-display-dc");
    if (dc_line_fd < 0) {
      return false;
    }
  }
  if (worker_display_is_st7789() && backlight_line_fd < 0) {
    backlight_line_fd =
        request_output_line(ST7789_BACKLIGHT_GPIO, "virtual-trezor-backlight");
    if (backlight_line_fd < 0) {
      return false;
    }
  }
  return set_output_line(dc_line_fd, false);
}

static bool write_spi_message(bool data, const uint8_t *message,
                              size_t length) {
  if (!set_output_line(dc_line_fd, data)) {
    return false;
  }
  return write_bytes_chunked(message, length);
}

static bool write_sh1106_message(const uint8_t *message, size_t length) {
  if (!worker_display_uses_spi()) {
    return write_bytes(message, length);
  }
  if (length < 2 || (message[0] != 0x00 && message[0] != 0x40)) {
    errno = EINVAL;
    return false;
  }
  return write_spi_message(message[0] == 0x40, message + 1, length - 1);
}

static bool write_st7789_command(uint8_t command) {
  return write_spi_message(false, &command, 1);
}

static bool write_st7789_data(const uint8_t *data, size_t length) {
  return length == 0 || write_spi_message(true, data, length);
}

static bool set_st7789_window(uint16_t x, uint16_t y, uint16_t width,
                              uint16_t height) {
  uint8_t column[4];
  uint8_t row[4];
  st7789_build_window_data(column, row, x, y, width, height);
  return write_st7789_command(0x2a) &&
         write_st7789_data(column, sizeof(column)) &&
         write_st7789_command(0x2b) && write_st7789_data(row, sizeof(row)) &&
         write_st7789_command(0x2c);
}

static bool clear_st7789(void) {
  static const uint8_t black[SPI_WRITE_CHUNK_SIZE] = {0};
  size_t remaining = ST7789_PANEL_WIDTH * ST7789_PANEL_HEIGHT * 2;
  if (!set_st7789_window(0, 0, ST7789_PANEL_WIDTH, ST7789_PANEL_HEIGHT) ||
      !set_output_line(dc_line_fd, true)) {
    return false;
  }
  while (remaining > 0) {
    size_t chunk = remaining < sizeof(black) ? remaining : sizeof(black);
    if (!write_bytes(black, chunk)) {
      return false;
    }
    remaining -= chunk;
  }
  return true;
}

static bool clear_oled(void) {
  static const uint8_t blank[SSD1306_FRAMEBUFFER_SIZE] = {0};

  if (worker_display_is_sh1106()) {
    uint8_t command[SH1106_PAGE_COMMAND_SIZE];
    uint8_t data[SH1106_PAGE_DATA_SIZE];
    for (uint8_t page = 0; page < SH1106_PAGE_COUNT; ++page) {
      sh1106_build_page_command(command, page);
      sh1106_build_page_data(data, blank, page);
      if (!write_sh1106_message(command, sizeof(command)) ||
          !write_sh1106_message(data, sizeof(data))) {
        return false;
      }
    }
    return true;
  }

  size_t address_length = 0;
  const uint8_t *address = ssd1306_address_message(&address_length);
  uint8_t frame[SSD1306_FRAME_MESSAGE_SIZE];
  ssd1306_build_frame_message(frame, blank);
  return write_bytes(address, address_length) &&
         write_bytes(frame, sizeof(frame));
}

static bool turn_display_off(void) {
  if (worker_display_is_st7789()) {
    bool command_sent = write_st7789_command(0x28);
    bool backlight_off =
        backlight_line_fd < 0 || set_output_line(backlight_line_fd, false);
    return command_sent && backlight_off;
  }

  static const uint8_t display_off[] = {0x00, 0xae};
  return worker_display_is_sh1106()
             ? write_sh1106_message(display_off, sizeof(display_off))
             : write_bytes(display_off, sizeof(display_off));
}

void worker_display_shutdown(void) {
  if (!display_ready) {
    return;
  }

  bool cleared = worker_display_is_st7789() ? clear_st7789() : clear_oled();
  bool powered_off = turn_display_off();
  if (!cleared || !powered_off) {
    fprintf(stderr, "virtual-trezor: %s display shutdown failed: %s\n",
            worker_display_backend_name(), strerror(errno));
  } else {
    fprintf(stderr, "virtual-trezor: %s display cleared\n",
            worker_display_backend_name());
  }
  display_ready = false;
}

static bool initialize_st7789(void) {
  pulse_display_reset();
  size_t count = 0;
  const st7789_init_step_t *steps = st7789_init_steps(&count);
  for (size_t index = 0; index < count; ++index) {
    const st7789_init_step_t *step = &steps[index];
    if (!write_st7789_command(step->command) ||
        !write_st7789_data(step->data, step->data_length)) {
      return false;
    }
    if (step->delay_ms != 0) {
      usleep(step->delay_ms * 1000);
    }
  }
  return clear_st7789() && set_output_line(backlight_line_fd, true);
}

static bool send_initialization(void) {
  size_t length = 0;
  const uint8_t *message;
  if (worker_display_is_st7789()) {
    return initialize_st7789();
  }
  if (worker_display_is_sh1106()) {
    pulse_display_reset();
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
    display_fd = workerDisplayResourceFd();
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
    uint8_t address =
        worker_display_is_sh1106() ? SH1106_I2C_ADDRESS : SSD1306_I2C_ADDRESS;
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
            error_reported ? "recovered" : "ready", display_spi_speed_hz());
  } else {
    uint8_t address =
        worker_display_is_sh1106() ? SH1106_I2C_ADDRESS : SSD1306_I2C_ADDRESS;
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
  if (worker_display_is_st7789()) {
    st7789_encode_legacy_frame(st7789_frame, framebuffer);
    if (!set_st7789_window(ST7789_RENDER_X, ST7789_RENDER_Y,
                           ST7789_RENDER_WIDTH, ST7789_RENDER_HEIGHT) ||
        !write_st7789_data(st7789_frame, sizeof(st7789_frame))) {
      report_error("ST7789 frame write");
    }
  } else if (worker_display_is_sh1106()) {
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
