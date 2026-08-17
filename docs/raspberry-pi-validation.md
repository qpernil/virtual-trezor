# Raspberry Pi FunctionFS validation

## Verified deployment

| Item | Value |
| --- | --- |
| Date | 2026-08-17 |
| Host | Raspberry Pi, aarch64 |
| OS | Debian GNU/Linux 13 (trixie) |
| Upstream firmware | Trezor One `legacy/v1.14.1` at `725c0c01879329900f08fc453d8fd0fcb4d86090` |
| Worker SHA-256 | `3d7406c2a638af027729ef4b2bc1e2d41f06eb843525ad96bdb49d693eacf0fe` |
| Supervisor service | `usb-gadget-supervisor@virtual-trezor.service` |
| USB device controller | `fe980000.usb`, state `configured` |
| FunctionFS mount | `trezor` at `/dev/ffs-virtual-trezor` |

The root supervisor created ConfigFS and FunctionFS resources, bound the USB
device controller, and launched the firmware worker as the unprivileged user
`per`. The worker inherited the supervisor control socket and FunctionFS mount,
published its descriptors, and opened the endpoint files itself. SDL display
and buttons continued to run in that unprivileged process.

## Build

The Pi build used GCC, SDL2, SDL2_image, the pinned upstream `uv` environment,
and `libprotoc 33.5`:

```sh
PROTOC_BIN=/path/to/protoc-33.5 make worker
```

The build used the upstream legacy firmware Makefile for the real firmware
object list. `mk/worker-firmware.mk` changed only the source rule for the
firmware's expected `udp.o`, compiling it from
`platform/raspberry-pi/usb_functionfs.c`. The upstream emulator support archive
provided SDL, buttons, flash, timer, and host setup after its UDP member was
removed. The final worker contained the project FunctionFS symbols and neither
upstream `emulatorSocket` implementation.

## USB and protocol results

The gadget enumerated on macOS at full speed with:

| Field | Observed value |
| --- | --- |
| Vendor/product | `1209:53c1` |
| Manufacturer | `SatoshiLabs` |
| Product | `TREZOR` |
| Serial | `virtual-trezor-one` |
| Device release | `0x0100` |

The pinned `trezorlib` WebUSB transport discovered `webusb:001:1:4`, opened the
real USB endpoints, and received these unmodified firmware features:

```text
vendor=trezor.io
model=1
firmware=1.14.1
initialized=False
capabilities=Bitcoin,Bitcoin_like,Crypto,Ethereum,NEM,Stellar,U2F
```

A no-protection ping with a 173-byte message returned the identical payload.
That exercises both OUT and IN traffic across multiple 64-byte FunctionFS
interrupt transfers, not merely descriptor enumeration.

## Known limitations

- Only the main vendor interface is published. The firmware advertises U2F,
  but the separate U2F HID interface is not yet exposed.
- Trezor Suite acceptance has not yet been tested.
- The current display and buttons use SDL/X11. Physical OLED-over-I2C and GPIO
  button drivers are the next platform milestone.
- The profile requests USB BCD `0x0210`, matching current Trezor One firmware,
  but this deployed supervisor/gadget instance reported `0x0200`. Main WebUSB
  communication works; BOS/WebUSB descriptor parity still needs review.
- Emulator file storage and `rand_insecure.c` remain in use. This build is not
  suitable for real seeds, funds, credentials, or production cryptography.
