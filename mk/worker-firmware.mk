# Overlay for the upstream legacy firmware Makefile.
#
# Run from upstream/trezor-firmware/legacy/firmware. The included upstream
# Makefile still selects and builds every genuine firmware object; these
# explicit rules replace USB, display, and buttons without changing upstream.

WORKER_OVERLAY := $(lastword $(MAKEFILE_LIST))
PROJECT_ROOT := $(abspath $(dir $(WORKER_OVERLAY))/..)
DISPLAY_BACKENDS_DIR ?= $(abspath $(PROJECT_ROOT)/../display-backends)
DISPLAY_BACKENDS_LIB := $(DISPLAY_BACKENDS_DIR)/target/release/libdisplay_backends.a

include Makefile

SDL_CFLAGS := $(shell pkg-config --cflags sdl2 SDL2_image 2>/dev/null)
SDL_LDLIBS := $(shell pkg-config --libs sdl2 SDL2_image 2>/dev/null)
CFLAGS := $(filter-out $(SDL_CFLAGS),$(CFLAGS))
LDLIBS := $(filter-out $(SDL_LDLIBS),$(LDLIBS))
CFLAGS += -pthread
CFLAGS += -I$(DISPLAY_BACKENDS_DIR)/include
LDFLAGS += -pthread
LDLIBS += $(DISPLAY_BACKENDS_LIB) -ldl -lm
OBJS += platform_buttons.o platform_display.o platform_main.o \
	platform_worker_config.o

$(NAME).elf: platform_buttons.o platform_display.o platform_main.o \
	platform_worker_config.o $(DISPLAY_BACKENDS_LIB)

trezor.o: trezor.c
	@printf "  CC      %s (renamed upstream entry point)\n" "$@"
	$(Q)$(CC) $(CFLAGS) -Dmain=trezorFirmwareMain -MMD -MP -o $@ -c $<

udp.o: $(PROJECT_ROOT)/platform/raspberry-pi/usb_functionfs.c
	@printf "  CC      %s (FunctionFS replacement)\n" "$@"
	$(Q)$(CC) $(CFLAGS) -I. -MMD -MP -o $@ -c $<

platform_buttons.o: $(PROJECT_ROOT)/platform/raspberry-pi/buttons_gpio.c
	@printf "  CC      %s (GPIO buttons replacement)\n" "$@"
	$(Q)$(CC) $(CFLAGS) -I. -MMD -MP -o $@ -c $<

platform_display.o: $(PROJECT_ROOT)/platform/raspberry-pi/display_linux.c
	@printf "  CC      %s (Linux display replacement)\n" "$@"
	$(Q)$(CC) $(CFLAGS) -I. -I$(PROJECT_ROOT)/platform/raspberry-pi \
		-MMD -MP -o $@ -c $<

platform_main.o: $(PROJECT_ROOT)/platform/raspberry-pi/worker_main.c
	@printf "  CC      %s (worker entry point)\n" "$@"
	$(Q)$(CC) $(CFLAGS) -I$(PROJECT_ROOT)/platform/raspberry-pi \
		-MMD -MP -o $@ -c $<

platform_worker_config.o: $(PROJECT_ROOT)/platform/raspberry-pi/worker_config.c
	@printf "  CC      %s (worker configuration)\n" "$@"
	$(Q)$(CC) $(CFLAGS) -I$(PROJECT_ROOT)/platform/raspberry-pi \
		-MMD -MP -o $@ -c $<
