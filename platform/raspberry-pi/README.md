# Raspberry Pi platform boundary

This directory implements the host-facing functions required by the
unmodified Trezor One firmware logic.

The worker compiles neither upstream datagram transport nor the upstream SDL
display/button implementations. It supplies the complete Raspberry Pi USB,
display, and button boundary while leaving firmware UI composition unchanged.

The current replacement surface is:

| Replacement | Responsibility |
| --- | --- |
| `usb_functionfs.c` | `usbInit`, `usbPoll`, `waitAndProcessUSBRequests`, `usbTiny`, `usbFlush`, and `usbReconnect`; inherited FunctionFS endpoints, `ep0` events, and supervisor liveness |
| `buttons_gpio.c` | `buttonRead`; active-low GPIO5/GPIO26 inputs for No/Yes and GPIO13 center mapped to both |
| `display_linux.c` | `oledInit`, `oledRefresh`, recovery through `emulatorPoll`, and normal-exit clearing; send the upstream framebuffer over inherited I2C or SPI descriptors |
| `ssd1306_stream.c` | Pure construction of SSD1306 initialization, address-window, and 1,025-byte framebuffer messages |
| `sh1106_stream.c` | Pure construction of SH1106 initialization and page-addressed framebuffer messages |
| `st7789.c` | ST7789 initialization, address-window encoding, and conversion of the legacy reverse-page framebuffer to centered 240x120 RGB565 pixels |
| `worker_main.c`, `worker_config.c` | Parse project-owned worker options before entering the renamed upstream firmware `main` |

`usb_functionfs.c` is compiled as the upstream firmware's expected `udp.o`.
The installed profile declares one vendor-specific main interface with 64-byte
interrupt IN and OUT endpoints. The supervisor publishes it and transfers
`ep0`, OUT, and IN in a fixed pre-bind bundle; the worker never opens the
FunctionFS mount. DebugLink is disabled. The separate U2F HID interface is
deferred until the main Trezor/Suite transport is validated.

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
The supervisor opens it while privileged and appends its descriptor to the
pre-bind bundle; the worker selects address `0x3c` and performs all
device-specific transactions after privilege drop. The SPI profile places
`/dev/spidev0.0` in the same fixed slot. The worker defaults to SH1106 SPI. The
`--display=ssd1306-i2c|sh1106-i2c|sh1106-spi|st7789-spi` option overrides the backend;
the two I2C controllers cannot be distinguished by probing because both
normally use `0x3c`.

The required `display-gpio` resource places `/dev/gpiochip0` in the next
pre-bind slot. SH1106 initialization requests GPIO25 and performs the vendor
reset pulse. SPI mode also requests GPIO24 to select
command or framebuffer data, configures SPI mode 0 at 4 MHz, and lets SPI0 CE0
drive chip select. The button backend requests GPIO5, GPIO26, and GPIO13 as
pull-up inputs from the same GPIO chip. GPIO13 is the HAT joystick press and
reports both logical Trezor buttons. Missing resources are fatal; a transfer failure
is logged without terminating USB service. The regular firmware `emulatorPoll`
path retries after one second, reinitializes the display, and retransmits the
current framebuffer even when the UI produces no later refresh.

On an orderly exit the worker blanks display RAM and turns the panel output
off. This covers service stops and requested USB reincarnations. `SIGKILL`
cannot run process cleanup; the replacement worker clears the panel again as
part of display initialization.

The ST7789 backend requests GPIO25 Data/Command, GPIO27 reset, and GPIO24
backlight, then uses SPI mode 0 at 62.5 MHz. It clears the native 240x240 panel
once and updates only a centered 240x120 RGB565 window. Trezor's own renderer
still composes the original 128x64 1-bit framebuffer.

The original Trezor One OLED transport is SPI. The SH1106 SPI HAT is not the
original display controller, but it preserves the four-wire transport style.
The I2C backend remains a Pi platform adaptation around the same unchanged
upstream framebuffer. See
[`../../docs/i2c-display-plan.md`](../../docs/i2c-display-plan.md).
