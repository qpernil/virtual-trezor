# Raspberry Pi platform boundary

This directory implements the host-facing functions required by the
unmodified Trezor One firmware logic.

The worker compiles neither upstream datagram transport nor the upstream SDL
display/button implementations. It supplies the complete Raspberry Pi USB,
display, and button boundary while leaving firmware UI composition unchanged.

The current replacement surface is:

| Replacement | Responsibility |
| --- | --- |
| `usb_functionfs.c` | `usbInit`, `usbPoll`, `waitAndProcessUSBRequests`, `usbTiny`, `usbFlush`, and `usbReconnect`; FunctionFS descriptors/endpoints and supervisor lifecycle |
| `buttons_gpio.c` | `buttonRead`; active-low GPIO5 and GPIO26 inputs for No and Yes |
| `display_i2c.c` | `oledInit`, `oledRefresh`, and recovery through `emulatorPoll`; send the upstream framebuffer over an inherited I2C descriptor |
| `ssd1306_stream.c` | Pure construction of SSD1306 initialization, address-window, and 1,025-byte framebuffer messages |
| `sh1106_stream.c` | Pure construction of SH1106 initialization and page-addressed framebuffer messages |
| `worker_main.c`, `worker_config.c` | Parse project-owned worker options before entering the renamed upstream firmware `main` |

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
on a bounded queue signaled by `eventfd`. The firmware thread therefore
continues to refresh the I2C display and sample buttons while host software
waits for an on-device confirmation.

The profile declares `/dev/i2c-1` as the required `display-i2c` resource. The
supervisor opens it while privileged and passes its descriptor as
`USB_GADGET_RESOURCE_DISPLAY_I2C_FD`; the worker selects address `0x3c` and
performs all device-specific transactions after privilege drop. The
`--i2c-display=ssd1306|sh1106` worker argument selects the protocol explicitly;
the controllers cannot be distinguished by address probing because both
normally use `0x3c`.

The required `display-gpio` resource passes `/dev/gpiochip0` as
`USB_GADGET_RESOURCE_DISPLAY_GPIO_FD`. SH1106 initialization requests GPIO25
and performs the vendor reset pulse. The button backend requests GPIO5 and
GPIO26 as pull-up inputs from the same GPIO chip. Missing resources are fatal;
an I2C transfer failure is logged without terminating USB service. The regular
firmware `emulatorPoll` path retries after one second, reinitializes the display,
and retransmits the current framebuffer even when the UI produces no later
refresh.

The original Trezor One OLED transport is SPI. The I2C backend is a Pi
platform adaptation around the unchanged upstream framebuffer, not an attempt
to reproduce the original board-level display wiring. See
[`../../docs/i2c-display-plan.md`](../../docs/i2c-display-plan.md).
