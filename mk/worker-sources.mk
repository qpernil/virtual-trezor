# Source boundary for the Raspberry Pi worker.
#
# The build will enumerate upstream logic explicitly instead of invoking the
# upstream EMULATOR target, because that target unconditionally selects UDP.
# The worker retains only the host flash, memory, and timer facilities.
# This manifest documents the implemented boundary. The actual build overlay is
# mk/worker-firmware.mk because the upstream Makefile already enumerates the
# complete firmware object set.

UPSTREAM_LOGIC_TO_RETAIN := \
	legacy/buttons.c \
	legacy/oled.c \
	legacy/firmware/messages.c \
	legacy/firmware/config.c \
	legacy/firmware/trezor.c

UPSTREAM_PLATFORM_TO_EXCLUDE := \
	legacy/firmware/udp.c \
	legacy/emulator/udp.c

UPSTREAM_EMULATOR_TO_RETAIN := \
	legacy/emulator/setup.c \
	legacy/emulator/memory.c \
	legacy/emulator/strl.c

PI_INITIAL_PLATFORM_TO_PROVIDE := \
	platform/raspberry-pi/usb_functionfs.c \
	platform/raspberry-pi/buttons_gpio.c \
	platform/raspberry-pi/display_linux.c \
	platform/raspberry-pi/timer_linux.c \
	platform/raspberry-pi/worker_main.c \
	platform/raspberry-pi/worker_config.c
