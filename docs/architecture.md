# Architecture

## Process boundary

`usb-gadget-supervisor` creates the USB gadget, validates and publishes the
profile's FunctionFS descriptors, opens the resulting endpoints and declared
I2C/SPI devices, claims exact GPIO line groups, drops privileges, and launches
this worker.
The worker receives `ep0`, OUT, and IN as open file descriptors and owns all
runtime USB traffic. It opens no USB path. The supervisor does not interpret
Trezor messages or proxy secret material.

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
`oledRefresh` implementations pass the existing framebuffer and inherited
bus/GPIO descriptors through the C ABI of the sibling `display-backends`
library. The library's native layer owns controller initialization, signaling,
native page/RGB565 writes, clearing, and display-off. Its optional conversion
layer maps the producer format into that native frame. The Trezor adapter retains
selection, retry, timing, and logging policy. `emulatorPoll` remains a platform ABI symbol because the
upstream firmware loop calls it in emulator builds. In this implementation it
retries a failed display transfer after one second, reinitializing the
controller and retransmitting the current framebuffer independently of later
UI changes. The project-owned worker entry point parses the display selection
before calling the renamed, otherwise unmodified upstream firmware entry
point. I2C is a Pi virtual-display choice; the factory SH1106 HAT uses the
four-wire SPI backend, which is the worker default.
The optional ST7789 backend converts the same completed 1-bit framebuffer to
RGB565 and scales it to a centered 240x120 window on the 240x240 panel. It does
not replace Trezor's own layout, font, bitmap, or drawing routines.
The shared handle declares this producer as the neutral
`Mono1MsbReversePage` format with width 128, height 64, and stride 128. The
library also accepts row-major `Mono8` and RGB565 producers, independently of
controller selection, for workers with future framebuffer formats. A producer
already composing the controller's native 1-bit or RGB565 frame can bypass
conversion entirely.

Buttons have the same useful split. `legacy/buttons.c` continues to own
`buttonUpdate` and the real debounce/state transitions. The Pi port supplies
only `buttonRead`, receiving one line-request handle ordered as No, Yes, and
center. The profile maps that group to active-low GPIO5, GPIO26, and GPIO13 with
pull-ups and both-edge events. The kernel therefore exposes logical pressed
bits and a pollable event stream without giving the worker GPIO-chip access.
On the physical HAT, center maps to both logical Trezor buttons. The original
two lines may instead be driven by the remote virtual-display process, whose
middle click activates both.

The platform wait polls USB, lifecycle, packet, and button descriptors for no
longer than the timeout requested by the upstream firmware. Events and a due
display-recovery deadline may wake it earlier. Preserving the firmware's 10 ms
main-loop cadence keeps automatic lock, busy-screen expiry, and button state
processing faithful to the legacy emulator contract.

USB has two emulator-specific layers that must both be excluded:

- `legacy/firmware/udp.c` implements the firmware-facing `usbInit`,
  `usbPoll`, `waitAndProcessUSBRequests`, `usbTiny`, and `usbFlush`.
- `legacy/emulator/udp.c` implements the localhost datagram sockets beneath
  those calls.

The Pi port replaces the firmware-facing layer directly with inherited
FunctionFS endpoint I/O and adds the supervisor resource/liveness connection. It does not
reproduce the socket abstraction. Emulator flash, timer, and randomness remain
in place; desktop SDL does not.

`mk/worker-firmware.mk` includes the genuine upstream firmware Makefile and
supplies explicit rules for the project platform objects. The expected `udp.o`
is compiled from `platform/raspberry-pi/usb_functionfs.c`. The derived emulator
support archive is constructed only from `setup.o`, `memory.o`, `timer.o`, and
`strl.o`. Project objects provide USB, display, buttons, and worker entry/config
symbols. Controller mechanics come from the Rust static library. Neither
upstream datagram implementation nor an SDL object is present in the worker.

## Milestones

1. **Complete:** build and run the unmodified upstream SDL/UDP emulator as a
   baseline.
2. **In progress:** build the Pi worker with FunctionFS USB. Real
   USB enumeration, `Features`, multi-packet protocol traffic, reconnects, and
   interactive confirmation are proven. The profile now publishes WinUSB
   association descriptors and a WebUSB BOS capability matching the upstream
   legacy firmware. Device release `1.01` prevents the earlier descriptorless
   `1.00` enumeration result from suppressing Windows' Microsoft OS probe.
   Fresh Windows enumeration and automatic inbox WinUSB binding are validated;
   full Suite workflows and the separate U2F HID interface remain.
3. **Complete:** the genuine framebuffer is sent to a selectable SSD1306 or
   SH1106 I2C stream, with GPIO-backed buttons. Unit, full-worker, target
   electrical, second-Pi rendering, interactive-button, and 400 kHz
   oscilloscope validation pass.
4. **In progress:** the same SH1106 stream has a factory-HAT SPI transport;
   its ARM64 build, supervisor resource handoff, spidev configuration, GPIO,
   and USB startup pass. Physical OLED/button-HAT validation remains.

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
