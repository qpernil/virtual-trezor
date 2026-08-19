# Upstream build baseline

## Verified revision

| Item | Value |
| --- | --- |
| Date | 2026-08-17 |
| Upstream tag | `legacy/v1.14.1` |
| Upstream commit | `725c0c01879329900f08fc453d8fd0fcb4d86090` |
| Host | macOS arm64 |
| Compiler | Apple Clang 21 |
| Python environment | upstream `uv.lock` |
| Protobuf compiler | `grpcio-tools==1.81.0` (`libprotoc 33.5`) |
| Artifact | `legacy/firmware/trezor.elf` |

The upstream source and gitlink were not modified. Generated files and object
files are ignored within the upstream checkout.

## Minimal observed dependencies

The emulator baseline and Pi worker compile code or headers from these pinned
nested repositories:

- `vendor/nanopb`
- `vendor/QR-Code-generator`
- `vendor/libopencm3`, because retained emulator `setup.c` includes an STM32
  flash header even though the worker does not link libopencm3
- `vendor/secp256k1-zkp`
- `vendor/ts-tvl`, because the monorepo's locked `uv` project declares it as an
  editable Python dependency even though it is not linked into Trezor One

Both builds obtain `protoc` 33.5 from package-managed
`grpcio-tools==1.81.0`, matching the locked Python protobuf 6.33.5 runtime.
The baseline additionally requires SDL2 and SDL2_image for the unmodified
emulator display/buttons.

No other nested submodule was initialized.

## Host compatibility flags

Apple Clang 21 diagnoses two constructs more strictly than the Linux-oriented
upstream build. The baseline invocation adds:

```text
-Wno-gnu-folding-constant
-Wno-error=unterminated-string-initialization
```

These are invocation-only compatibility flags. They do not alter upstream
source and are not expected to be necessary for the Raspberry Pi GCC build.

## Result

The build completed through protobuf generation, nanopb generation, all legacy
firmware sources, cryptography, storage, final linking, and the
`legacy/firmware/trezor.elf` target. A headless startup smoke test remained
running after binding the expected localhost transport until interrupted.

The baseline script prints the artifact SHA-256 for each run. The macOS host
emulator is not asserted to be reproducible; the reproducibility guarantee for
official releases applies to the embedded firmware build in Trezor's pinned
Linux build environment.

The linked baseline intentionally contains SDL and UDP because it proves the
official upstream target before replacement. The Raspberry Pi worker now
replaces both upstream UDP implementations, the SDL display, and SDL buttons
with FunctionFS, I2C, and GPIO platform objects; its validation is recorded in
[`raspberry-pi-validation.md`](raspberry-pi-validation.md).
