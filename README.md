# Virtual Trezor

[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue.svg)](LICENSE)
[![Status: experimental](https://img.shields.io/badge/status-experimental-orange.svg)](#current-status)

`virtual-trezor` runs the upstream Trezor One firmware logic as an
unprivileged Raspberry Pi USB-gadget worker. The intended device uses the Pi's
real USB device controller, a physical 128x64 OLED, and two GPIO buttons. It is
a development simulator, not a hardware wallet: secrets stored on a
general-purpose Pi do not receive Trezor's physical, extraction, or
side-channel protections.

The worker is designed to run under
[`usb-gadget-supervisor`](https://github.com/qpernil/usb-gadget-supervisor).
The supervisor owns ConfigFS, FunctionFS setup, UDC binding, privilege drop,
and lifecycle control. This repository owns the Trezor protocol, persistent
simulated flash, display, buttons, and unprivileged endpoint I/O.

## Quick start

Clone the integration repository without recursively initializing every
dependency in the upstream monorepo, then let the project select only the
submodules required by Trezor One:

```sh
git clone https://github.com/qpernil/virtual-trezor.git
cd virtual-trezor
make init
make check
```

The FunctionFS worker must be built on Linux with a C toolchain, `uv`, and the
exact protobuf compiler required by the pinned firmware release:

```sh
PROTOC_BIN=/path/to/protoc-33.5 make worker
```

See [`docs/deployment.md`](docs/deployment.md) for Raspberry Pi prerequisites,
installation, profile customization, and USB gadget startup.

## Current status

The principal platform milestones now work:

- the official Trezor One `legacy/v1.14.1` emulator builds from an unmodified,
  pinned upstream checkout and passes a startup smoke test on macOS arm64;
- an aarch64 Raspberry Pi worker builds and runs that firmware behind the USB
  gadget supervisor, replacing UDP, desktop display, and desktop input with
  FunctionFS, I2C/SPI, and GPIO implementations.

The Pi was recognized by macOS as USB `1209:53c1`. The pinned Trezor host
library opened it, read a model `1` firmware `1.14.1` `Features` response, and
completed a 173-byte multi-packet ping. See
[`docs/upstream-baseline.md`](docs/upstream-baseline.md) and
[`docs/raspberry-pi-validation.md`](docs/raspberry-pi-validation.md).

The current worker keeps upstream UI composition and host support while
replacing the Raspberry Pi hardware boundary:

- retain upstream firmware, protobuf, cryptography, storage, UI composition,
  the generic OLED framebuffer, file-backed flash, timer, and randomness;
- exclude both UDP source files and the upstream SDL display/button objects;
- supply FunctionFS USB, supervisor-control, Linux display, and active-low
  GPIO button implementations from `platform/raspberry-pi`;
- send the unchanged framebuffer either to an SSD1306/SH1106 I2C display at
  address `0x3c` or to a factory-configured SH1106 SPI HAT;
- do not patch the upstream submodule.

Trezor Suite recognizes the worker and reaches its firmware-check and
on-device confirmation workflow; complete setup/recovery validation remains.
The FunctionFS descriptor set currently exposes only the main Trezor vendor
interface, so the separate U2F HID interface is also pending. A real Trezor
One uses SPI for its OLED. The physical-HAT backend therefore preserves that
transport style, while I2C remains an intentional Raspberry Pi adaptation for
the virtual display and future I2C-native modules. Both retain the genuine
upstream framebuffer and UI composition.

The checked-in I2C profile declares `/dev/i2c-1` and `/dev/gpiochip0`; the SPI
profile instead declares `/dev/spidev0.0` and `/dev/gpiochip0`. The worker's
`--display=ssd1306-i2c|sh1106-i2c|sh1106-spi` option selects the backend.
SH1106 modes pulse GPIO25 reset; SPI additionally drives GPIO24 Data/Command.
The two active-low firmware buttons are read on GPIO5 and GPIO26. Both I2C
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
| Release | `legacy/v1.14.1` |
| Commit | `725c0c01879329900f08fc453d8fd0fcb4d86090` |
| Model | Trezor One / `T1B1` |

Initialize the submodule and only the dependencies needed by the Trezor One
worker and its pinned Python tool environment:

```sh
make init
make check
```

This does not recursively initialize dependencies for newer Trezor models.
The linked firmware dependencies are nanopb, QR-Code-generator, and
secp256k1-zkp. The retained emulator setup object also includes a libopencm3
flash header, without linking that library. The monorepo's locked `uv`
environment additionally requires the unlinked `ts-tvl` Python package.

The optional upstream emulator baseline additionally needs SDL2, SDL2_image,
and an exactly compatible `protoc`:

```sh
make init-baseline
PROTOC_BIN=/path/to/protoc-33.5 make upstream-baseline
```

The baseline target is diagnostic only and still uses upstream SDL/UDP. On
Linux, `make worker` builds the headless FunctionFS/I2C/SPI/GPIO worker; it
links neither upstream UDP nor SDL display/button implementations.

## Documentation

- [Architecture and platform boundaries](docs/architecture.md)
- [Upstream baseline](docs/upstream-baseline.md)
- [Raspberry Pi FunctionFS validation](docs/raspberry-pi-validation.md)
- [I2C display and oscilloscope plan](docs/i2c-display-plan.md)
- [Raspberry Pi deployment](docs/deployment.md)
- [Raspberry Pi platform implementation](platform/raspberry-pi/README.md)

## Safety and identity

Never use this software to protect real funds, recovery seeds, passphrases, or
other valuable secrets. Emulator randomness and file-backed storage are not
hardware security.

Trezor names, protocol identifiers, and USB identifiers are used only for
controlled compatibility testing. This project is not affiliated with or
endorsed by Trezor Company.

## Contributing

The project is experimental, and hardware reports, USB compatibility results,
and focused code or documentation improvements are welcome. Read
[`CONTRIBUTING.md`](CONTRIBUTING.md) before opening a pull request. Report
security-sensitive findings according to [`SECURITY.md`](SECURITY.md), not in
a public issue.

## License

This integration project is licensed under GPL-3.0. The upstream monorepo
contains components under GPL-3.0, LGPL-3.0, MIT, and file-specific licenses;
the upstream notices remain authoritative for those files.
