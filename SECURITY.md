# Security Policy

Virtual Trezor is experimental compatibility software. It has not been audited
as a wallet, does not provide a trusted display or tamper resistance, and must
not be used with real funds, recovery seeds, passphrases, or valuable secrets.

## Supported versions

Security fixes are made on the `main` branch. Historical commits and upstream
firmware releases pinned by older commits are not maintained separately.

## Reporting a vulnerability

Do not open a public issue for a suspected vulnerability. Use GitHub's private
vulnerability reporting feature for this repository. Include the affected
component, expected impact, reproduction details, platform and kernel versions,
and any suggested mitigation. Do not attach real wallet state or secrets.

Issues in the upstream Trezor firmware should follow upstream's security
policy. Reports about ConfigFS privilege handling, worker lifecycle, or UDC
cleanup may belong in
[`usb-gadget-supervisor`](https://github.com/qpernil/usb-gadget-supervisor)
instead.

## Security model

The supervisor/worker split limits the amount of project code running as root,
but the worker, storage, display path, operating system, and Raspberry Pi remain
ordinary software-accessible components. Emulator randomness and file-backed
state do not reproduce the physical, extraction, side-channel, firmware
authenticity, or supply-chain protections of a genuine hardware wallet.
