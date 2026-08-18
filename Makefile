SHELL := /usr/bin/env bash

.DEFAULT_GOAL := check

.PHONY: all check check-platform check-upstream check-worker-boundary init \
	init-baseline upstream-baseline worker

all: check

init:
	./scripts/init-upstream.sh

init-baseline: init
	git -C upstream/trezor-firmware submodule update --init vendor/libopencm3

check: check-upstream check-worker-boundary check-platform

check-upstream:
	./scripts/check-upstream.sh

check-worker-boundary:
	@grep -q 'legacy/firmware/udp.c' mk/worker-sources.mk
	@grep -q 'legacy/emulator/udp.c' mk/worker-sources.mk
	@grep -q 'legacy/emulator/oled.c' mk/worker-sources.mk
	@grep -q 'USB_GADGET_FUNCTIONFS_TREZOR' platform/raspberry-pi/usb_functionfs.c
	@grep -q 'SDL_MOUSEBUTTONUP' platform/raspberry-pi/buttons_sdl.c
	@grep -q 'USB_GADGET_RESOURCE_DISPLAY_I2C_FD' platform/raspberry-pi/display_i2c_sdl.c
	@grep -q 'oledInit=sdlOledInit' mk/worker-firmware.mk
	@echo "Worker boundary excludes UDP and retains SDL with an optional I2C mirror."

check-platform:
	@mkdir -p build/tests
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic \
		-Iplatform/raspberry-pi \
		platform/raspberry-pi/ssd1306_stream.c \
		tests/test_ssd1306_stream.c \
		-o build/tests/test_ssd1306_stream
	@build/tests/test_ssd1306_stream

upstream-baseline: check-upstream
	./scripts/build-upstream-baseline.sh

worker: check
	./scripts/build-pi-worker.sh
