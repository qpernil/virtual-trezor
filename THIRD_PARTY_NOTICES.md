# Third-party notices

## Trezor firmware submodule

The upstream source is pinned as a Git submodule at
`upstream/trezor-firmware`. Its root `LICENSE.md` is the authoritative license
map. Components linked into the Trezor One worker include:

| Component | Upstream location | License family |
| --- | --- | --- |
| Legacy Trezor One firmware | `legacy/` | LGPL-3.0-or-later |
| Common protobuf definitions | `common/` | LGPL-3.0-or-later |
| Persistent storage | `storage/` | GPL-3.0-or-later |
| Cryptography | `crypto/` | Primarily MIT, with file-specific notices |
| nanopb | `vendor/nanopb/` | zlib-style license |
| QR-Code-generator | `vendor/QR-Code-generator/` | MIT |
| secp256k1-zkp | `vendor/secp256k1-zkp/` | MIT |

The retained emulator setup includes a libopencm3 header; the worker does not
link libopencm3. The locked upstream Python environment also references
`ts-tvl`, which is not linked into the worker. Preserve all copyright and
license notices from the exact pinned source tree.

Because GPL-3.0 storage code is linked into the worker, the combined worker and
this integration are distributed under GPL-3.0. The repository's root
`LICENSE` contains that license.

## Complete corresponding source for binaries

Anyone distributing a worker binary must also satisfy GPL requirements for
complete corresponding source, including the exact upstream submodule content,
nested source dependencies used by the build, generated-source inputs, build
scripts, and integration sources.

GitHub's automatically generated source ZIP and tar archives omit Git submodule
contents. Do not rely on those archives alone for a binary release. Attach a
matching source bundle with initialized submodules, or provide another durable
GPL-compliant method of obtaining the complete corresponding source.

## Names and USB identity

Trezor is a trademark of Trezor Company s.r.o. This project is independent and
is not affiliated with, sponsored by, or endorsed by Trezor Company.

Trezor names, protocol identifiers, and the emulator USB VID/PID are used for
controlled compatibility testing. The VID/PID is not assigned to this project
and is not licensed here for redistributed or commercial hardware.

## Hardware references

The SH1106 backend implements public display-controller commands. Public
Waveshare example material was used only to check hardware behavior; no
Waveshare source file is included or linked.
