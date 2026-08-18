# Raspberry Pi deployment

## Scope

This guide deploys the current SDL/FunctionFS worker on a 64-bit Raspberry Pi
Linux system. The worker is experimental and must use only disposable emulator
state and test seeds. It requires a USB Device Controller and a desktop/X11 or
Xwayland session for the current SDL display.

The tested architecture has two separately installed projects:

- `usb-gadget-supervisor` owns ConfigFS, FunctionFS, the UDC, privilege drop,
  and cleanup as root;
- `virtual-trezor-worker` runs the firmware integration and endpoint/UI logic
  as the desktop user named by the profile.

Only one gadget profile can own a UDC at a time. Stop Virtual YubiKey or any
other gadget service before starting Virtual Trezor. Do not enable Raspberry
Pi OS's `rpi-usb-gadget` Ethernet mode because its `g_ether` driver would claim
the same UDC.

## Hardware and boot prerequisites

Raspberry Pi 4 and Raspberry Pi 5 expose a USB device controller through their
USB-C data port when the appropriate `dwc2` overlay is active. Distribution
boot files differ, so merge the required overlay into the existing
`/boot/firmware/config.txt`; do not replace an Ubuntu configuration with a
Raspberry Pi OS file.

After reboot, verify that a controller is available:

```sh
ls -l /sys/class/udc
```

The validated Pi 4 controller appeared as `fe980000.usb`; a tested Pi 5
appeared as `1000480000.usb`. An empty directory means gadget mode is not ready.

## Build prerequisites

Install the normal C, SDL2, and package-config development dependencies. On
Debian-family systems the base packages are:

```sh
sudo apt install build-essential git pkg-config libsdl2-dev libsdl2-image-dev
```

Install `uv` using its supported packaging method and provide an executable
`protoc` whose `--version` output is exactly `libprotoc 33.5`. The worker build
rejects a different protobuf compiler so generated messages cannot silently
drift from the pinned release.

Clone and initialize only the selected Trezor One dependencies:

```sh
git clone https://github.com/qpernil/virtual-trezor.git
cd virtual-trezor
make init
make check
PROTOC_BIN=/absolute/path/to/protoc-33.5 make worker
```

The resulting executable is `build/virtual-trezor-worker`.

## Install the supervisor and worker

Build and install
[`usb-gadget-supervisor`](https://github.com/qpernil/usb-gadget-supervisor)
first, including its binary at `/usr/local/sbin/usb-gadget-supervisor` and its
template systemd unit.

Install the worker and profile:

```sh
sudo install -d -m 0755 /usr/local/libexec/virtual-trezor
sudo install -d -m 0755 /etc/usb-gadget-supervisor/profiles
sudo install -o root -g root -m 0755 build/virtual-trezor-worker \
  /usr/local/libexec/virtual-trezor/virtual-trezor-worker
sudo install -o root -g root -m 0644 profiles/virtual-trezor.toml \
  /etc/usb-gadget-supervisor/profiles/virtual-trezor.toml
```

The profile declares `/dev/i2c-1` as an optional `display-i2c` resource. When
that device node exists, the supervisor opens it before dropping privileges
and the worker mirrors SDL refreshes to an SSD1306-compatible display at
address `0x3c`. No membership in the `i2c` group is required. If the node is
absent, the worker runs with SDL alone.

The checked-in profile records the account used by the validated Pi. Before
starting the service, edit the installed profile's `worker.run_as` value to the
local, unprivileged desktop account that owns the SDL session:

```sh
sudoedit /etc/usb-gadget-supervisor/profiles/virtual-trezor.toml
```

Keep the installed profile and worker root-owned. The supervisor rejects unsafe
profile ownership or paths. Validate the final file before touching the UDC:

```sh
sudo /usr/local/sbin/usb-gadget-supervisor \
  --check-profile \
  --profile /etc/usb-gadget-supervisor/profiles/virtual-trezor.toml
```

## Start and verify

The current SDL worker needs the correct `DISPLAY` and X authentication for
the selected desktop account. For initial validation, launch the supervisor
from that logged-in graphical session while preserving the site's `DISPLAY`
and `XAUTHORITY` environment. For unattended systemd use, configure equivalent
environment values in a local service override; their paths vary across X11
and Xwayland desktops and should not be hard-coded in this repository.

With no competing gadget active, an initial foreground launch is:

```sh
sudo --preserve-env=DISPLAY,XAUTHORITY \
  /usr/local/sbin/usb-gadget-supervisor \
  --profile /etc/usb-gadget-supervisor/profiles/virtual-trezor.toml
```

After a site-specific systemd environment override is installed, start and
inspect the service with:

```sh
sudo systemctl start usb-gadget-supervisor@virtual-trezor.service
systemctl --no-pager --full status \
  usb-gadget-supervisor@virtual-trezor.service
cat /sys/class/udc/*/state
```

The UDC should reach `configured` after attachment to the host. Confirm that
`trezorctl` or the pinned `trezorlib` reports model `1` and firmware `1.14.1`
before testing Trezor Suite. The firmware-integrity warning from Suite is
expected because this Linux worker is not an official signed embedded image.

Use the left/right arrow keys or click the left/right half of the SDL window
for No/Yes confirmation. Stop the service before selecting another profile:

```sh
sudo systemctl stop usb-gadget-supervisor@virtual-trezor.service
```

Do not delete `/var/lib/virtual-trezor` casually: it contains the worker's
simulated persistent state. That state is ordinary software-accessible data,
not hardware-protected storage.
