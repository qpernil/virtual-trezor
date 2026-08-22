# Overlay for the upstream legacy firmware Makefile.
#
# Run from upstream/trezor-firmware/legacy/firmware. The included upstream
# Makefile still selects and builds every genuine firmware object; these
# explicit rules replace USB, display, and buttons without changing upstream.

WORKER_OVERLAY := $(lastword $(MAKEFILE_LIST))
PROJECT_ROOT := $(abspath $(dir $(WORKER_OVERLAY))/..)
DISPLAY_BACKENDS_DIR ?= $(abspath $(PROJECT_ROOT)/../display-backends)
DISPLAY_BACKENDS_LIB := $(DISPLAY_BACKENDS_DIR)/target/release/libdisplay_backends.a
USB_GADGET_SUPERVISOR_DIR ?= $(abspath $(PROJECT_ROOT)/../usb-gadget-supervisor)
USB_GADGET_WORKER_LIB := $(USB_GADGET_SUPERVISOR_DIR)/target/release/libusb_gadget_worker.a

include Makefile

SDL_CFLAGS := $(shell pkg-config --cflags sdl2 SDL2_image 2>/dev/null)
SDL_LDLIBS := $(shell pkg-config --libs sdl2 SDL2_image 2>/dev/null)
CFLAGS := $(filter-out $(SDL_CFLAGS),$(CFLAGS))
LDLIBS := $(filter-out $(SDL_LDLIBS),$(LDLIBS))
CFLAGS += -I$(DISPLAY_BACKENDS_DIR)/include
LDLIBS += $(DISPLAY_BACKENDS_LIB) $(USB_GADGET_WORKER_LIB) -ldl -lm
OBJS += platform_buttons.o platform_display.o platform_main.o \
	platform_worker_config.o firmware_usb.o libopencm3_usb.o \
	libopencm3_usb_control.o

$(NAME).elf: platform_buttons.o platform_display.o platform_main.o \
	platform_worker_config.o firmware_usb.o libopencm3_usb.o \
	libopencm3_usb_control.o $(DISPLAY_BACKENDS_LIB) \
	$(USB_GADGET_WORKER_LIB)

trezor.o: trezor.c
	@printf "  CC      %s (renamed upstream entry point)\n" "$@"
	$(Q)$(CC) $(CFLAGS) -Dmain=trezorFirmwareMain -MMD -MP -o $@ -c $<

udp.o: $(PROJECT_ROOT)/platform/raspberry-pi/usb_functionfs.c
	@printf "  CC      %s (virtual USB controller)\n" "$@"
	$(Q)$(CC) $(CFLAGS) -I. -I$(USB_GADGET_SUPERVISOR_DIR)/worker \
		-MMD -MP -o $@ -c $<

firmware_usb.o: usb.c
	@printf "  CC      %s (genuine legacy USB implementation)\n" "$@"
	$(Q)$(CC) $(CFLAGS) \
		-DusbInit=firmwareUsbInit -DusbPoll=firmwareUsbPoll \
		-DusbReconnect=firmwareUsbReconnect -DusbTiny=firmwareUsbTiny \
		-DwaitAndProcessUSBRequests=firmwareWaitAndProcessUSBRequests \
		-DusbFlush=firmwareUsbFlush -MMD -MP -o $@ -c $<

libopencm3_usb.o: ../vendor/libopencm3/lib/usb/usb.c
	@printf "  CC      %s (libopencm3 USB core)\n" "$@"
	$(Q)$(CC) $(CFLAGS) -MMD -MP -o $@ -c $<

libopencm3_usb_control.o: ../vendor/libopencm3/lib/usb/usb_control.c
	@printf "  CC      %s (libopencm3 EP0 state machine)\n" "$@"
	$(Q)$(CC) $(CFLAGS) -MMD -MP -o $@ -c $<

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
