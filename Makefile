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
	@grep -q 'receive_fd_bundle(UGSP_PREBIND_RESOURCES, 6' platform/raspberry-pi/usb_functionfs.c
	@grep -q 'GPIO_V2_LINE_GET_VALUES_IOCTL' platform/raspberry-pi/buttons_gpio.c
	@grep -q 'workerButtonLinesFd' platform/raspberry-pi/buttons_gpio.c
	@grep -q 'display_backends_create' platform/raspberry-pi/display_linux.c
	@grep -q 'DISPLAY_BACKENDS_MONO1_MSB_REVERSE_PAGE' platform/raspberry-pi/display_linux.c
	@grep -q 'display_backends_write_frame' platform/raspberry-pi/display_linux.c
	@grep -q 'DISPLAY_ST7789_SPI' platform/raspberry-pi/worker_config.h
	@grep -q 'libdisplay_backends.a' mk/worker-firmware.mk
	@test -x tools/protoc
	@! grep -R -q '#include <SDL' platform/raspberry-pi/buttons_gpio.c \
		platform/raspberry-pi/display_linux.c
	@! grep -q 'platform_sdl\|emulator/oled.c' mk/worker-firmware.mk
	@echo "Worker boundary replaces UDP, SDL display, and SDL buttons."

check-platform:
	@mkdir -p build/tests
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic \
		-Iplatform/raspberry-pi \
		platform/raspberry-pi/worker_config.c \
		tests/test_platform_config.c \
		-o build/tests/test_platform_config
	@build/tests/test_platform_config

upstream-baseline: check-upstream
	./scripts/build-upstream-baseline.sh

worker: check
	./scripts/build-pi-worker.sh
