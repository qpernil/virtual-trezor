# Overlay for the upstream legacy firmware Makefile.
#
# Run from upstream/trezor-firmware/legacy/firmware. The included upstream
# Makefile still selects and builds every genuine firmware object; this one
# explicit rules replace the USB transport and wrap the SDL display with the
# optional I2C mirror without changing the upstream checkout.

WORKER_OVERLAY := $(lastword $(MAKEFILE_LIST))
PROJECT_ROOT := $(abspath $(dir $(WORKER_OVERLAY))/..)

include Makefile

CFLAGS += -pthread
LDFLAGS += -pthread
OBJS += platform_buttons.o platform_display.o platform_sdl_oled.o \
	platform_ssd1306_stream.o

$(NAME).elf: platform_buttons.o platform_display.o platform_sdl_oled.o \
	platform_ssd1306_stream.o

udp.o: $(PROJECT_ROOT)/platform/raspberry-pi/usb_functionfs.c
	@printf "  CC      %s (FunctionFS replacement)\n" "$@"
	$(Q)$(CC) $(CFLAGS) -I. -MMD -MP -o $@ -c $<

platform_buttons.o: $(PROJECT_ROOT)/platform/raspberry-pi/buttons_sdl.c
	@printf "  CC      %s (SDL buttons replacement)\n" "$@"
	$(Q)$(CC) $(CFLAGS) -I. -MMD -MP -o $@ -c $<

platform_display.o: $(PROJECT_ROOT)/platform/raspberry-pi/display_i2c_sdl.c
	@printf "  CC      %s (SDL and I2C display wrapper)\n" "$@"
	$(Q)$(CC) $(CFLAGS) -I. -I$(PROJECT_ROOT)/platform/raspberry-pi \
		-MMD -MP -o $@ -c $<

platform_sdl_oled.o: $(PROJECT_ROOT)/upstream/trezor-firmware/legacy/emulator/oled.c
	@printf "  CC      %s (renamed upstream SDL renderer)\n" "$@"
	$(Q)$(CC) $(CFLAGS) -I. \
		-DoledInit=sdlOledInit \
		-DoledRefresh=sdlOledRefresh \
		-DemulatorPoll=sdlEmulatorPoll \
		-MMD -MP -o $@ -c $<

platform_ssd1306_stream.o: $(PROJECT_ROOT)/platform/raspberry-pi/ssd1306_stream.c
	@printf "  CC      %s (SSD1306 stream)\n" "$@"
	$(Q)$(CC) $(CFLAGS) -I$(PROJECT_ROOT)/platform/raspberry-pi \
		-MMD -MP -o $@ -c $<
