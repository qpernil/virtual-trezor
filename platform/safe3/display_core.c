/* SPDX-License-Identifier: GPL-3.0-or-later */

/* Safe 3 Core framebuffer adapter. Pixel conversion belongs to the selected
 * display backend; Core's Mono8 bytes cross this boundary unchanged. */

#include <trezor_bsp.h>
#include <trezor_rtl.h>

#include <io/display.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "display_backends.h"
#include "display_resources.h"

#define SAFE3_FRAME_WIDTH 128U
#define SAFE3_FRAME_HEIGHT 64U
#define SAFE3_FRAME_STRIDE SAFE3_FRAME_WIDTH
#define SAFE3_FRAME_LENGTH (SAFE3_FRAME_STRIDE * SAFE3_FRAME_HEIGHT)

typedef struct {
  bool initialized;
  bool shutdown_registered;
  int orientation_angle;
  uint8_t backlight_level;
  uint8_t framebuffer[SAFE3_FRAME_LENGTH];
  DisplayBackendsHandle *display;
} safe3_display_driver_t;

static safe3_display_driver_t g_display;

static void display_shutdown_at_exit(void) {
  display_deinit(DISPLAY_RESET_CONTENT);
}

bool display_init(display_content_mode_t mode) {
  if (g_display.initialized) {
    return true;
  }
  if (!g_display.shutdown_registered) {
    if (atexit(display_shutdown_at_exit) != 0) {
      fputs("virtual-trezor-safe3: cannot register display shutdown\n", stderr);
      return false;
    }
    g_display.shutdown_registered = true;
  }
  if (mode == DISPLAY_RESET_CONTENT) {
    memset(g_display.framebuffer, 0, sizeof(g_display.framebuffer));
  }
  g_display.orientation_angle = 0;
#if USE_BACKLIGHT
  g_display.backlight_level = 0;
#else
  g_display.backlight_level = 255;
#endif
  gfx_bitblt_init();

  safe3_display_resources_t resources;
  if (!safe3_display_resources_acquire(&resources)) {
    gfx_bitblt_deinit();
    return false;
  }
  int error = display_backends_create(DISPLAY_BACKENDS_ST7789_SPI,
                                      resources.bus_fd, resources.control_fd,
                                      &g_display.display);
  safe3_display_resources_release(&resources);
  if (error != 0) {
    fprintf(stderr, "virtual-trezor-safe3: display initialization failed: %s\n",
            strerror(error));
    gfx_bitblt_deinit();
    return false;
  }

  g_display.initialized = true;
  return true;
}

void display_deinit(display_content_mode_t mode) {
  if (!g_display.initialized) {
    return;
  }
  int error = display_backends_shutdown(g_display.display);
  if (error != 0) {
    fprintf(stderr, "virtual-trezor-safe3: display shutdown failed: %s\n",
            strerror(error));
  } else {
    fputs("virtual-trezor-safe3: display shut down\n", stderr);
  }
  display_backends_destroy(g_display.display);
  g_display.display = NULL;
  gfx_bitblt_deinit();
  g_display.initialized = false;
  if (mode == DISPLAY_RESET_CONTENT) {
    memset(g_display.framebuffer, 0, sizeof(g_display.framebuffer));
  }
}

bool display_set_backlight(uint8_t level) {
  if (!g_display.initialized) {
    return false;
  }
#if !USE_BACKLIGHT
  level = 255;
#endif
  if (g_display.backlight_level != level) {
    g_display.backlight_level = level;
    display_refresh();
  }
  return true;
}

uint8_t display_get_backlight(void) {
  return g_display.initialized ? g_display.backlight_level : 0;
}

int display_set_orientation(int angle) {
  if (!g_display.initialized) {
    return 0;
  }
  if (angle == 0 || angle == 180) {
    g_display.orientation_angle = angle;
  }
  return g_display.orientation_angle;
}

int display_get_orientation(void) {
  return g_display.initialized ? g_display.orientation_angle : 0;
}

bool display_get_frame_buffer(display_fb_info_t *fb) {
  memset(fb, 0, sizeof(*fb));
  if (!g_display.initialized) {
    return false;
  }
  fb->ptr = g_display.framebuffer;
  fb->stride = SAFE3_FRAME_STRIDE;
  fb->size = sizeof(g_display.framebuffer);
  return true;
}

void display_refresh(void) {
  if (!g_display.initialized) {
    return;
  }
  int error = display_backends_write_frame(
      g_display.display, DISPLAY_BACKENDS_MONO8, SAFE3_FRAME_WIDTH,
      SAFE3_FRAME_HEIGHT, SAFE3_FRAME_STRIDE, g_display.framebuffer,
      sizeof(g_display.framebuffer));
  if (error != 0) {
    fprintf(stderr, "virtual-trezor-safe3: display frame failed: %s\n",
            strerror(error));
  }
}

void display_fill(const gfx_bitblt_t *bb) {
  if (!g_display.initialized) {
    return;
  }
  gfx_bitblt_t copy = *bb;
  copy.dst_row = g_display.framebuffer + SAFE3_FRAME_STRIDE * copy.dst_y;
  copy.dst_stride = SAFE3_FRAME_STRIDE;
  gfx_mono8_fill(&copy);
}

void display_copy_mono1p(const gfx_bitblt_t *bb) {
  if (!g_display.initialized) {
    return;
  }
  gfx_bitblt_t copy = *bb;
  copy.dst_row = g_display.framebuffer + SAFE3_FRAME_STRIDE * copy.dst_y;
  copy.dst_stride = SAFE3_FRAME_STRIDE;
  gfx_mono8_copy_mono1p(&copy);
}

void display_save(const char *prefix) { (void)prefix; }

void display_clear_save(void) {}
