# Architecture

## Process boundary

`usb-gadget-supervisor` creates the USB gadget, mounts FunctionFS, opens any
declared I2C/GPIO resources, drops privileges, and launches this worker. The
worker publishes its FunctionFS descriptors and owns all USB endpoint traffic.
The supervisor does not interpret Trezor messages or proxy secret material.

## Firmware boundary

The Trezor One firmware already separates most UI composition from physical
display refresh:

- `legacy/oled.c` owns the 128x64 framebuffer and drawing primitives.
- For an emulator build, upstream omits its STM32 `oledInit` and
  `oledRefresh` implementations.
- `legacy/emulator/oled.c` supplies those two functions with a desktop
  renderer.

The first Pi port keeps `legacy/emulator/oled.c` and its SDL renderer. Once the
real-USB path works with normal host software, the hardware-UI port will keep
`legacy/oled.c` unchanged and replace only `oledInit` and `oledRefresh`. Its
refresh implementation writes the existing framebuffer to the I2C display.

Buttons have the same useful split. The first port keeps the upstream SDL
button implementation. The hardware-UI port later keeps `legacy/buttons.c`,
which owns `buttonUpdate` and state transitions, while supplying only
`buttonRead` from GPIO.

USB has two emulator-specific layers that must both be excluded:

- `legacy/firmware/udp.c` implements the firmware-facing `usbInit`,
  `usbPoll`, `waitAndProcessUSBRequests`, `usbTiny`, and `usbFlush`.
- `legacy/emulator/udp.c` implements the localhost datagram sockets beneath
  those calls.

The current Pi port replaces the firmware-facing layer directly with
FunctionFS endpoint I/O and adds the supervisor lifecycle connection. It does
not reproduce the socket abstraction. Emulator flash, timer, randomness, SDL
display, and SDL buttons remain in place for this milestone.

`mk/worker-firmware.mk` includes the genuine upstream firmware Makefile and
supplies one explicit rule for its expected `udp.o`. That object is compiled
from `platform/raspberry-pi/usb_functionfs.c`. The derived emulator support
archive has its own `udp.o` member removed before final linking, so neither
upstream datagram implementation is present in the worker.

## Milestones

1. **Complete:** build and run the unmodified upstream SDL/UDP emulator as a
   baseline.
2. **In progress:** build the Pi worker with SDL UI and FunctionFS USB. Real
   USB enumeration, `Features`, and multi-packet protocol traffic are proven;
   Trezor Suite and the separate U2F HID interface remain.
3. **Pending:** replace SDL display/buttons with the physical OLED and GPIO
   implementation.

## Upstream policy

The upstream submodule remains detached at a reviewed release commit. Project
code may select upstream sources, supply include paths and compiler definitions,
and provide linker symbols, but it must not modify files inside the submodule.

An upstream update is a single auditable gitlink change followed by:

1. the unmodified upstream baseline build;
2. worker compilation with the explicit source boundary;
3. framebuffer and button tests;
4. USB descriptor and protocol tests;
5. Raspberry Pi deployment validation.
