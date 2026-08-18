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
| `display_linux.c` | `oledInit`, `oledRefresh`, and recovery through `emulatorPoll`; send the upstream framebuffer over inherited I2C or SPI descriptors |
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

The I2C profile declares `/dev/i2c-1` as its required `display-i2c` resource.
The supervisor opens it while privileged and passes its descriptor as
`USB_GADGET_RESOURCE_DISPLAY_I2C_FD`; the worker selects address `0x3c` and
performs all device-specific transactions after privilege drop. The SPI
profile similarly exports `/dev/spidev0.0` as
`USB_GADGET_RESOURCE_DISPLAY_SPI_FD`. The
worker defaults to SH1106 SPI. The
`--display=ssd1306-i2c|sh1106-i2c|sh1106-spi` option overrides the backend;
the two I2C controllers cannot be distinguished by probing because both
normally use `0x3c`.

The required `display-gpio` resource passes `/dev/gpiochip0` as
`USB_GADGET_RESOURCE_DISPLAY_GPIO_FD`. SH1106 initialization requests GPIO25
and performs the vendor reset pulse. SPI mode also requests GPIO24 to select
command or framebuffer data, configures SPI mode 0 at 4 MHz, and lets SPI0 CE0
drive chip select. The button backend requests GPIO5 and GPIO26 as pull-up
inputs from the same GPIO chip. Missing resources are fatal; a transfer failure
is logged without terminating USB service. The regular firmware `emulatorPoll`
path retries after one second, reinitializes the display, and retransmits the
current framebuffer even when the UI produces no later refresh.

The original Trezor One OLED transport is SPI. The SH1106 SPI HAT is not the
original display controller, but it preserves the four-wire transport style.
The I2C backend remains a Pi platform adaptation around the same unchanged
upstream framebuffer. See
[`../../docs/i2c-display-plan.md`](../../docs/i2c-display-plan.md).
