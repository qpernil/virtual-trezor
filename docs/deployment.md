# Raspberry Pi deployment

## Scope

This guide deploys the headless FunctionFS/I2C/GPIO worker on a 64-bit
Raspberry Pi Linux system. The worker is experimental and must use only
disposable emulator state and test seeds. It requires a USB Device Controller,
an I2C controller, and a GPIO chip; it does not require a desktop session.

The tested architecture has two separately installed projects:

- `usb-gadget-supervisor` owns ConfigFS, FunctionFS, the UDC, privilege drop,
  and cleanup as root;
- `virtual-trezor-worker` runs the firmware integration and endpoint/UI logic
  as the unprivileged user named by the profile.

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

Install the normal C and package-config development dependencies. On
Debian-family systems the base packages are:

```sh
sudo apt install build-essential git pkg-config
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

The profile declares `/dev/i2c-1` and `/dev/gpiochip0` as required resources.
The supervisor opens them before dropping privileges and passes inherited file
descriptors to the worker, so no `i2c` or `gpio` group membership is required.
The worker exits rather than silently running without its display or buttons
if either resource is missing.

Select the controller in the installed profile; do not rely on address
probing, because both controllers normally use `0x3c`:

```toml
[worker]
arguments = ["--i2c-display=sh1106"] # use ssd1306 for that controller family
```

The Waveshare 1.3-inch OLED HAT uses SH1106 and GPIO25 reset. With
`--i2c-display=sh1106`, the worker requests GPIO25 and pulses reset before
sending the initialization sequence. Firmware No/Yes input uses active-low
GPIO5 and GPIO26 with pull-ups. These can connect to physical buttons or to the
button outputs of the second-Pi virtual-display client.

The checked-in profile records the account used by the validated Pi. Before
starting the service, edit the installed profile's `worker.run_as` value to the
local, unprivileged account that should own the worker process:

```sh
sudoedit /etc/usb-gadget-supervisor/profiles/virtual-trezor.toml
```

The current supervisor service template reads profiles from `/etc`. Confirm
the path on an older installation with:

```sh
systemctl cat usb-gadget-supervisor@.service
```

If its `ExecStart` still names an earlier `/opt/usb-gadget-supervisor/profiles`
layout, update that supervisor installation or place the root-owned profile at
the path actually named by the unit before validation.

Keep the installed profile and worker root-owned. The supervisor rejects unsafe
profile ownership or paths. Validate the final file before touching the UDC:

```sh
sudo /usr/local/sbin/usb-gadget-supervisor \
  --check-profile \
  --profile /etc/usb-gadget-supervisor/profiles/virtual-trezor.toml
```

## Optional second-Pi display and button bridge

A physical OLED and buttons may be replaced during development by the generic
`virtual-display` client from
[`raspberry-pi-i2c-target`](https://github.com/qpernil/raspberry-pi-i2c-target).
Run it from a graphical session on a Pi 3/3B+ target before starting the
worker, so address `0x3c` acknowledges the worker's initialization writes:

```sh
sudo -E ./prebuilt/aarch64/virtual-display \
  --display=sh1106 \
  --title "Virtual Trezor display" \
  --button-outputs=5,26 \
  0x3c ./kernel
```

The 40-pin wiring is the same on the tested Pi 4 controller and Pi 3B+ target:

| Signal | Worker/controller Pi | Viewer/target Pi |
| --- | --- | --- |
| SDA | GPIO2, physical pin 3 | GPIO2, physical pin 3 |
| SCL | GPIO3, physical pin 5 | GPIO3, physical pin 5 |
| Ground | physical pin 6 | physical pin 6 |
| No/left | GPIO5, physical pin 29, input | GPIO5, physical pin 29, open-drain output |
| Yes/right | GPIO26, physical pin 37, input | GPIO26, physical pin 37, open-drain output |

Do not connect the boards' power rails. Holding the viewer's left, middle, or
right third drives No, both buttons, or Yes for as long as the mouse button
remains down. The client owns the target driver and unloads it when the client
exits, so do not run `target-driver` at the same time.

## Start and verify

The worker has no graphical-session dependency. With no competing gadget
active, an initial foreground launch is:

```sh
sudo /usr/local/sbin/usb-gadget-supervisor \
  --profile /etc/usb-gadget-supervisor/profiles/virtual-trezor.toml
```

Start and inspect the systemd service with:

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

Use physical active-low buttons on GPIO5/GPIO26 or the remote virtual-display
client for No/Yes confirmation. Stop the service before selecting another
profile:

```sh
sudo systemctl stop usb-gadget-supervisor@virtual-trezor.service
```

Do not delete `/var/lib/virtual-trezor` casually: it contains the worker's
simulated persistent state. That state is ordinary software-accessible data,
not hardware-protected storage.
