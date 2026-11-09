# Implementation provenance

Virtual Trezor combines a pinned fork of the official Trezor firmware
with an independently written Raspberry Pi platform layer.

The platform layer was developed with OpenAI Codex under Per Nilsson's
direction in August 2026. It was implemented from the public upstream emulator
interfaces, Linux FunctionFS, I2C, SPI and GPIO APIs, public display-controller
documentation, and black-box interoperability tests. No private Trezor source,
documentation, signing material, or other confidential information was used.

The `qpernil/trezor-firmware` fork is included as the
`upstream/trezor-firmware` Git submodule, pinned to
`ace9215f2c09a43438fd2a33b11f9888355c5cd7`. Its official upstream base is
`037e506db454691bda188df2803df2fc26c75f36` (`core/v2.13.0`, containing Trezor
One `1.14.2`). Hardware integration, OS randomness, and compiler compatibility
are committed changes in that fork, documented in its `VIRTUAL_TREZOR.md`.
The original licensing is retained. The integration build supplies platform
objects and environment options without applying patches or changing tracked
submodule files.

The SH1106 backend uses functional command values from public controller
documentation and was checked against publicly available hardware examples.
No Waveshare source file is included in this repository.

Compatibility was tested through normal Trezor protocol exchanges using
public host tools and Trezor Suite. Production Suite correctly identifies the
worker as unauthenticated emulator/debug firmware and may refuse transaction
operations. That restriction is intentionally documented and is not bypassed
by this project.
