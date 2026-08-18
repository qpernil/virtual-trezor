# Architecture

## Process boundary

`usb-gadget-supervisor` creates the USB gadget, mounts FunctionFS, opens any
declared I2C/GPIO resources, drops privileges, and launches this worker. The
worker publishes its FunctionFS descriptors and owns all USB endpoint traffic.
The supervisor does not interpret Trezor messages or proxy secret material.

A Pi currently provides one usable USB device controller. The supervisor can
therefore select a Virtual Trezor or Virtual YubiKey profile, but it cannot
make them two independent USB devices simultaneously through that controller.
Two concurrently enumerated device identities require two Pis or independent
device controllers.

## Firmware boundary

The Trezor One firmware already separates most UI composition from physical
display refresh:

- `legacy/oled.c` owns the 128x64, 1024-byte framebuffer and drawing
  primitives.
- A physical Trezor One uses the non-emulator `oledInit` and `oledRefresh`
  implementations in that file to drive its OLED over STM32 SPI with separate
  CS, data/command, and reset GPIOs.
- For an emulator build, those SPI implementations are omitted by
  `#if !EMULATOR`.
- `legacy/emulator/oled.c` supplies those two functions with a desktop
  renderer.

The first Pi port retains the unmodified `legacy/emulator/oled.c` SDL renderer,
but compiles its three entry points under private `sdl*` names. The project
wrapper supplies the public `oledInit`, `oledRefresh`, and `emulatorPoll`
symbols. It calls SDL and optionally mirrors the existing framebuffer to an
SSD1306-compatible I2C stream, so the proven mouse/keyboard controls remain
available. The final backend can replace that wrapper without SDL. I2C is a Pi
platform choice, not a claim that the original Trezor One display bus is I2C.

Buttons have the same useful split. `legacy/buttons.c` continues to own
`buttonUpdate` and the real debounce/state transitions. The current Pi port
supplies only `buttonRead` through an SDL adapter: arrows remain available,
while clicking the left or right half of the display acts as No or Yes. The
hardware-UI port later replaces that adapter with a GPIO `buttonRead`.

USB has two emulator-specific layers that must both be excluded:

- `legacy/firmware/udp.c` implements the firmware-facing `usbInit`,
  `usbPoll`, `waitAndProcessUSBRequests`, `usbTiny`, and `usbFlush`.
- `legacy/emulator/udp.c` implements the localhost datagram sockets beneath
  those calls.

The current Pi port replaces the firmware-facing layer directly with
FunctionFS endpoint I/O and adds the supervisor lifecycle connection. It does
not reproduce the socket abstraction. Emulator flash, timer, randomness, and
SDL display remain in place for this milestone.

`mk/worker-firmware.mk` includes the genuine upstream firmware Makefile and
supplies explicit rules for the project platform objects. The expected `udp.o`
is compiled from `platform/raspberry-pi/usb_functionfs.c`. The derived emulator
support archive has its `udp.o`, `buttons.o`, and `oled.o` members removed. The
same unmodified SDL OLED source is then compiled under private symbol names and
linked beside the project display wrapper. Neither upstream datagram
implementation is present in the worker.

## Milestones

1. **Complete:** build and run the unmodified upstream SDL/UDP emulator as a
   baseline.
2. **In progress:** build the Pi worker with SDL UI and FunctionFS USB. Real
   USB enumeration, `Features`, multi-packet protocol traffic, reconnects, and
   interactive confirmation are proven. Full Suite workflows and the separate
   U2F HID interface remain.
3. **In progress:** the genuine framebuffer is mirrored to an
   SSD1306-compatible I2C stream while retaining SDL display/buttons. Unit,
   full-worker, two-target electrical, and 400 kHz oscilloscope validation pass
   with zero receive loss. SSD1306 interpretation and second-Pi rendering
   remain.
4. **Pending:** replace SDL with the physical OLED and GPIO implementation.

The detailed display stages and the Pi 4 controller-clock finding are in
[`i2c-display-plan.md`](i2c-display-plan.md).

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
