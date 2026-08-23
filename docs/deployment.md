# Raspberry Pi deployment

## Scope

This guide deploys the headless FunctionFS/I2C/SPI/GPIO worker on a Raspberry
Pi running either 64-bit Ubuntu or 64-bit Raspberry Pi OS; both are tested.
Enabling DWC2 in peripheral mode provides the UDC used by the supervisor
through ConfigFS and FunctionFS. The worker is experimental and must use only
disposable emulator state and test seeds. It also requires the selected I2C or
SPI display bus and exact supervisor-owned GPIO line groups; it does not
require a desktop session.

The tested architecture has three separately installed projects:

- `usb-gadget-supervisor` owns ConfigFS, FunctionFS, the UDC, privilege drop,
  and cleanup as root;
- `virtual-trezor-worker` runs the firmware integration and endpoint/UI logic
  as the unprivileged user named by the profile.
- `display-backends` is linked into the worker and owns controller setup,
  framebuffer conversion, and bus/GPIO operations.

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
sudo apt install build-essential git pkg-config python3-dev libffi-dev rustup
sudo snap install astral-uv --classic
rustup set profile minimal
rustup toolchain install 1.85.0
rustup default 1.85.0
```

The build uses `uv tool run` with pinned `grpcio-tools==1.81.0`, which provides
`libprotoc 33.5` matching the upstream locked Python protobuf 6.33.5 runtime.
No separately installed or copied `protoc` executable is required. The build
puts the checked-in `tools/protoc` launcher first on `PATH`, because the
pinned upstream Makefile invokes that fixed command name. The launcher uses
the pinned package above, and the build checks its reported compiler version
so generated messages cannot silently drift from the pinned release. It also
feature-detects the narrow warning compatibility flag required by GCC 15 for
upstream fixed-size byte arrays.

Clone and initialize only the selected Trezor One dependencies:

```sh
git clone https://github.com/qpernil/display-backends.git
git clone https://github.com/qpernil/usb-gadget-supervisor.git
git clone https://github.com/qpernil/virtual-trezor.git
cd virtual-trezor
make init
make check
make worker
```

Keep all three repositories as siblings. The worker links the current
`display-backends` native library and the supervisor repository's shared
`usb-gadget-worker` discovery/CBOR static library. The resulting executable is
`build/virtual-trezor-worker`.

## Install the supervisor and worker

Build and install
[`usb-gadget-supervisor`](https://github.com/qpernil/usb-gadget-supervisor)
first, including its binary, profiles directory, and template systemd unit
under `/opt/usb-gadget-supervisor` as described in that project's README.

The worker runs directly from this repository's build directory. Install the
root-owned default profile for the factory-configured SH1106 SPI HAT:

```sh
cp profiles/virtual-trezor.toml /tmp/virtual-trezor.toml
editor /tmp/virtual-trezor.toml
sudo install -o root -g root -m 0644 /tmp/virtual-trezor.toml \
  /opt/usb-gadget-supervisor/profiles/virtual-trezor.toml
```

Install the explicitly named I2C profile when driving the second-Pi virtual
display or an I2C-native module:

```sh
cp profiles/virtual-trezor-i2c.toml /tmp/virtual-trezor-i2c.toml
editor /tmp/virtual-trezor-i2c.toml
sudo install -o root -g root -m 0644 /tmp/virtual-trezor-i2c.toml \
  /opt/usb-gadget-supervisor/profiles/virtual-trezor-i2c.toml
```

Install the ST7789 profile for the Waveshare 1.3-inch 240x240 LCD HAT:

```sh
cp profiles/virtual-trezor-st7789.toml /tmp/virtual-trezor-st7789.toml
editor /tmp/virtual-trezor-st7789.toml
sudo install -o root -g root -m 0644 /tmp/virtual-trezor-st7789.toml \
  /opt/usb-gadget-supervisor/profiles/virtual-trezor-st7789.toml
```

The resulting choices are:

| Supervisor instance | Display path | Worker argument |
| --- | --- | --- |
| `virtual-trezor` | Direct SH1106 over SPI0 | none; `sh1106-spi` is the default |
| `virtual-trezor-i2c` | SH1106 over I2C1, including the second-Pi viewer | `--display=sh1106-i2c` |
| `virtual-trezor-st7789` | Direct 240x240 ST7789 LCD over SPI0 | `--display=st7789-spi` |

The default SPI profile declares `/dev/spidev0.0`; the I2C profile declares
`/dev/i2c-1`. Both also declare one display-control output group and one button
input/event group on `/dev/gpiochip0`. The supervisor opens the bus, claims the
exact GPIO offsets, and passes the three resulting handles before dropping
privileges. It does not pass the GPIO-chip descriptor, so the worker can neither
select different offsets nor change line configuration. No `i2c`, `spi`, or
`gpio` group membership is required. The worker exits rather than silently
running without its display or buttons if a required resource is missing.

The worker defaults to `sh1106-spi`. Override it in an alternate profile when
needed; do not rely on I2C address probing, because both I2C controllers
normally use `0x3c`:

```toml
[worker]
arguments = ["--display=sh1106-i2c"]
```

The available override values are `ssd1306-i2c`, `sh1106-i2c`,
`sh1106-spi`, and `st7789-spi`.
The SH1106 profiles place GPIO25 reset in the output handle, with SPI also
placing GPIO24 Data/Command first. The worker pulses reset before initialization
and SPI0 CE0 supplies chip select. The button handle contains active-low GPIO5,
GPIO26, and GPIO13 with pull-ups and both-edge detection. The kernel converts
them to logical pressed bits; GPIO13 maps the physical HAT's joystick press to
both logical buttons. GPIO5/GPIO26 can instead connect to the button outputs of
the second-Pi virtual-display client.

The checked-in profiles use neutral placeholders. The temporary copies above
must set `worker.command` to the built worker's absolute path and
`worker.run_as` to its unprivileged service account. Keep machine-local paths
out of the checked-in templates.

Confirm that the installed service template uses the same `/opt` layout:

```sh
systemctl cat usb-gadget-supervisor@.service
```

Keep the installed profile root-owned. The worker may be owned by `run_as`, but
the supervisor rejects unsafe ownership or paths. Validate the final file
before touching the UDC:

```sh
sudo /opt/usb-gadget-supervisor/usb-gadget-supervisor \
  --check-profile \
  --profile /opt/usb-gadget-supervisor/profiles/virtual-trezor.toml
sudo /opt/usb-gadget-supervisor/usb-gadget-supervisor \
  --check-profile \
  --profile /opt/usb-gadget-supervisor/profiles/virtual-trezor-i2c.toml
sudo /opt/usb-gadget-supervisor/usb-gadget-supervisor \
  --check-profile \
  --profile /opt/usb-gadget-supervisor/profiles/virtual-trezor-st7789.toml
```

## Physical SH1106 SPI HAT

The Waveshare 1.3-inch OLED + Joystick HAT ships in four-wire SPI mode, so it
needs no resistor changes for the SPI backend. Enable SPI0, reboot if the
setting changed, and verify its device node before starting the worker:

```sh
sudo raspi-config nonint do_spi 0
ls -l /dev/spidev0.0
```

SPI0 and I2C1 may remain enabled simultaneously; they use different pins on
the worker Pi. The selected supervisor profile determines which bus the worker
opens.

The factory HAT mapping is:

| Function | BCM GPIO | Physical pin |
| --- | ---: | ---: |
| SPI0 MOSI | 10 | 19 |
| SPI0 SCLK | 11 | 23 |
| SPI0 CE0 | 8 | 24 |
| Data/Command | 24 | 18 |
| Reset | 25 | 22 |
| No/left | 5 | 29 |
| Yes/right | 26 | 37 |
| Both/joystick press | 13 | 33 |

No/left and Yes/right mean the HAT joystick directions. Pressing the joystick
straight down reports both logical Trezor buttons simultaneously. The separate
KEY1, KEY2, and KEY3 switches use GPIO21, GPIO20, and GPIO16 and are not
sampled.

The worker configures mode 0 at 4 MHz. Power down before fitting or removing
the HAT. Stop the I2C virtual-display setup and disconnect its inter-Pi wiring
before installing the physical board.

## Physical ST7789 SPI HAT

The Waveshare 1.3-inch LCD + Joystick HAT uses a 240x240 RGB565 ST7789 panel.
The legacy firmware still composes its genuine 128x64 monochrome framebuffer;
the platform backend decodes that layout, scales it by 1.875 in both axes, and
centers the resulting 240x120 image between 60-pixel black bands. It does not
alter the upstream UI code.

The HAT shares SPI0 and the joystick GPIOs with the OLED HAT, but its control
pins differ:

| Function | BCM GPIO | Physical pin |
| --- | ---: | ---: |
| SPI0 MOSI | 10 | 19 |
| SPI0 SCLK | 11 | 23 |
| SPI0 CE0 | 8 | 24 |
| Data/Command | 25 | 22 |
| Reset | 27 | 13 |
| Backlight | 24 | 18 |
| No/left | 5 | 29 |
| Yes/right | 26 | 37 |
| Both/joystick press | 13 | 33 |

The backend uses SPI mode 0 at 62.5 MHz. To test only the panel wiring and the
actual shared backend, without firmware, FunctionFS, or the supervisor, stop
any gadget worker and run from the sibling `display-backends` checkout:

```sh
cargo build --release --locked --features demo --bin display-backends-demo
sudo ./target/release/display-backends-demo st7789-spi
```

The demo initializes the panel through the same library used by the worker,
renders and transfers 240 frames, reports the measured rate, and waits for
Enter before clearing and releasing its GPIO lines. This is a direct hardware
test, so it normally requires root unless local SPI/GPIO permissions have been
configured. Optional SPI and GPIO-chip paths may follow the backend name.

## Optional second-Pi display and button bridge

A physical OLED and buttons may be replaced during development by the generic
`virtual-display` client from
[`raspberry-pi-i2c-target`](https://github.com/qpernil/raspberry-pi-i2c-target).
Run it from a graphical session on a Pi 3/3B+ target before starting the
worker, so address `0x3c` acknowledges the worker's initialization writes:

```sh
sudo -E ./prebuilt/aarch64/virtual-display \
  --title "Virtual Trezor display" \
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
active, an initial foreground SPI launch is:

```sh
sudo /opt/usb-gadget-supervisor/usb-gadget-supervisor \
  --profile /opt/usb-gadget-supervisor/profiles/virtual-trezor.toml
```

For a foreground virtual-display launch, substitute
`virtual-trezor-i2c.toml` in that command.

Start and inspect the systemd service with:

```sh
sudo systemctl start usb-gadget-supervisor@virtual-trezor.service
systemctl --no-pager --full status \
  usb-gadget-supervisor@virtual-trezor.service
cat /sys/class/udc/*/state
```

For the I2C profile, the instance is
`usb-gadget-supervisor@virtual-trezor-i2c.service`. Never run the I2C and SPI
instances together; they share the USB identity, FunctionFS mount, GPIOs, and
persistent state.

Switch from the direct SPI HAT to the I2C/SDL path with:

```sh
sudo systemctl stop usb-gadget-supervisor@virtual-trezor.service
sudo systemctl start usb-gadget-supervisor@virtual-trezor-i2c.service
```

Switch back to the default SPI path with:

```sh
sudo systemctl stop usb-gadget-supervisor@virtual-trezor-i2c.service
sudo systemctl start usb-gadget-supervisor@virtual-trezor.service
```

If using `systemctl enable`, enable only the profile that should claim the UDC
at boot, and ensure Virtual YubiKey or other gadget instances are disabled.

The UDC should reach `configured` after attachment to the host. Confirm that
`trezorctl` or the pinned `trezorlib` reports model `1` and firmware `1.14.1`
before testing Trezor Suite. The firmware-integrity warning from Suite is
expected because this Linux worker is not an official signed embedded image.
Production Suite may refuse transaction operations after that failed check;
this is an intentional safety boundary, not a transport failure. Use testnet
or regtest with development tooling for signing-flow experiments, and never
place a valuable seed in the worker.

Leave the device on its home screen without host traffic for the configured
automatic-lock interval. The worker should remain at low idle CPU while the
firmware still enters its lock/screensaver state on schedule. USB, GPIO, and
supervisor events wake the worker early; otherwise its main loop returns at the
upstream emulator's normal 10 ms deadline.

Use the HAT joystick on GPIO5/GPIO26/GPIO13 or the remote virtual-display
client for No/Yes/both input. Stop the service before selecting another
profile:

```sh
sudo systemctl stop usb-gadget-supervisor@virtual-trezor.service
```

Do not delete `/var/lib/virtual-trezor` casually: it contains the worker's
simulated persistent state. That state is ordinary software-accessible data,
not hardware-protected storage.
