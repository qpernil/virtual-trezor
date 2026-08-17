SHELL := /usr/bin/env bash

.DEFAULT_GOAL := check

.PHONY: all check check-upstream check-worker-boundary init init-baseline \
	upstream-baseline worker

all: check

init:
	./scripts/init-upstream.sh

init-baseline: init
	git -C upstream/trezor-firmware submodule update --init vendor/libopencm3

check: check-upstream check-worker-boundary

check-upstream:
	./scripts/check-upstream.sh

check-worker-boundary:
	@grep -q 'legacy/firmware/udp.c' mk/worker-sources.mk
	@grep -q 'legacy/emulator/udp.c' mk/worker-sources.mk
	@grep -q 'legacy/emulator/oled.c' mk/worker-sources.mk
	@grep -q 'USB_GADGET_FUNCTIONFS_TREZOR' platform/raspberry-pi/usb_functionfs.c
	@grep -q 'SDL_MOUSEBUTTONUP' platform/raspberry-pi/buttons_sdl.c
	@echo "Initial worker boundary excludes UDP and retains SDL display support."

upstream-baseline: check-upstream
	./scripts/build-upstream-baseline.sh

worker: check
	./scripts/build-pi-worker.sh
