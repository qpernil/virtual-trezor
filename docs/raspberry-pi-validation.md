# Raspberry Pi FunctionFS validation

## Verified deployment

| Item | Value |
| --- | --- |
| Date | 2026-08-17 |
| Host | Raspberry Pi, aarch64 |
| OS | Debian GNU/Linux 13 (trixie) |
| Upstream firmware | Trezor One `legacy/v1.14.1` at `725c0c01879329900f08fc453d8fd0fcb4d86090` |
| Worker SHA-256 | `1f99db42f49b452b1d32fb469c7fffa419b85a5b2c944002a9afdf57b6b9ac0b` |
| Supervisor service | `usb-gadget-supervisor@virtual-trezor.service` |
| USB device controller | `fe980000.usb`, state `configured` |
| FunctionFS mount | `trezor` at `/dev/ffs-virtual-trezor` |

The root supervisor created ConfigFS and FunctionFS resources, bound the USB
device controller, and launched the firmware worker as the unprivileged user
`per`. The worker inherited the supervisor control socket and FunctionFS mount,
published its descriptors, and opened the endpoint files itself. The current
worker drives I2C and GPIO through additional supervisor-opened descriptors.

## Build

The Pi build used GCC, the pinned upstream `uv` environment, and `libprotoc
33.5`:

```sh
PROTOC_BIN=/path/to/protoc-33.5 make worker
```

The build used the upstream legacy firmware Makefile for the real firmware
object list. `mk/worker-firmware.mk` supplies project objects for the
firmware's expected USB, display, and button symbols. The emulator support
archive is constructed only from setup, memory, timer, and string compatibility
objects. The final worker contains FunctionFS/I2C/GPIO platform symbols and
neither upstream `emulatorSocket` implementation nor SDL.

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

The deployed adapter was subsequently tested against the host lifecycle used
by Trezor Suite. A challenged `GetFirmwareHash` returned 32 bytes in 0.246
seconds; the host then closed the FunctionFS interface, reopened it, and
completed a new ping without restarting the service. The adapter consumes
FunctionFS `ENABLE`/`DISABLE` events so a host interface reset cannot leave the
worker logically detached.

Trezor Suite recognizes the gadget and reaches its firmware check and
on-device confirmation workflow. It correctly warns that the firmware check
does not authenticate this Linux worker as an official signed embedded image.

A protected ping was used to verify interactive behavior. While the host
waited on a `ProtectCall` button request, a dedicated FunctionFS reader thread
remained blocked in the kernel and the main firmware thread continued polling
buttons. The second-Pi virtual-display client held the appropriate GPIO line
low for the duration of a mouse press, producing the genuine upstream button
press/release transition and completing the request. Left, right, and both-
button input are represented by the two independent active-low lines.

## Coexistence with other gadget profiles

The Virtual Trezor worker and profile may remain installed beside Virtual
YubiKey. They are selectable profiles, not simultaneous independent devices:
only one supervisor instance can own the Pi's single UDC at a time. Switching
profiles requires stopping the active gadget, allowing supervisor cleanup,
and then starting the other profile. Two Pis can expose the two identities
concurrently.

## Known limitations

- Only the main vendor interface is published. The firmware advertises U2F,
  but the separate U2F HID interface is not yet exposed.
- Full Trezor Suite device initialization and recovery/setup workflows still
  require interactive validation.
- The required SSD1306/SH1106 I2C backend and GPIO button backend are deployed.
  SSD1306 transferred complete frames to two Pi 3 targets
  at a measured 400 kHz with zero receive overruns or drops. SH1106 produced
  two deterministic captures of 139,412 bytes each: 28 initialization bytes
  plus 131 complete 1,064-byte page-addressed refreshes. Target-side rendering
  and remote button control are validated; a physical OLED/button HAT remains.
  The real Trezor One OLED
  is SPI; this I2C stream is a Raspberry Pi adaptation around the unchanged
  upstream framebuffer. See [`i2c-display-plan.md`](i2c-display-plan.md).
- The profile requests USB BCD `0x0210`, matching current Trezor One firmware,
  but this deployed supervisor/gadget instance reported `0x0200`. Main WebUSB
  communication works; BOS/WebUSB descriptor parity still needs review.
- Emulator file storage and `rand_insecure.c` remain in use. This build is not
  suitable for real seeds, funds, credentials, or production cryptography.
