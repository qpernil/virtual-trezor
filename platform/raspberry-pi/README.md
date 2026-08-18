# Raspberry Pi platform boundary

This directory implements the host-facing functions required by the
unmodified Trezor One firmware logic.

The initial worker must not compile either upstream datagram transport
implementation. It keeps the upstream SDL display so USB compatibility can be
tested independently of the hardware UI, and supplies practical SDL controls
through the same narrow `buttonRead` platform boundary used by future GPIO.

The current replacement surface is:

| Replacement | Responsibility |
| --- | --- |
| `usb_functionfs.c` | `usbInit`, `usbPoll`, `waitAndProcessUSBRequests`, `usbTiny`, `usbFlush`, and `usbReconnect`; FunctionFS descriptors/endpoints and supervisor lifecycle |
| `buttons_sdl.c` | `buttonRead`; keyboard plus clickable left and right display halves for No and Yes |
| `display_i2c_sdl.c` | Transitional `oledInit`, `oledRefresh`, and `emulatorPoll`; call the renamed upstream SDL renderer and optionally mirror the framebuffer over an inherited I2C descriptor |
| `ssd1306_stream.c` | Pure construction of SSD1306 initialization, address-window, and 1,025-byte framebuffer messages |

`usb_functionfs.c` is compiled as the upstream firmware's expected `udp.o`. It
publishes one vendor-specific main interface with 64-byte interrupt IN and OUT
endpoints. DebugLink is disabled. The separate U2F HID interface is deferred
until the main Trezor/Suite transport is validated.

The implementation intentionally processes one FunctionFS OUT packet per poll
cycle. FunctionFS endpoint reads can block when a second packet is not queued,
even when the endpoint was opened with `O_NONBLOCK`; returning to the firmware
loop after each packet ensures generated replies are flushed immediately.

FunctionFS interface state is tracked separately from the supervisor's UDC
binding state. `ENABLE`, `DISABLE`, `RESUME`, and `UNBIND` events from `ep0`
control whether data endpoints are polled. This lets host software close and
reopen the USB interface without stranding the worker or causing a busy loop.

The Pi FunctionFS driver can block a synchronous OUT endpoint read even when
the file was opened with `O_NONBLOCK` and `poll` reported it readable. A small
reader thread owns that blocking syscall and places complete 64-byte packets
on a bounded queue signaled by `eventfd`. The firmware/UI thread therefore
continues to refresh SDL and sample buttons while host software waits for an
on-device confirmation.

The SDL/X11 setup disables SDL's optional XRandR integration. The Labwc
Xwayland server can expose a stale output during display changes, causing SDL's
asynchronous `RRGetOutputInfo` request to terminate an otherwise healthy
worker. The fixed-size emulator window does not need XRandR.

The remaining hardware-UI milestones add:

| Replacement | Responsibility |
| --- | --- |
| `display.c` | Final non-SDL `oledInit`, `oledRefresh`, and poll hook for the physical 128x64 OLED |
| `buttons.c` | `buttonRead` using two GPIO inputs; upstream `buttonUpdate` retains debounce and event semantics |

The profile declares `/dev/i2c-1` as the optional `display-i2c` resource. The
supervisor opens it while privileged and passes its descriptor as
`USB_GADGET_RESOURCE_DISPLAY_I2C_FD`; the worker selects address `0x3c` and
performs all device-specific transactions after privilege drop. Missing or
failed I2C remains non-fatal and SDL stays authoritative.

The original Trezor One OLED transport is SPI. The I2C backend is a Pi
platform adaptation around the unchanged upstream framebuffer, not an attempt
to reproduce the original board-level display wiring. See
[`../../docs/i2c-display-plan.md`](../../docs/i2c-display-plan.md).
