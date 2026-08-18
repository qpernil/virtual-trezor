SHELL := /usr/bin/env bash

.DEFAULT_GOAL := check

.PHONY: all check check-platform check-upstream check-worker-boundary init \
	init-baseline upstream-baseline worker

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
	@grep -q 'USB_GADGET_RESOURCE_DISPLAY_I2C_FD' platform/raspberry-pi/display_i2c.c
	@! grep -R -q '#include <SDL' platform/raspberry-pi/buttons_gpio.c \
		platform/raspberry-pi/display_i2c.c
	@! grep -q 'platform_sdl\|emulator/oled.c' mk/worker-firmware.mk
	@echo "Worker boundary replaces UDP, SDL display, and SDL buttons."

check-platform:
	@mkdir -p build/tests
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic \
		-Iplatform/raspberry-pi \
		platform/raspberry-pi/worker_config.c \
		platform/raspberry-pi/sh1106_stream.c \
		platform/raspberry-pi/ssd1306_stream.c \
		tests/test_i2c_display_stream.c \
		-o build/tests/test_i2c_display_stream
	@build/tests/test_i2c_display_stream

upstream-baseline: check-upstream
	./scripts/build-upstream-baseline.sh

worker: check
	./scripts/build-pi-worker.sh
