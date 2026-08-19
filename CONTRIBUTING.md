# Contributing

Thank you for helping improve Virtual Trezor. The repository is experimental
compatibility software, not a production hardware-wallet implementation.

Discuss substantial USB, firmware-boundary, storage, or hardware-UI changes in
an issue before implementing them. Focused fixes and documentation corrections
can go directly to a pull request.

## Design constraints

Contributions must preserve these boundaries unless a proposal explicitly
justifies changing them:

- upstream Trezor firmware remains an unmodified, pinned Git submodule;
- upstream wallet, protocol, cryptography, UI composition, framebuffer, and
  button-state logic stay upstream;
- Raspberry Pi adaptations live in this repository's `platform`, `mk`,
  `profiles`, and `scripts` paths;
- real USB endpoint traffic goes directly between the worker and FunctionFS;
- the privileged supervisor owns ConfigFS, UDC lifecycle, and privilege drop,
  but does not interpret Trezor messages or handle wallet secrets;
- one physical UDC exposes one selected USB identity at a time; and
- I2C/GPIO implementations do not imply the physical-security properties of a
  genuine Trezor.

Do not patch files inside `upstream/trezor-firmware`. An upstream update should
be an auditable gitlink change to a reviewed release followed by baseline,
worker, USB, UI, and hardware validation.

## Validation

Initialize the selected upstream dependencies and run the repository checks:

```sh
make init
make check
```

Linux worker changes should also build with the package-managed pinned
protobuf compiler:

```sh
make worker
```

For USB- or hardware-visible changes, include the Pi model, OS, kernel, UDC,
host software, test workflow, and relevant sanitized logs or oscilloscope
measurements. Never include seeds, passphrases, private keys, wallet data, or
state files.

## Pull requests

Keep each pull request focused and include:

1. the problem and intended behavior;
2. effects on the upstream, privilege, USB, storage, or UI boundary;
3. tests and hardware validation performed; and
4. documentation updates for externally visible behavior.

By contributing, you agree that your contribution is licensed under GPL-3.0,
while upstream and vendored files retain their own licenses.
