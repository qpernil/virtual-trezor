# Implementation provenance

Virtual Trezor combines a pinned, unmodified upstream Trezor firmware checkout
with an independently written Raspberry Pi platform layer.

The platform layer was developed with OpenAI Codex under Per Nilsson's
direction in August 2026. It was implemented from the public upstream emulator
interfaces, Linux FunctionFS, I2C, SPI and GPIO APIs, public display-controller
documentation, and black-box interoperability tests. No private Trezor source,
documentation, signing material, or other confidential information was used.

The official `trezor/trezor-firmware` repository is included as the
`upstream/trezor-firmware` Git submodule, pinned to commit
`30be4e8c9488eeab68f994af23b3d9c9b7334266` (`core/v2.12.4`, containing the
current Trezor One 1.14.1 sources). This project does not rewrite or relicense
that source. Its build replaces selected upstream emulator platform objects at
link time without permanently patching the submodule.

The SH1106 backend uses functional command values from public controller
documentation and was checked against publicly available hardware examples.
No Waveshare source file is included in this repository.

Compatibility was tested through normal Trezor protocol exchanges using
public host tools and Trezor Suite. Production Suite correctly identifies the
worker as unauthenticated emulator/debug firmware and may refuse transaction
operations. That restriction is intentionally documented and is not bypassed
by this project.
