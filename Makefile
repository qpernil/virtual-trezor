SHELL := /usr/bin/env bash

.DEFAULT_GOAL := check

.PHONY: all check check-platform check-upstream check-worker-boundary init \
	init-baseline st7789-test upstream-baseline worker

all: check

init:
	./scripts/init-upstream.sh

init-baseline: init

check: check-upstream check-worker-boundary check-platform

check-upstream:
	./scripts/check-upstream.sh

check-worker-boundary:
	@grep -q 'legacy/firmware/udp.c' mk/worker-sources.mk
	@grep -q 'legacy/emulator/udp.c' mk/worker-sources.mk
	@grep -q 'legacy/emulator/setup.c' mk/worker-sources.mk
	@grep -q 'USB_GADGET_FUNCTIONFS_TREZOR' platform/raspberry-pi/usb_functionfs.c
	@grep -q 'GPIO_V2_LINE_GET_VALUES_IOCTL' platform/raspberry-pi/buttons_gpio.c
	@grep -q 'BUTTON_CENTER_GPIO 13' platform/raspberry-pi/buttons_gpio.c
	@grep -q 'USB_GADGET_RESOURCE_DISPLAY_I2C_FD' platform/raspberry-pi/display_linux.c
	@grep -q 'USB_GADGET_RESOURCE_DISPLAY_SPI_FD' platform/raspberry-pi/display_linux.c
	@grep -q 'DISPLAY_ST7789_SPI' platform/raspberry-pi/worker_config.h
	@grep -q 'platform_st7789.o' mk/worker-firmware.mk
	@! grep -R -q '#include <SDL' platform/raspberry-pi/buttons_gpio.c \
		platform/raspberry-pi/display_linux.c
	@! grep -q 'platform_sdl\|emulator/oled.c' mk/worker-firmware.mk
	@echo "Worker boundary replaces UDP, SDL display, and SDL buttons."

check-platform:
	@mkdir -p build/tests
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic \
		-Iplatform/raspberry-pi \
		platform/raspberry-pi/worker_config.c \
		platform/raspberry-pi/sh1106_stream.c \
		platform/raspberry-pi/ssd1306_stream.c \
		platform/raspberry-pi/st7789.c \
		tests/test_display_stream.c \
		-o build/tests/test_display_stream
	@build/tests/test_display_stream

upstream-baseline: check-upstream
	./scripts/build-upstream-baseline.sh

worker: check
	./scripts/build-pi-worker.sh

st7789-test:
	@if [[ "$$(uname -s)" != Linux ]]; then \
		echo "The standalone ST7789 test requires Linux." >&2; exit 1; \
	fi
	@mkdir -p build
	$(CC) -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -pedantic \
		-Iplatform/raspberry-pi -Iupstream/trezor-firmware/legacy \
		platform/raspberry-pi/display_linux.c \
		platform/raspberry-pi/worker_config.c \
		platform/raspberry-pi/sh1106_stream.c \
		platform/raspberry-pi/ssd1306_stream.c \
		platform/raspberry-pi/st7789.c \
		tools/st7789-test.c -o build/st7789-test
