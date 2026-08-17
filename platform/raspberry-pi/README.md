# Raspberry Pi platform boundary

This directory implements the host-facing functions required by the
unmodified Trezor One firmware logic.

The initial worker must not compile either upstream datagram transport
implementation. It keeps the upstream SDL display and button objects so USB
compatibility can be tested independently of the hardware UI.

The initial replacement surface is:

| Replacement | Responsibility |
| --- | --- |
| `usb_functionfs.c` | `usbInit`, `usbPoll`, `waitAndProcessUSBRequests`, `usbTiny`, `usbFlush`, and `usbReconnect`; FunctionFS descriptors/endpoints and supervisor lifecycle |

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

The SDL/X11 setup disables SDL's optional XRandR integration. The Labwc
Xwayland server can expose a stale output during display changes, causing SDL's
asynchronous `RRGetOutputInfo` request to terminate an otherwise healthy
worker. The fixed-size emulator window does not need XRandR.

After the FunctionFS device works with `trezorctl` and Trezor Suite, the
hardware-UI milestone adds:

| Replacement | Responsibility |
| --- | --- |
| `display.c` | `oledInit` and `oledRefresh`; send the framebuffer returned by upstream `oledGetBuffer` to a 128x64 OLED |
| `buttons.c` | `buttonRead` using two GPIO inputs; upstream `buttonUpdate` retains debounce and event semantics |

The supervisor may open I2C/GPIO character devices while privileged and pass
their descriptors to the worker. All device-specific transactions and policy
remain here in the unprivileged process.
