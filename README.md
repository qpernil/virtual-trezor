# Virtual Trezor

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

## Current status

Two milestones now work:

- the official Trezor One `legacy/v1.14.1` emulator builds from an unmodified,
  pinned upstream checkout and passes a startup smoke test on macOS arm64;
- an aarch64 Raspberry Pi worker builds and runs that firmware behind the USB
  gadget supervisor, replacing UDP with FunctionFS while retaining the SDL UI.

The Pi was recognized by macOS as USB `1209:53c1`. The pinned Trezor host
library opened it, read a model `1` firmware `1.14.1` `Features` response, and
completed a 173-byte multi-packet ping. See
[`docs/upstream-baseline.md`](docs/upstream-baseline.md) and
[`docs/raspberry-pi-validation.md`](docs/raspberry-pi-validation.md).

The current worker deliberately keeps the proven emulator UI and host support
while changing only the transport boundary:

- retain upstream firmware, protobuf, cryptography, storage, UI composition,
  the generic OLED framebuffer, SDL display, file-backed flash, timer, and
  randomness;
- exclude both UDP source files;
- supply FunctionFS USB, supervisor-control, and practical SDL keyboard/mouse
  button implementations from `platform/raspberry-pi`;
- do not patch the upstream submodule.

Trezor Suite recognizes the worker and reaches its firmware-check and
on-device confirmation workflow; complete setup/recovery validation remains.
The FunctionFS descriptor set currently exposes only the main Trezor vendor
interface, so the separate U2F HID interface is also pending. After that
compatibility work, the next platform milestone replaces SDL display/buttons
with the physical OLED and GPIO drivers.

See [`docs/architecture.md`](docs/architecture.md) and
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
secp256k1-zkp. The monorepo's locked `uv` environment additionally requires
the unlinked `ts-tvl` Python package to be present.

The optional upstream emulator baseline also needs libopencm3 headers, SDL2,
SDL2_image, and an exactly compatible `protoc`:

```sh
make init-baseline
PROTOC_BIN=/path/to/protoc-33.5 make upstream-baseline
```

The baseline target is diagnostic only. On Linux, `make worker` builds the
FunctionFS worker. It keeps SDL but does not link either upstream UDP
implementation. The later hardware-UI target will also remove SDL.

## Safety and identity

Never use this software to protect real funds, recovery seeds, passphrases, or
other valuable secrets. Emulator randomness and file-backed storage are not
hardware security.

Trezor names, protocol identifiers, and USB identifiers are used only for
controlled compatibility testing. This project is not affiliated with or
endorsed by Trezor Company.

## License

This integration project is licensed under GPL-3.0. The upstream monorepo
contains components under GPL-3.0, LGPL-3.0, MIT, and file-specific licenses;
the upstream notices remain authoritative for those files.
