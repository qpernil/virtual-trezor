# Virtual Trezor

[![CI](https://github.com/qpernil/virtual-trezor/actions/workflows/ci.yml/badge.svg)](https://github.com/qpernil/virtual-trezor/actions/workflows/ci.yml)
[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue.svg)](LICENSE)
[![Status: experimental](https://img.shields.io/badge/status-experimental-orange.svg)](#current-status)

`virtual-trezor` runs genuine upstream Trezor One and Trezor Safe 3 firmware
logic as independent, unprivileged Raspberry Pi USB-gadget workers. The
workers use the Pi's real USB device controller, physical displays, and GPIO
buttons. This is a development simulator, not a hardware wallet: secrets
stored on a general-purpose Pi do not receive Trezor's physical, extraction,
or side-channel protections.

Deployment is tested on both 64-bit Ubuntu and 64-bit Raspberry Pi OS. On
either system, enabling DWC2 in peripheral mode provides the UDC used by the
supervisor through ConfigFS and FunctionFS.

The worker is designed to run under
[`usb-gadget-supervisor`](https://github.com/qpernil/usb-gadget-supervisor).
The supervisor owns ConfigFS, FunctionFS setup, UDC binding, privilege drop,
and lifecycle control. This repository owns its Raspberry Pi integration,
persistent simulated-flash adaptation, display policy, button backend, and
unprivileged endpoint I/O. Controller-specific mechanics are shared through
[`display-backends`](https://github.com/qpernil/display-backends). Upstream
Trezor code remains owned and licensed by its respective copyright holders.

## Quick start

Clone the integration repository without recursively initializing every
dependency in the upstream monorepo, then let the project select only the
submodules required by the checked firmware targets:

```sh
git clone https://github.com/qpernil/virtual-trezor.git
git clone https://github.com/qpernil/display-backends.git
cd virtual-trezor
make init
make check
```

Keep the repositories as sibling directories. Built workers require Linux, a
C toolchain, the current stable Rust toolchain, and `uv`.
The build obtains the matching protobuf compiler through pinned
`grpcio-tools==1.81.0` package metadata:

```sh
make worker
```

The default runtime arrangement is a factory-configured SH1106 SPI HAT on the
same Pi as the worker. SPI0 uses GPIO8/10/11, Data/Command uses GPIO24, reset
uses GPIO25, and the Trezor No/Yes inputs use joystick-left/right on
GPIO5/GPIO26. Joystick-center on GPIO13 reports both logical buttons. Install
`profiles/virtual-trezor.toml` for that arrangement.
Use `profiles/virtual-trezor-i2c.toml` only for the second-Pi SDL viewer or a
resoldered I2C display.
Use `profiles/virtual-trezor-st7789.toml` for the Waveshare 240x240 ST7789 LCD
HAT; it scales the unchanged legacy framebuffer to a centered 240x120 image.

See [`docs/deployment.md`](docs/deployment.md) for Raspberry Pi prerequisites,
installation, profile customization, and USB gadget startup.

## Current status

The supported configurations are:

- the official Trezor One `legacy/v1.14.1` emulator builds from an unmodified,
  pinned upstream checkout and passes a startup smoke test on macOS arm64;
- an aarch64 Raspberry Pi worker builds and runs that firmware behind the USB
  gadget supervisor, replacing UDP, desktop display, and desktop input with
  FunctionFS, I2C/SPI, and GPIO implementations;
- the upstream Safe 3 revision B (`T3B1`) Core `2.12.4` firmware builds as a
  separate worker with its genuine MicroPython/Rust UI, file-backed state,
  USB personality, and protocol stack;
- Safe 3 is physically validated on the 240x240 ST7789 HAT with GPIO buttons,
  macOS USB enumeration, `trezorctl`, and Trezor Suite in debug mode; and
- the unmodified upstream Safe 3 SDL/UDP emulator averages 9.85% of one Pi 4
  core at its settled home screen, versus about 1.7% on an M-series Mac,
  because its Unix event loop wakes every millisecond; and
- Safe 3's Linux virtual-WFI integration settles at approximately 0–0.2% of
  one Pi 4 core without advancing firmware time while suspended or delaying
  firmware deadlines. It blocks until USB/control activity, endpoint
  readiness, a GPIO edge, or the remaining time on Core's virtual timer
  expires.

Use the current acceptance checklist in
[`docs/raspberry-pi-validation.md`](docs/raspberry-pi-validation.md) to verify
enumeration, resource confinement, transport, UI, and worker-incarnation
recovery.

The Trezor One worker keeps upstream UI composition and host support while
replacing the Raspberry Pi hardware boundary:

- retain upstream firmware, protobuf, cryptography, storage, UI composition,
  the generic OLED framebuffer, and file-backed flash; retain the firmware RNG
  API while sourcing its entropy from the Linux kernel;
- exclude both UDP source files and the upstream SDL display/button objects;
- supply a virtual `libopencm3` USB controller, direct FunctionFS endpoints,
  the supervisor control channel, Linux display, and inherited GPIO-line button
  implementations from
  `platform/raspberry-pi`;
- block the Linux worker in `poll` only until the deadline requested by the
  upstream firmware, normally 10 ms, so idle CPU remains low without delaying
  automatic lock, busy-screen expiry, or button processing;
- send the unchanged framebuffer either to an SSD1306/SH1106 I2C display at
  address `0x3c`, a factory-configured SH1106 SPI HAT, or a 240x240 ST7789 SPI
  HAT;
- do not patch the upstream submodule.

Trezor Suite recognizes the Trezor One worker, can initialize its simulated state, and
reaches on-device confirmation workflows. Production Suite reports the
expected firmware-integrity failure and refuses cryptocurrency transaction
operations because this worker is not authenticated production firmware.
Recovery remains unvalidated.
The Trezor One firmware configuration exposes the main Trezor vendor interface
and the U2F HID interface, with two interrupt endpoints each. The TOML profiles
contain no USB identity. At startup a shared discovery parser asks the genuine
legacy control engine for its device, configuration, string,
Microsoft OS 1.0, and WebUSB descriptors. It serializes the resulting typed
personality as CBOR; the supervisor validates and projects it into ConfigFS and
FunctionFS. Thus interface zero receives the firmware's `WINUSB` compatible ID
and interface GUID, while its BOS reports WebUSB 1.0 without a landing page.
WinUSB supplies the Windows driver; WebUSB separately enables permissioned
browser discovery and access. The worker reports the firmware's device release
`1.00` without a virtual override. Windows caches enumeration results by device
identity; clearing that host-side cache is an operational concern rather than
part of descriptor discovery.
Worker-requested reconnects preserve the firmware process while the supervisor
replaces the complete USB generation and guarantees at least 250 ms detached
before rebinding.

A real Trezor One uses SPI for its OLED. The physical-HAT backend therefore preserves that
transport style, while I2C remains an intentional Raspberry Pi adaptation for
the virtual display and future I2C-native modules. Both retain the genuine
upstream framebuffer and UI composition.

The default checked-in profile declares `/dev/spidev0.0`, an exact
display-control GPIO output group, and an exact button GPIO input/event group;
the explicitly named I2C profile substitutes `/dev/i2c-1` for the display bus.
The supervisor claims both GPIO groups and passes only their line-request
handles, never the GPIO-chip handle. With no argument the worker selects SH1106
SPI. The
`--display=ssd1306-i2c|sh1106-i2c|sh1106-spi|st7789-spi` option overrides it.
SH1106 modes pulse GPIO25 reset; SPI additionally drives GPIO24 Data/Command.
The ST7789 HAT instead uses GPIO25 Data/Command, GPIO27 reset, GPIO24
backlight, and SPI mode 0 at 62.5 MHz.
The two active-low firmware buttons are read on GPIO5 and GPIO26; GPIO13 maps
the HAT joystick-center action to both simultaneously. Both I2C
display streams have been validated at 400 kHz against the Pi 3 target driver.
A transient display failure leaves USB running; the regular emulator poll
path reinitializes the display after one second and retransmits the current
framebuffer. A second Pi can receive the I2C traffic through
[`raspberry-pi-i2c-target`](https://github.com/qpernil/raspberry-pi-i2c-target),
reconstruct the display, render it through SDL, and drive the worker's two
button GPIO inputs from mouse presses. See
[`docs/i2c-display-plan.md`](docs/i2c-display-plan.md).

Each Pi exposes one USB device controller. Virtual YubiKey and Virtual Trezor
profiles may both be installed, but only one profile can bind that controller
at a time. Running both as independent USB devices requires two Pis or other
independent device controllers.

See [`docs/architecture.md`](docs/architecture.md),
[`docs/i2c-display-plan.md`](docs/i2c-display-plan.md), and
[`mk/worker-sources.mk`](mk/worker-sources.mk).

## Upstream source

The official
[`trezor/trezor-firmware`](https://github.com/trezor/trezor-firmware)
repository is a Git submodule at `upstream/trezor-firmware`, pinned to:

| Item | Value |
| --- | --- |
| Core release | `core/v2.12.4` |
| Legacy release level | `1.14.1` |
| Commit | `30be4e8c9488eeab68f994af23b3d9c9b7334266` |
| Models | Trezor One / `T1B1`; Safe 3 rev B / `T3B1` |

Initialize the submodule and only the dependencies needed by the Trezor One
worker and its pinned Python tool environment:

```sh
make init
make check
```

This does not recursively initialize dependencies for other Trezor models.
The linked firmware dependencies are nanopb, QR-Code-generator, and
secp256k1-zkp. The retained emulator setup object also includes a libopencm3
flash header, without linking that library. The monorepo's locked `uv`
environment additionally requires the unlinked `ts-tvl` Python package.

The optional upstream emulator baseline additionally needs SDL2 and
SDL2_image. It uses the same package-managed protobuf compiler as the worker:

```sh
make init-baseline
make upstream-baseline
```

The baseline target is diagnostic only and still uses upstream SDL/UDP. On
Linux, `make worker` builds the headless FunctionFS/I2C/SPI/GPIO worker; it
links neither upstream UDP nor SDL display/button implementations.

The separate Safe 3 (`T3B1`) baseline proves the genuine Core, MicroPython,
Rust UI, and cryptography source graph without changing the Trezor One worker:

```sh
make safe3-baseline
```

On Debian-family Linux systems this exact desktop emulator additionally
requires `libsdl3-dev` and `libsdl3-image-dev`. The baseline intentionally
retains upstream SDL display/input and UDP USB.

Safe 3 project artifacts are unfrozen Unix emulator builds. They embed
MicroPython and the native C/Rust modules, then load application `.py` modules
from `upstream/trezor-firmware/core/src` and compile them to bytecode at
runtime. Real firmware instead links `mpy-cross` output into its image. See
[`docs/trezor-safe3.md`](docs/trezor-safe3.md) for the complete execution model.

The separate Linux display diagnostic target is:

```sh
make safe3-display
```

This target removes SDL entirely. Its project-owned Core display driver sends
the genuine 128x64 `Mono8` framebuffer unchanged to `display-backends`; the
selected backend alone converts and scales it for the physical panel. The USB
worker supports the same `ssd1306-i2c`, `sh1106-i2c`, `sh1106-spi`, and
`st7789-spi` choices as the Trezor One worker; the supervisor resource name
selects the matching backend. Button
input is deliberately inert in this stage, while USB remains on the upstream
UDP transport and the upstream lifecycle and timing remain unchanged. The
result is `build/safe3-t3b1-display/virtual-trezor-safe3-display`, accompanied
by its shared `libdisplay_backends.so`. Run the unfrozen Core binary with
`upstream/trezor-firmware/core/src` as its working directory.

The input diagnostic target retains that display boundary and adds the Pi HAT's
physical buttons through Core's genuine button poller:

```sh
make safe3-input
```

GPIO5 and GPIO26 map to Safe 3 left and right; GPIO13 maps the HAT's center
press to both buttons. The result is
`build/safe3-t3b1-input/virtual-trezor-safe3-input`. Set
`VIRTUAL_TREZOR_BUTTON_TRACE=1` only when diagnosing transitions. The normal
driver is silent.

The complete USB target replaces Core's Unix UDP transport with the
supervisor personality and endpoint protocol:

```sh
make safe3-usb
```

Core's `usb_init`, `usb_webusb_add`, and `usb_hid_add` calls populate the
shared Rust `UsbPersonalityBuilder`; `usb_start` finishes and publishes the
same typed personality used by legacy firmware discovery. The resulting
`build/safe3-t3b1-usb/virtual-trezor-safe3-usb` is accompanied by
`libdisplay_backends.so` and `libusb_gadget_worker.so`. The checked-in
`profiles/virtual-trezor-safe3.toml` is the ST7789 default; the adjacent named
Safe 3 profiles select either SH1106 transport or SSD1306 while retaining the
same firmware state directory. All keep Safe 3 state and FunctionFS mounts
separate from Trezor One. Holding display-HAT KEY3 publishes an empty USB
personality, so the powered worker remains absent from USB until release;
release republishes the genuine Core personality immediately. Core first sends
an empty configuration as its readiness declaration, so an interactive boot or
unlock screen can keep USB absent indefinitely without holding the supervisor
startup path or restarting a healthy firmware worker.

Trezor Suite must run in **debug mode** when using this worker. Debug mode
selects Suite's emulator trust path, so it accepts the genuine upstream
emulator build as genuine firmware even though the transport is the Pi's
physical USB gadget rather than the usual UDP emulator transport. Production
mode intentionally applies production firmware-authenticity policy and may
reject protected operations.

## Documentation

- [Architecture and platform boundaries](docs/architecture.md)
- [Virtual Trezor Safe 3](docs/trezor-safe3.md)
- [Upstream baseline](docs/upstream-baseline.md)
- [Raspberry Pi FunctionFS validation](docs/raspberry-pi-validation.md)
- [I2C display and oscilloscope plan](docs/i2c-display-plan.md)
- [Raspberry Pi deployment](docs/deployment.md)
- [Raspberry Pi platform implementation](platform/raspberry-pi/README.md)
- [Implementation provenance](PROVENANCE.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)

## Safety and identity

Never use this software to protect real funds, recovery seeds, passphrases, or
other valuable secrets. Linux kernel entropy and file-backed storage do not
provide the physical isolation or hardware security of a genuine Trezor.

The USB VID/PID and descriptor strings come from the upstream firmware for
controlled, local compatibility testing; they do not distinguish this worker
from genuine hardware. The identifier is not a project assignment and must not
be used for redistributed, manufactured, or commercial devices without
permission from its owner.

This independent integration is not affiliated with, sponsored by, or
endorsed by Trezor Company. Trezor is a trademark of Trezor Company s.r.o.; the
name is used descriptively to identify the upstream firmware and protocol with
which the project interoperates.

## Contributing

The project is experimental, and hardware reports, USB compatibility results,
and focused code or documentation improvements are welcome. Read
[`CONTRIBUTING.md`](CONTRIBUTING.md) before opening a pull request. Report
security-sensitive findings according to [`SECURITY.md`](SECURITY.md), not in
a public issue.

## License

This integration project is licensed under GPL-3.0. The upstream monorepo
contains components under GPL-3.0, LGPL-3.0, MIT, and file-specific licenses;
the upstream notices remain authoritative for those files. Binary
redistributors must provide the complete corresponding source and build
scripts. GitHub's automatic source archives do not contain submodule contents;
see [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) before publishing a
release.
