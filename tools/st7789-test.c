/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "st7789.h"
#include "worker_config.h"

#define DISPLAY_SPI_FD_ENV "USB_GADGET_RESOURCE_DISPLAY_SPI_FD"
#define DISPLAY_GPIO_FD_ENV "USB_GADGET_RESOURCE_DISPLAY_GPIO_FD"

static uint8_t framebuffer[ST7789_SOURCE_FRAMEBUFFER_SIZE];

const uint8_t *oledGetBuffer(void) { return framebuffer; }
void oledInit(void);
void oledRefresh(void);

static void fail(const char *operation) {
  fprintf(stderr, "st7789-test: %s: %s\n", operation, strerror(errno));
  exit(1);
}

static void export_fd(const char *name, int fd) {
  char value[32];
  snprintf(value, sizeof(value), "%d", fd);
  if (setenv(name, value, 1) != 0) {
    fail("set environment");
  }
}

static void set_pixel(uint16_t x, uint16_t y) {
  size_t offset = ST7789_SOURCE_FRAMEBUFFER_SIZE - 1 - x -
                  (size_t)(y / 8) * ST7789_SOURCE_WIDTH;
  framebuffer[offset] |= (uint8_t)(1U << (7 - (y % 8)));
}

static void draw_test_pattern(void) {
  for (uint16_t y = 0; y < ST7789_SOURCE_HEIGHT; ++y) {
    for (uint16_t x = 0; x < ST7789_SOURCE_WIDTH; ++x) {
      bool border = x == 0 || y == 0 || x == ST7789_SOURCE_WIDTH - 1 ||
                    y == ST7789_SOURCE_HEIGHT - 1;
      bool diagonals = x == y * 2 || x == ST7789_SOURCE_WIDTH - 1 - y * 2;
      bool center = x == ST7789_SOURCE_WIDTH / 2 ||
                    y == ST7789_SOURCE_HEIGHT / 2;
      bool bars = y >= 48 && ((x / 8) % 2 == 0);
      if (border || diagonals || center || bars) {
        set_pixel(x, y);
      }
    }
  }
}

int main(int argc, char **argv) {
  const char *spi_path = argc > 1 ? argv[1] : "/dev/spidev0.0";
  const char *gpio_path = argc > 2 ? argv[2] : "/dev/gpiochip0";
  unsigned long frames = 0;
  if (argc > 3) {
    char *end = NULL;
    errno = 0;
    frames = strtoul(argv[3], &end, 10);
    if (errno != 0 || end == argv[3] || *end != '\0' || frames == 0) {
      fprintf(stderr, "st7789-test: frames must be a positive integer\n");
      return 2;
    }
  }
  if (argc > 4) {
    fprintf(stderr, "usage: %s [spidev [gpiochip [frames]]]\n", argv[0]);
    return 2;
  }

  int spi_fd = open(spi_path, O_RDWR | O_CLOEXEC);
  if (spi_fd < 0) {
    fail(spi_path);
  }
  int gpio_fd = open(gpio_path, O_RDWR | O_CLOEXEC);
  if (gpio_fd < 0) {
    fail(gpio_path);
  }
  export_fd(DISPLAY_SPI_FD_ENV, spi_fd);
  export_fd(DISPLAY_GPIO_FD_ENV, gpio_fd);

  char error[160];
  char *arguments[] = {"st7789-test", "--display=st7789-spi"};
  if (!worker_config_parse(2, arguments, error, sizeof(error))) {
    fprintf(stderr, "st7789-test: %s\n", error);
    return 1;
  }

  draw_test_pattern();
  oledInit();
  if (frames != 0) {
    struct timespec start;
    struct timespec end;
    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
      fail("read start time");
    }
    for (unsigned long frame = 0; frame < frames; ++frame) {
      oledRefresh();
    }
    if (clock_gettime(CLOCK_MONOTONIC, &end) != 0) {
      fail("read end time");
    }
    double elapsed_ms = (double)(end.tv_sec - start.tv_sec) * 1000.0 +
                        (double)(end.tv_nsec - start.tv_nsec) / 1000000.0;
    printf("%lu frames in %.1f ms: %.2f ms/frame, %.1f frames/s\n", frames,
           elapsed_ms, elapsed_ms / (double)frames,
           (double)frames * 1000.0 / elapsed_ms);
  }
  puts("ST7789 test pattern sent. Press Enter to release the display GPIOs.");
  (void)getchar();
  return 0;
}
