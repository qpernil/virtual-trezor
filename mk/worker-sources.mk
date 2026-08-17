# Source boundary for the Raspberry Pi worker.
#
# The build will enumerate upstream logic explicitly instead of invoking the
# upstream EMULATOR target, because that target unconditionally selects UDP.
# The initial worker retains SDL UI and the other host-emulator facilities.
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

UPSTREAM_EMULATOR_TO_RETAIN_INITIAL := \
	legacy/emulator/oled.c \
	legacy/emulator/setup.c \
	legacy/emulator/memory.c \
	legacy/emulator/timer.c

PI_INITIAL_PLATFORM_TO_PROVIDE := \
	platform/raspberry-pi/usb_functionfs.c \
	platform/raspberry-pi/buttons_sdl.c

PI_I2C_MIRROR_TO_PROVIDE_NEXT := \
	platform/raspberry-pi/display_i2c_sdl.c

PI_HARDWARE_UI_TO_PROVIDE_LATER := \
	platform/raspberry-pi/display.c \
	platform/raspberry-pi/buttons.c
