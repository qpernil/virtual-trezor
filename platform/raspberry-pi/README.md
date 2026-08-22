# Raspberry Pi platform boundary

This directory implements the host-facing functions required by the
unmodified Trezor One firmware logic.

The worker compiles neither upstream datagram transport nor the upstream SDL
display/button implementations. It supplies the complete Raspberry Pi USB,
display, and button boundary while leaving firmware UI composition unchanged.

The current replacement surface is:

| Replacement | Responsibility |
| --- | --- |
| `usb_functionfs.c` | `usbInit`, `usbPoll`, `waitAndProcessUSBRequests`, `usbTiny`, `usbFlush`, and `usbReconnect`; virtual controller, supervisor records, direct FunctionFS endpoints, and liveness |
| `buttons_gpio.c` | `buttonRead`; logical No/Yes/center values and edge events through one inherited input-line handle |
| `display_linux.c` | `oledInit`, `oledRefresh`, recovery through `emulatorPoll`, and normal-exit clearing; pass the framebuffer and inherited descriptors through the `display-backends` C ABI |
| `worker_main.c`, `worker_config.c` | Parse project-owned worker options before entering the renamed upstream firmware `main` |

`usb_functionfs.c` is compiled as the upstream firmware's expected `udp.o`, but
implements a virtual `libopencm3` controller rather than a datagram transport.
Firmware initialization creates the genuine device/configuration/string,
Microsoft OS 1.0, and WebUSB responses. The shared supervisor library's direct
discovery parser issues control requests through one C callback and serializes
the resulting typed USB personality as CBOR. The supervisor projects it into
ConfigFS and FunctionFS, then transfers each actual OUT or IN endpoint file
back to the worker. The supervisor retains `ep0` and never accesses data
traffic. The worker never opens the FunctionFS mount. DebugLink is disabled. The
firmware-discovered normal configuration includes both the main vendor
interface and U2F HID, with four data endpoints in total.

Each blocking FunctionFS OUT endpoint has one worker-owned reader thread. The
thread blocks only in the real endpoint read, publishes exactly one completed
transfer in a single handoff slot, and wakes the firmware poll loop through an
`eventfd`. The genuine firmware callback consumes that transfer. Completion of
another FunctionFS read while the preceding handoff remains unconsumed is an
invariant failure, not an invitation for the virtual controller to assemble or
buffer a higher-level message. That assembly remains in the genuine firmware.

IN has no helper or handoff. `usbd_ep_write_packet()` writes the firmware's
transfer directly to the blocking FunctionFS IN endpoint. The host-side
request/response flow provides the natural rendezvous. There is no internal
framing, acknowledgement, socket, or data queue in either direction, and a
zero-length FunctionFS operation remains a zero-length USB transfer. The timed
platform wait suppresses the genuine firmware poll's immediately repeated
virtual-controller pass, but retains that call's pending-reply flush; direct
firmware `usbPoll()` calls continue to perform a normal controller pass.

Suspend retains pending traffic and the existing handles. Disable lets the
kernel cancel FunctionFS operations and retires the OUT readers; the following
enable starts fresh readers on the same handles. Neither path restarts the
worker.

USB configuration state is tracked separately from the supervisor's UDC
binding state. `BIND`, `ENABLE`, `DISABLE`, `UNBIND`, `SUSPEND`, and `RESUME`
records drive the virtual controller. Suspend/resume preserves endpoint and
firmware state and pending traffic; disable/enable resets and reconfigures it
without replacing the FunctionFS handles. Merely closing a host
application handle is not a USB lifecycle event.

`usbReconnect()` performs a live USB-generation replacement. The worker sends
another complete CBOR personality, quiesces and closes its current endpoint
FDs on request, installs the replacements, and acknowledges readiness before
the supervisor rebinds the UDC. The firmware process survives the host-visible
disconnect/re-enumeration.

The I2C profile declares `/dev/i2c-1` as its required `display-i2c` resource.
The supervisor opens it while privileged and includes its descriptor in the
initial named-resource record; the worker selects address `0x3c` and performs all
device-specific transactions after privilege drop. The SPI profile places
`/dev/spidev0.0` in the same fixed slot. The worker defaults to SH1106 SPI. The
`--display=ssd1306-i2c|sh1106-i2c|sh1106-spi|st7789-spi` option overrides the backend;
the two I2C controllers cannot be distinguished by probing because both
normally use `0x3c`.

The supervisor claims two exact GPIO v2 line groups. The next resource is
one display-control output handle, ordered as Data/Command then reset for
SH1106. The ST7789 profile adds backlight as its third line. The following slot
is one input/event handle ordered as No, Yes, then center. Its profile configures
active-low interpretation, pull-ups, and both-edge events, so the worker knows
neither GPIO-chip paths nor numeric offsets. It writes display-control bits,
reads logical button bits atomically, and wakes early on button events. GPIO
line ownership and electrical configuration remain in the supervisor. The
normal firmware loop retains the timeout requested by upstream—normally 10 ms—
so automatic lock, busy-screen expiry, and button processing continue even
while USB is idle. A due display-recovery deadline may shorten that wait.

The sibling `display-backends` library performs controller initialization,
native frame transfer, optional frame conversion, GPIO control, bus I/O,
clearing, and display-off. It duplicates
the already-open bus and exact GPIO-line descriptors passed through its C ABI;
it never opens a hardware path. The worker retains backend selection, retry,
timing, and logging policy. SH1106 SPI uses mode 0 at 4 MHz and lets SPI0 CE0
drive chip select. Center reports both logical Trezor buttons. Missing resources are
fatal; a transfer failure is logged without terminating USB service. The
regular firmware `emulatorPoll` path retries after one second, reinitializes the
display, and retransmits the current framebuffer even when the UI produces no
later refresh.

The adapter declares the current firmware framebuffer as
`Mono1MsbReversePage`, 128x64 with a 128-byte page stride. Pixel format is
separate from controller selection: the shared library also supports row-major
`Mono8` and RGB565 input through SSD1306, SH1106, and ST7789 backends. Producers
that already compose a controller-native frame may use the native write layer
without conversion.

On an orderly exit the worker blanks display RAM and turns the panel output
off. This covers service stops and requested USB reincarnations. `SIGKILL`
cannot run process cleanup; the replacement worker clears the panel again as
part of display initialization.

USB suspend also checkpoints the mapped emulator flash and turns the panel
off. USB resume, or enable after a reset-style wake, reinitializes it and
redraws the unchanged firmware framebuffer. Display refreshes are suppressed
while suspended. The worker remains blocked on its pollable supervisor control
socket instead of returning to the firmware's 10 ms loop, and its platform
timer excludes the suspended interval. Firmware deadlines therefore resume
from where they stopped rather than expiring during host sleep.
On ST7789 hardware this is visibly distinct from the firmware screensaver:
the screensaver writes a black framebuffer while leaving the backlight on,
whereas USB suspend shuts the backend down and turns the backlight off.

The ST7789 profile assigns GPIO25 Data/Command, GPIO27 reset, and GPIO24
backlight to its inherited output handle. The backend uses SPI mode 0 at 62.5
MHz. It clears the native 240x240 panel
once and updates only a centered 240x120 RGB565 window. Trezor's own renderer
still composes the original 128x64 1-bit framebuffer.

The original Trezor One OLED transport is SPI. The SH1106 SPI HAT is not the
original display controller, but it preserves the four-wire transport style.
The I2C backend remains a Pi platform adaptation around the same unchanged
upstream framebuffer. See
[`../../docs/i2c-display-plan.md`](../../docs/i2c-display-plan.md).
