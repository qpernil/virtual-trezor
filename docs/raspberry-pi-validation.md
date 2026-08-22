# Raspberry Pi Validation

Use this checklist for each Virtual Trezor profile after building the worker
and installing the matching supervisor.

## Build and profile

```sh
make check
make worker
/opt/usb-gadget-supervisor/usb-gadget-supervisor --check-profile \
  --profile /opt/usb-gadget-supervisor/profiles/virtual-trezor.toml
```

Confirm the worker binary has no SDL/X11 dependencies and the selected profile
contains only the intended FunctionFS mount, display resources, and worker
arguments. It must not contain copied USB identity or descriptor blobs.

## USB enumeration and traffic

Start the selected service with a data-capable host connection. Confirm:

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

## Descriptor-capability boundary

Inspect the running worker and confirm:

- it runs as the configured non-root account;
- it holds fixed control FD 3 plus four direction-specific FunctionFS data
  endpoints, with one reader thread and one `eventfd` per OUT endpoint;
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
without changing the worker PID. Separately kill the worker and confirm that
process failure still causes a complete fresh-process cycle. Stopping the
systemd service must perform final teardown without creating another worker.

With the Pi independently powered, put the attached host to sleep and wake it.
The worker PID and USB generation must survive. Accept either a direct
`SUSPEND`/`RESUME` or a wake-time `SUSPEND`/`DISABLE`/`ENABLE`; confirm traffic
continues afterward. Confirm the suspend log reports a completed flash
checkpoint, the display turns off while suspended, and resume redraws the
current framebuffer. The worker should consume no periodic 10 ms wakeups while
suspended, and a firmware deadline that was pending before suspend should have
the same remaining duration after resume. If the host supplies the Pi's only
power, first determine whether that machine and hub retain VBUS during
sleep—VBUS loss cold-boots the Pi and cannot be handled as a USB event.

For ST7789, distinguish the two dark states physically: normal firmware
screensaver blanking leaves the backlight on, while USB suspend turns the
backlight off. Resume powers the backend again even when the firmware
framebuffer remains black.

## Display and buttons

For the selected profile, confirm the genuine 128x64 upstream framebuffer is
visible and both active-low firmware buttons work:

- SH1106 SPI: GPIO24 data/command, GPIO25 reset, SPI0 CE0;
- SSD1306/SH1106 I2C: `/dev/i2c-1`, address `0x3c`;
- ST7789 SPI: GPIO25 data/command, GPIO27 reset, GPIO24 backlight; and
- buttons: GPIO5, GPIO26, with GPIO13 center mapped to both.

Fault-inject a display disconnect. USB must remain operational, and the regular
firmware polling path must reinitialize and retransmit the current framebuffer
after the resource returns.

With no host traffic or button input, leave the home screen idle for the
configured automatic-lock interval. Confirm that the worker remains at low
idle CPU and that the firmware enters its lock/screensaver state without an
external event. This guards the legacy emulator's bounded 10 ms wait contract.

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

- Emulator file storage and development randomness remain software resources.
- The appliance does not provide physical Trezor security or firmware
  authenticity.
