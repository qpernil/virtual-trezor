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
contains the intended USB identity, FunctionFS blobs, display resource nodes,
and worker arguments.

## USB enumeration and traffic

Start the selected service with a data-capable host connection. Confirm:

- UDC state becomes `configured`;
- the host sees full-speed `1209:53c1`, product `Virtual Trezor`, and the
  selected serial;
- the main interface exposes 64-byte interrupt OUT and IN endpoints;
- `trezorctl` discovers the device and reads model `1` firmware features;
- a multi-packet ping round-trips unchanged; and
- Trezor Suite reaches its expected firmware-check and confirmation workflow.

The unsigned Linux worker is not authenticated production firmware, so host
software may deliberately reject protected wallet operations.

## Descriptor-capability boundary

Inspect the running worker and confirm:

- it runs as the configured non-root account;
- it holds fixed control FD 3 plus FunctionFS `ep0`, OUT, and IN descriptors;
- it has no descriptor-number or device-path environment variables and never
  opens `/dev/ffs-*`;
- `ep0` `ENABLE`, `DISABLE`, `UNBIND`, and `SETUP` events control runtime state;
- I2C/SPI/GPIO access exists only through profile-approved pre-bind FDs; and
- FunctionFS remains root-owned.

## Incarnation recovery

Send `SIGKILL` to the worker without stopping the supervisor. The same
supervisor process must unbind, remove the old gadget and mount, start a fresh
worker with fresh FDs, rebind, and return the UDC to `configured`.

Exercise `usbReconnect()` and confirm it produces the same complete
fresh-process cycle. Stopping the systemd service must instead perform final
teardown without creating another incarnation.

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
report device release `1.01`, distinct from the earlier `1.00` descriptor set.
Increment `bcd_device` again whenever Microsoft OS descriptors change in a way
that requires Windows to enumerate them anew.

## Known limitations

- Only the main vendor interface is exposed; the separate U2F HID interface is
  not present.
- Emulator file storage and development randomness remain software resources.
- The appliance does not provide physical Trezor security or firmware
  authenticity.
