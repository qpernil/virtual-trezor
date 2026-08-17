# Overlay for the upstream legacy firmware Makefile.
#
# Run from upstream/trezor-firmware/legacy/firmware. The included upstream
# Makefile still selects and builds every genuine firmware object; this one
# explicit rule changes only the source used to produce its expected udp.o.

WORKER_OVERLAY := $(lastword $(MAKEFILE_LIST))
PROJECT_ROOT := $(abspath $(dir $(WORKER_OVERLAY))/..)

include Makefile

udp.o: $(PROJECT_ROOT)/platform/raspberry-pi/usb_functionfs.c
	@printf "  CC      %s (FunctionFS replacement)\n" "$@"
	$(Q)$(CC) $(CFLAGS) -I. -MMD -MP -o $@ -c $<
