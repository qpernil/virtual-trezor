# Architecture

## Process boundary

`usb-gadget-supervisor` opens declared I2C/SPI devices, claims exact GPIO line
groups, drops privileges, and launches this worker. The worker then publishes
the USB personality obtained from the genuine firmware. The supervisor
validates it, creates ConfigFS and FunctionFS, retains `ep0`, and passes the
four actual data-endpoint files to the worker. `ep0` lifecycle and setup
requests arrive as typed control-channel records. The worker opens no USB path.
The supervisor is absent from the Trezor packet path.

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

The platform wait polls the supervisor control socket, worker-local endpoint
queues, and button descriptors for no
longer than the timeout requested by the upstream firmware. Events and a due
display-recovery deadline may wake it earlier. Preserving the firmware's 10 ms
main-loop cadence keeps automatic lock, busy-screen expiry, and button state
processing faithful to the legacy emulator contract.

USB has two emulator-specific layers that must both be excluded:

- `legacy/firmware/udp.c` implements the firmware-facing `usbInit`,
  `usbPoll`, `waitAndProcessUSBRequests`, `usbTiny`, and `usbFlush`.
- `legacy/emulator/udp.c` implements the localhost datagram sockets beneath
  those calls.

The Pi port replaces the firmware-facing layer with a virtual `libopencm3`
controller. The genuine firmware still initializes its USB stack, handles
setup packets, configures endpoints, and processes packet callbacks as it would
on hardware. A shared Rust discovery parser directly drives that controller
through one control-transfer callback and returns the typed CBOR personality
expected by the supervisor. At runtime supervisor control records and direct
FunctionFS data handles drive the same virtual controller. Emulator flash and
timer remain in place; the firmware randomness API is backed by Linux kernel
entropy. The UDP abstraction and desktop SDL do not.

One worker-owned helper thread serves each blocking FunctionFS endpoint; none
calls firmware code. An OUT helper publishes one completed transfer through a
single controller handoff slot and wakes Core with `eventfd`. An IN helper
drains one controller submission slot into FunctionFS and signals write
readiness again only after the host accepts it. Consequently a firmware write
returns when the virtual controller accepts the report, as on hardware,
instead of freezing Core until the host reads it. There is no socket, stream
framing, acknowledgement, or higher-level buffering. Trezor message assembly
and USB backpressure remain in the genuine firmware and controller facade.

The supervisor control socket carries named hardware resources once, complete
USB configurations from worker to supervisor, quiesce/serving lifecycle
records, and replacement FunctionFS endpoint files. `usbReconnect()` republishes the
firmware personality and causes a real UDC unbind/rebind while the firmware
process survives. The supervisor guarantees at least 250 ms between UDC detach
and replacement bind. Invalid replacement CBOR is rejected before the active
USB generation is disturbed.

Safe 3 also uses the control channel's split reconnect form. A KEY3 press sends
an empty `Configure`, which unbinds and removes the active generation while the
worker and firmware keep running. KEY3 release sends the genuine Core
personality again. The supervisor binds it immediately, making the physical
hold the complete detached interval.

Host sleep is a normal firmware lifecycle, not a process restart. For the
legacy worker, a `SUSPEND`/`RESUME` pair preserves the current generation and
all firmware state. Suspend invokes the firmware callback, synchronously
checkpoints the mapped emulator flash image, turns the display off, and parks
inside the pollable supervisor control channel. The Raspberry Pi platform timer
does not advance while parked, so firmware deadlines do not consume suspended
time. Resume, or a reset-style wake followed by enable, restarts virtual time,
reinitializes the display, and retransmits the current framebuffer. The focused
flash checkpoint avoids stalling on unrelated filesystems; it is an
opportunistic safeguard rather than a guarantee that power will remain long
enough to complete it.

Safe 3 preserves its generation and gates endpoint readiness across the same
USB suspend/resume events, but genuine Core retains display policy: suspend,
unbind, and KEY3 ejection do not directly power the panel down. A clean worker
shutdown does explicitly deinitialize the display and disable its backlight.
If wake resets the link, `DISABLE`/`ENABLE` resets the virtual controller and
replays firmware configuration while leaving the worker alive. If USB VBUS is
the Pi's only power and the host removes it, the result is necessarily a cold
boot instead of a software event.

Core's hardware scheduler executes `WFI` when no source is ready. The standard
Unix emulator's `sysevents_poll()` sleeps for one millisecond and repeats its
source probes. The exact unmodified SDL/UDP emulator averages 9.85% of one Pi 4
core at a settled home screen, but only about 1.7% on an M-series Mac. The
simple one-millisecond delay is therefore inexpensive on a modern desktop and
material on the Pi. In the supervisor build, the exact no-event branch calls
`virtual_trezor_wait_for_interrupt(deadline)`. That function blocks in one
`ppoll` over the control channel, endpoint notifications, and GPIO edges,
bounded by Core's own nearest deadline. It then returns to the genuine
`sysevents` dispatcher to probe and dispatch readiness normally. This Linux
equivalent of interrupt wakeup measures 0–0.2% stable idle CPU without changing
firmware timer semantics.

`mk/worker-firmware.mk` includes the genuine upstream firmware Makefile and
supplies explicit rules for the project platform objects. The expected `udp.o`
is compiled from `platform/raspberry-pi/usb_functionfs.c`. The derived emulator
support archive is constructed only from `setup.o`, `memory.o`, `timer.o`, and
`strl.o`. Project objects provide USB, display, buttons, and worker entry/config
symbols. Controller mechanics come from the Rust static library. Neither
upstream datagram implementation nor an SDL object is present in the worker.

## Implementation status

The Trezor One worker is complete for the current integration scope. It runs
the genuine legacy `1.14.1` firmware with supervisor-managed FunctionFS USB,
firmware-discovered WinUSB/WebUSB descriptors, direct endpoint handles,
file-backed flash, selectable I2C/SPI displays, GPIO buttons, reconnect, and
USB suspend/resume. Its SH1106 and SSD1306 paths have been exercised on the Pi
and through the second-Pi SDL viewer; ST7789 is physically validated.

The separate Safe 3 revision B worker is also complete for the current
integration scope. It runs upstream Core `2.12.4` with its genuine
MicroPython/Rust UI, protocol and file-backed storage. Core constructs the
typed USB personality, publishes an empty personality until `usb_start()`, and
can remove/reinsert the gadget without restarting the worker. Its 128x64
`Mono8` framebuffer reaches `display-backends` unchanged, GPIO buttons feed
Core's genuine poller, secure randomness comes from the Unix RNG path, and
virtual WFI keeps idle CPU near zero. The ST7789 configuration is physically
validated with `trezorctl` and Trezor Suite in debug mode. The Safe 3 SH1106
and SSD1306 variants build and pass profile validation but still need final
physical-panel validation.

The detailed Trezor One display stages and the Pi 4 controller-clock finding
are in [`i2c-display-plan.md`](i2c-display-plan.md). Safe 3's complete build,
runtime and validation contract is in [`trezor-safe3.md`](trezor-safe3.md).

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
