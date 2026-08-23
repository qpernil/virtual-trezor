SHELL := /usr/bin/env bash
USB_GADGET_SUPERVISOR_DIR ?= $(abspath ../usb-gadget-supervisor)

.DEFAULT_GOAL := check

.PHONY: all check check-platform check-upstream check-worker-boundary init \
	init-baseline safe3-baseline safe3-display upstream-baseline worker

all: check

init:
	./scripts/init-upstream.sh

init-baseline: init

check: check-upstream check-worker-boundary check-platform

check-upstream:
	./scripts/check-upstream.sh

check-worker-boundary:
	@grep -q 'TREZOR_MODEL=T3B1' mk/safe3-worker.mk
	@grep -q 'build/safe3-t3b1' mk/safe3-worker.mk
	@grep -q 'legacy/firmware/udp.c' mk/worker-sources.mk
	@grep -q 'legacy/emulator/udp.c' mk/worker-sources.mk
	@grep -q 'legacy/emulator/setup.c' mk/worker-sources.mk
	@grep -q 'KIND_CONFIGURE' platform/raspberry-pi/usb_functionfs.c
	@grep -q 'run_control_transfer' platform/raspberry-pi/usb_functionfs.c
	@grep -q 'KIND_USB_ENDPOINTS' platform/raspberry-pi/usb_functionfs.c
	@grep -q 'ugsp_discover_usb_personality' platform/raspberry-pi/usb_functionfs.c
	@grep -q 'GPIO_V2_LINE_GET_VALUES_IOCTL' platform/raspberry-pi/buttons_gpio.c
	@grep -q 'workerButtonLinesFd' platform/raspberry-pi/buttons_gpio.c
	@grep -q 'service_virtual_usb_until_resumed' \
		platform/raspberry-pi/usb_functionfs.c
	@grep -q 'worker_timer_suspend' platform/raspberry-pi/usb_functionfs.c
	@grep -q 'endpoint_thread_main' \
		platform/raspberry-pi/usb_functionfs.c
	@grep -q 'write(endpoint->functionfs_fd' \
		platform/raspberry-pi/usb_functionfs.c
	@grep -q 'timer_linux.c' scripts/build-pi-worker.sh
	@grep -q 'display_backends_create' platform/raspberry-pi/display_linux.c
	@grep -q 'DISPLAY_BACKENDS_MONO1_MSB_REVERSE_PAGE' platform/raspberry-pi/display_linux.c
	@grep -q 'DISPLAY_BACKENDS_MONO8' platform/safe3/display_core.c
	@grep -q 'display_backends_write_frame' platform/safe3/display_core.c
	@grep -q 'VIRTUAL_TREZOR_HEADLESS_DISPLAY' \
		patches/safe3-headless-display.patch
	@! grep -q '#include .*SDL' platform/safe3/button_headless.c \
		platform/safe3/display_core.c platform/safe3/display_resources.c
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
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic \
		-Iplatform/raspberry-pi \
		tests/test_platform_timer.c \
		-o build/tests/test_platform_timer
	@build/tests/test_platform_timer
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic \
		-I$(USB_GADGET_SUPERVISOR_DIR)/worker \
		tests/test_worker_protocol.c \
		-o build/tests/test_worker_protocol
	@build/tests/test_worker_protocol
	$(CC) -std=gnu11 -Wall -Wextra -Werror \
		-Iplatform/raspberry-pi \
		-Iupstream/trezor-firmware/legacy \
		-Iupstream/trezor-firmware/legacy/gen \
		-I../display-backends/include \
		platform/raspberry-pi/display_linux.c \
		tests/test_display_adapter.c \
		-o build/tests/test_display_adapter
	@build/tests/test_display_adapter
	$(CC) -std=gnu11 -Wall -Wextra -Werror -c \
		-DTREZOR_EMULATOR -DTREZOR_MODEL_T3B1 \
		-D'MODEL_HEADER="T3B1/model_T3B1.h"' \
		-D'VERSIONS_HEADER="T3B1/versions.h"' \
		-D'TREZOR_BOARD="T3B1/boards/t3b1-unix.h"' \
		-DKERNEL_MODE=1 -DFRAMEBUFFER -DDISPLAY_MONO \
		-DDISPLAY_RESX=128 -DDISPLAY_RESY=64 \
		-DSTM32U585xx -DMCU_TYPE=STM32U585xx \
		-Iupstream/trezor-firmware/core \
		-Iupstream/trezor-firmware/core/embed/rtl/inc \
		-Iupstream/trezor-firmware/core/embed/models \
		-Iupstream/trezor-firmware/core/embed/sys/bsp/inc \
		-Iupstream/trezor-firmware/core/embed/sys/inc \
		-Iupstream/trezor-firmware/core/embed/io/display/inc \
		-Iupstream/trezor-firmware/core/embed/io/gfx/inc \
		-Iplatform/raspberry-pi -I../display-backends/include \
		platform/safe3/display_core.c \
		-o build/tests/safe3_display_core.o
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic \
		-D_POSIX_C_SOURCE=200809L -Iplatform/safe3 \
		platform/safe3/display_resources.c \
		tests/test_safe3_display_resources.c \
		-o build/tests/test_safe3_display_resources
	@build/tests/test_safe3_display_resources
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -DEMULATOR=1 \
		-Iupstream/trezor-firmware/legacy \
		-Iplatform/raspberry-pi \
		-c platform/raspberry-pi/timer_linux.c \
		-o build/tests/timer_linux.o

upstream-baseline: check-upstream
	./scripts/build-upstream-baseline.sh

safe3-baseline: check-upstream
	$(MAKE) -f mk/safe3-worker.mk baseline

safe3-display: check-upstream
	$(MAKE) -f mk/safe3-worker.mk display

worker: check
	./scripts/build-pi-worker.sh
