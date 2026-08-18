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

The Pi worker excludes `legacy/emulator/oled.c`. Project-owned `oledInit` and
`oledRefresh` implementations send the existing framebuffer directly to an
explicitly selected SSD1306 or SH1106 controller through an inherited I2C
descriptor. `emulatorPoll` remains a platform ABI symbol because the upstream
firmware loop calls it in emulator builds. In this implementation it retries a
failed display transfer after one second, reinitializing the controller and
retransmitting the current framebuffer independently of later UI changes. The
project-owned worker entry point parses the display selection before calling
the renamed, otherwise unmodified upstream firmware entry point. I2C is a Pi
platform choice, not a claim that the original Trezor One display bus is I2C.

Buttons have the same useful split. `legacy/buttons.c` continues to own
`buttonUpdate` and the real debounce/state transitions. The Pi port supplies
only `buttonRead`, sampling active-low GPIO5 and GPIO26 through the
supervisor-opened GPIO chip descriptor. The same two lines may be driven by
physical buttons or by the remote virtual-display process.

USB has two emulator-specific layers that must both be excluded:

- `legacy/firmware/udp.c` implements the firmware-facing `usbInit`,
  `usbPoll`, `waitAndProcessUSBRequests`, `usbTiny`, and `usbFlush`.
- `legacy/emulator/udp.c` implements the localhost datagram sockets beneath
  those calls.

The Pi port replaces the firmware-facing layer directly with FunctionFS
endpoint I/O and adds the supervisor lifecycle connection. It does not
reproduce the socket abstraction. Emulator flash, timer, and randomness remain
in place; desktop SDL does not.

`mk/worker-firmware.mk` includes the genuine upstream firmware Makefile and
supplies explicit rules for the project platform objects. The expected `udp.o`
is compiled from `platform/raspberry-pi/usb_functionfs.c`. The derived emulator
support archive is constructed only from `setup.o`, `memory.o`, `timer.o`, and
`strl.o`. Project objects provide USB, display, buttons, and worker entry/config
symbols. Neither upstream datagram implementation nor an SDL object is present
in the worker.

## Milestones

1. **Complete:** build and run the unmodified upstream SDL/UDP emulator as a
   baseline.
2. **In progress:** build the Pi worker with FunctionFS USB. Real
   USB enumeration, `Features`, multi-packet protocol traffic, reconnects, and
   interactive confirmation are proven. Full Suite workflows and the separate
   U2F HID interface remain.
3. **Complete:** the genuine framebuffer is sent to a selectable SSD1306 or
   SH1106 I2C stream, with GPIO-backed buttons. Unit, full-worker, target
   electrical, second-Pi rendering, interactive-button, and 400 kHz
   oscilloscope validation pass.
4. **Pending:** attach and validate the physical OLED/button HAT.

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
