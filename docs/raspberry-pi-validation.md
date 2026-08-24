# Raspberry Pi Validation

Use this checklist for each Virtual Trezor profile after building the worker
and installing the matching supervisor.

## Build and profile

```sh
make check
make worker
make safe3-usb
/opt/usb-gadget-supervisor/usb-gadget-supervisor --check-profile \
  --profile /opt/usb-gadget-supervisor/profiles/virtual-trezor.toml
/opt/usb-gadget-supervisor/usb-gadget-supervisor --check-profile \
  --profile /opt/usb-gadget-supervisor/profiles/virtual-trezor-safe3.toml
```

Confirm the worker binary has no SDL/X11 dependencies and the selected profile
contains only the intended FunctionFS mount, display resources, and worker
arguments. It must not contain copied USB identity or descriptor blobs.

## USB enumeration and traffic

Start the selected service with a data-capable host connection. For Trezor
One, confirm:

- UDC state becomes `configured`;
- the host sees the firmware-reported full-speed `1209:53c1`, manufacturer
  `SatoshiLabs`, product `TREZOR`, and configuration-derived serial;
- the main interface and U2F HID interface each expose 64-byte interrupt OUT
  and IN endpoints;
- the service log contains the decoded personality obtained from the genuine
  firmware, and its exact CBOR is saved under `/run/usb-gadget-supervisor`;
- `trezorctl` discovers the device and reads model `1` firmware features;
- a multi-packet ping round-trips unchanged; and
- Trezor Suite reaches its expected firmware-check and confirmation workflow.

The unsigned Linux worker is not authenticated production firmware, so host
software may deliberately reject protected wallet operations.

For Safe 3, confirm:

- the host sees full-speed `1209:53c1`, manufacturer `Trezor Company`, product
  `Trezor Safe 3`, and the firmware-generated serial;
- the main vendor/WebUSB and FIDO HID interfaces each expose their genuine
  interrupt OUT and IN endpoints;
- interface zero advertises the Microsoft OS 1.0 `WINUSB` compatible ID and
  the BOS includes WebUSB 1.0 without a landing page;
- `trezorctl` reports `T3B1`/Safe 3 firmware features and normal protocol
  requests complete; and
- Trezor Suite in **debug mode** accepts the upstream emulator firmware as
  genuine and exposes the normal emulator UI workflows.

Suite debug mode is required because it selects Suite's emulator trust path.
The transport can still be this physical FunctionFS USB gadget; it does not
need to be the upstream UDP emulator. Production mode may apply production
firmware-authenticity policy and reject protected operations.

## Descriptor-capability boundary

Inspect the running worker and confirm:

- it runs as the configured non-root account;
- it holds fixed control FD 3 plus four direction-specific FunctionFS data
  endpoints, with one helper thread and one `eventfd` per endpoint;
- it has no descriptor-number or device-path environment variables and never
  opens `/dev/ffs-*`;
- supervisor `BIND`, `ENABLE`, `DISABLE`, `UNBIND`, `SUSPEND`, `RESUME`, and
  control-request records drive runtime USB state;
- I2C/SPI/GPIO access exists only through profile-approved pre-bind FDs; and
- FunctionFS remains root-owned.

## USB reconfiguration and process recovery

Send `SIGKILL` to the worker without stopping the supervisor. The same
supervisor process must unbind, remove the old gadget and mount, start a fresh
worker with fresh FDs, rebind, and return the UDC to `configured`.

Exercise `usbReconnect()` and confirm the supervisor asks the existing worker
to quiesce, replaces FunctionFS and the OUT/IN endpoint files, unbinds and
rebinds the UDC, and re-enumerates
without changing the worker PID. Confirm the UDC remains detached for at least
250 ms before replacement bind. Separately kill the worker and confirm that
process failure still causes a complete fresh-process cycle. Stopping the
systemd service must perform final teardown without creating another worker.

With the Pi independently powered, put the attached host to sleep and wake it.
The worker PID and USB generation must survive. Accept either a direct
`SUSPEND`/`RESUME` or a wake-time `SUSPEND`/`DISABLE`/`ENABLE`; confirm traffic
continues afterward. For legacy Trezor, confirm the suspend log reports a
completed flash checkpoint, the display turns off, and resume redraws the
current framebuffer. Its worker should consume no periodic 10 ms wakeups while
suspended, and a pending firmware deadline should retain the same remaining
duration after resume. For Safe 3, genuine Core owns display policy; confirm
endpoint traffic pauses and resumes while the panel remains available.
If the host supplies the Pi's only power, first determine whether that machine
and hub retain VBUS during sleep—VBUS loss cold-boots the Pi and cannot be
handled as a USB event.

For legacy ST7789, distinguish the two dark states physically: normal firmware
screensaver blanking leaves the backlight on, while USB suspend turns the
backlight off. Resume powers the backend again even when the firmware
framebuffer remains black. Safe 3 screensaver blanking likewise leaves its
backlight on, but USB suspend does not currently alter it.

## Display and buttons

For the selected profile, confirm the genuine 128x64 upstream framebuffer is
visible and both active-low firmware buttons work:

- SH1106 SPI: GPIO24 data/command, GPIO25 reset, SPI0 CE0;
- SSD1306/SH1106 I2C: `/dev/i2c-1`, address `0x3c`;
- ST7789 SPI: GPIO25 data/command, GPIO27 reset, GPIO24 backlight; and
- buttons: GPIO5, GPIO26, with GPIO13 center mapped to both.

For Safe 3, hold display-HAT KEY3 (GPIO16) and confirm the host loses the USB
device while the worker PID remains alive. It must stay absent for the complete
hold, then enumerate as a fresh generation immediately after release. Repeat
with short and long holds and confirm handles for the retired generation cannot
transfer.

Fault-inject a display disconnect. USB must remain operational, and the regular
firmware polling path must reinitialize and retransmit the current framebuffer
after the resource returns.

With no host traffic or button input, leave the home screen idle for the
configured automatic-lock interval. Confirm that the worker remains at low
idle CPU and that the firmware enters its lock/screensaver state without an
external event. This guards the legacy emulator's bounded 10 ms wait contract.

For Safe 3, also observe the worker after initialization has settled. It should
remain near 0–0.2% of one Pi 4 core (well below 0.5%). A trace must show
blocking `ppoll()` wakes for control, endpoint, GPIO, or firmware deadlines and
no periodic one-millisecond sleep loop. Confirm automatic lock and UI timers
still expire on schedule, then suspend the USB host and confirm suspended time
does not advance those firmware timers.

For an upstream comparison, build `make safe3-baseline`, run the unmodified
SDL/UDP artifact from `core/src`, and sample only after it reaches the home
screen. The validated Pi 4 run used SDL's dummy video driver in a headless SSH
session and averaged 9.85% of one core over ten two-second `pidstat` samples
(4.50% user, 5.35% kernel). This confirms the upstream emulator's persistent
idle load; it is not a single-variable comparison because the upstream
baseline also retains SDL and UDP.

On a fresh Windows device instance, confirm that interface zero binds to
Microsoft's inbox WinUSB driver without installing an INF. Then inspect the BOS
descriptor and confirm the WebUSB 1.0 platform capability uses vendor request
code `0x01` and advertises no landing page. Windows may cache an earlier
driverless result under the VID/PID/device-release tuple and will not repeat a
failed Microsoft OS descriptor probe. The WinUSB-capable profiles therefore
preserve the firmware's device release `1.00`; clear any stale host-side device
instance when validating the newly discovered descriptor set. USB metadata is
not overridden by the profile or virtual controller.

## Known limitations

- File storage and Linux kernel entropy remain software resources without
  genuine-device physical isolation.
- The appliance does not provide physical Trezor security or firmware
  authenticity.
- Safe 3 is physically validated on ST7789. Its SH1106 SPI/I2C and SSD1306 I2C
  profiles build and pass schema validation but still require final validation
  on their physical panels.
