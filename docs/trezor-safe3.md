# Virtual Trezor Safe 3

## Current status

The Safe 3 revision B (`T3B1`) worker is implemented and runs genuine upstream
Core firmware logic on 64-bit Linux. It is independent of the legacy Trezor One
worker and produces:

```text
build/safe3-t3b1-usb/virtual-trezor-safe3-usb
```

The worker combines upstream Core, MicroPython, Rust UI and cryptography with
project-owned Linux adaptations for display, buttons, USB, secure emulator
randomness and idle waiting. On Raspberry Pi 4 it has been validated with the
ST7789 display, physical buttons, the supervisor, macOS USB enumeration,
`trezorctl`, and Trezor Suite in debug mode. At stable idle it consumes
approximately 0–0.2% of one core.

This remains a development simulator, not a hardware wallet. Its state and
secrets are ordinary files on a general-purpose computer and do not receive
Safe 3's physical, extraction or side-channel protections.

### Trezor Suite debug mode

Run Trezor Suite in **debug mode** for Safe 3 emulator work. Debug mode selects
Suite's emulator trust path, allowing the genuine upstream emulator firmware
to pass Suite's authenticity check. This remains true when that firmware is
transported through the Pi's physical FunctionFS USB gadget rather than the
upstream emulator's usual UDP transport.

Without debug mode, Suite applies its production-firmware trust policy and may
report the emulator image as non-genuine or refuse protected operations. Debug
mode changes Suite's acceptance policy; it does not make the Pi, file-backed
state, or virtual secure element equivalent to production hardware. The Suite
UI should continue to identify the session as an emulator/development context.

## Build targets

Safe 3 has four explicit build stages. They use separate output directories
and do not change the Trezor One artifact.

| Target | Purpose |
| --- | --- |
| `make safe3-baseline` | Unmodified upstream SDL display/input and UDP USB emulator |
| `make safe3-display` | Project display driver, no SDL, inert buttons, upstream UDP USB |
| `make safe3-input` | Display stage plus physical GPIO buttons |
| `make safe3-usb` | Complete Pi worker with supervisor USB and virtual WFI |

The complete worker requires Linux, `libjpeg-dev`, the LLVM resource headers
matching the host compiler, the repository's pinned Python environment, and
the current stable Rust toolchain:

```sh
make init
make check
make safe3-usb
```

The upstream Core workspace currently uses nightly-only language and Cargo
features. The build therefore applies the narrowly scoped
`RUSTC_BOOTSTRAP=1` environment setting while still using the global stable
toolchain. It records the upstream revision and Rust version beside each
artifact.

Two checked project overlays are applied only while a Safe 3 target builds and
are always reversed afterward:

- `safe3-headless-display.patch` selects the project display, button and USB
  implementations, removes SDL from headless targets, links the shared native
  libraries, and installs the virtual-WFI hook.
- `safe3-secure-random.patch` replaces deterministic emulator reseeding with
  the secure Unix RNG path backed by `/dev/urandom`.

The pinned upstream submodule must be clean before and after every build.

## MicroPython execution model

All four project targets are Unix emulator builds. `safe3-baseline` invokes
upstream `build_unix`; the display, input, and USB targets invoke upstream
`xtask` with `build firmware --emulator --pyopt false`. None passes `--frozen`.

The resulting executable contains the MicroPython interpreter and the compiled
C and Rust native modules. Core's application modules remain `.py` files under
`core/src`; the emulator imports them from the filesystem and compiles them to
MicroPython bytecode at runtime. Consequently the worker must start with
`core/src` as its script directory, and changing application Python takes
effect without relinking the native executable.

Real Safe 3 firmware enables Core's `frozen` feature. The build runs the Python
modules through `mpy-cross`, generates `frozen_mpy.c`, links that bytecode into
the firmware image, and starts `main.py` from the frozen module table. Upstream
also provides `build_unix_frozen` for a Unix emulator with that packaging, but
the Virtual Trezor Safe 3 artifacts intentionally use the ordinary unfrozen
emulator form during development. This changes how Python is packaged and
loaded; it does not replace Core's application code or MicroPython runtime.

## Firmware and state

The worker runs upstream model `T3B1` from Core release `2.12.4`. Core's
file-backed Unix flash implementation remains genuine and is rooted through
`TREZOR_PROFILE_DIR` at the profile's state directory:

```text
/var/lib/virtual-trezor-safe3
```

Trezor One uses a different state directory. All Safe 3 display-profile
variants deliberately share the Safe 3 directory so changing only the physical
panel does not create a different wallet. Only one variant may run at a time.

Core can remain in its own boot or PIN flow before opening USB. The worker
therefore first publishes an empty personality as a readiness declaration. The
supervisor stays healthy without attaching a USB device until genuine Core
calls `usb_start()` and publishes the real personality.

## Display contract

Core composes a row-major 128×64 `Mono8` framebuffer: one intensity byte per
pixel with a stride of 128 bytes. The worker sends all 8192 bytes unchanged to
`display-backends`. It does not threshold, pack, dither or convert the frame.

Safe 3 supports the same four physical backends as Trezor One:

| Backend | Profile | Conversion owned by `display-backends` |
| --- | --- | --- |
| ST7789 SPI | `virtual-trezor-safe3.toml` | Mono8 to RGB565, scaled to 240×120 and centered |
| SH1106 SPI | `virtual-trezor-safe3-sh1106-spi.toml` | Thresholded and packed into native 1-bit pages |
| SH1106 I²C | `virtual-trezor-safe3-sh1106-i2c.toml` | Thresholded and packed into native 1-bit pages |
| SSD1306 I²C | `virtual-trezor-safe3-ssd1306-i2c.toml` | Thresholded and packed into native 1-bit pages |

The named display-bus resource selects the backend; Safe 3 does not parse a
display option from Core's Python command line. ST7789 is physically validated.
The three monochrome profile variants build and pass supervisor schema checks,
but still require final physical Safe 3 validation on their respective panels.

## Buttons and disconnect control

Core's genuine `button_poll.c` state machines retain ownership of per-task
press/release history. The project driver supplies only current logical GPIO
state:

- GPIO5: left
- GPIO26: right
- GPIO13: joystick center, mapped to both logical buttons
- GPIO16 / display-HAT KEY3: spring-loaded USB eject and insertion

All inputs are active-low with pull-ups and both-edge detection. The supervisor
claims the exact GPIO groups and passes line-request handles; the worker never
opens the GPIO chip or chooses additional lines.

Holding KEY3 publishes an empty USB personality and removes the current USB
generation while Core and its display remain alive. Releasing KEY3 republishes
the complete genuine personality and creates a fresh generation immediately.
Handles associated with the retired generation cannot transfer.

## USB personality and endpoints

Core's normal `usb_init`, `usb_webusb_add`, `usb_hid_add` and `usb_start` calls
populate the shared Rust `UsbPersonalityBuilder`. The resulting typed CBOR
personality describes the genuine Safe 3 device and is validated by the
supervisor before ConfigFS or FunctionFS is configured.

The current non-debug personality enumerates as `1209:53c1` and contains:

- the main Trezor vendor/WebUSB interface;
- the FIDO HID interface;
- two interrupt endpoints per interface;
- a Microsoft OS 1.0 `WINUSB` compatible ID for the vendor interface; and
- a WebUSB 1.0 BOS capability without a landing page.

The supervisor retains privileged `ep0` ownership. Direct FunctionFS data
endpoint handles are transferred to the unprivileged worker. Each blocking
endpoint direction has one helper thread and one single-packet handoff slot;
there is no socket framing, acknowledgement protocol or extra USB-level queue.
Core continues to own Trezor message assembly and protocol backpressure.

## Virtual wait for interrupt

Real Core hardware executes `WFI` when no event source is ready. The standard
Unix emulator's `sysevents_poll()` probes every source and, when none is ready,
calls `systick_delay_ms(1)` before repeating the complete probe. Waking about a
thousand times per second has a measurable idle cost.

The Safe 3 build overlay changes only that no-event branch when
`VIRTUAL_TREZOR_SUPERVISOR_USB` is enabled. It calls
`virtual_trezor_wait_for_interrupt(deadline)`, passing the deadline already
computed by Core. The Linux implementation calculates the remaining firmware
time and performs one blocking `ppoll()`. In practical terms, the worker does
not wake every millisecond: it remains blocked until USB control or lifecycle
traffic arrives, an endpoint becomes ready, a button GPIO changes, KEY3
changes, or the remaining time on Core's virtual firmware timer expires. The
wait covers:

- the supervisor control and lifecycle channel;
- endpoint notification `eventfd`s;
- the KEY3 reconnect line;
- normal button GPIO edge events; and
- Core's nearest firmware timer deadline.

An FD event or timeout returns control to the genuine `sysevents_poll()` loop,
which probes and dispatches the event through the normal firmware path. The
platform wait neither manufactures a firmware event nor consumes a timer
deadline. Automatic lock, UI timers, transport timeouts, USB lifecycle, and
button input therefore retain firmware timing semantics without a busy loop.
Measured stable idle CPU is 0–0.2%, with no periodic one-millisecond sleeps.

An exact upstream `make safe3-baseline` build was also measured on the same Pi
4. It retained upstream SDL/UDP, used SDL's dummy video driver only because the
SSH session was headless, and ran unmodified `sysevents_poll()`. After reaching
the home screen, ten two-second `pidstat` samples averaged 9.85% of one core:
4.50% user time and 5.35% kernel time. The identical artifact measured about
1.7% on an M-series Mac. That contrast helps explain why the portable
one-millisecond delay is a reasonable simplicity tradeoff for a desktop
development emulator: it consumes only a small fraction of one modern desktop
core. The same 1 kHz wake cadence is material on the Pi 4. This confirms that
the persistent idle cost exists in Trezor's upstream emulator; because the
baseline also contains SDL and UDP, it is not a single-variable benchmark of
the virtual-WFI hook.

## Lifecycle

`ENABLE`, `DISABLE`, `SUSPEND`, `RESUME`, unbind and replacement-generation
events reach the worker through the supervisor control channel and wake virtual
WFI. Endpoint readiness is gated while USB is disabled or suspended. The
worker and Core state survive ordinary USB generation replacement, including
KEY3 eject/insert.

Safe 3 leaves panel policy with genuine Core: USB suspend and KEY3 ejection do
not directly turn off the display. A clean worker shutdown explicitly
deinitializes the selected backend and disables its display or backlight.

## Known limitations and later work

- Production Suite may enforce firmware authenticity. Debug mode is required
  for the intended emulator workflows.
- Safe 3 recovery and every protected signing workflow are not exhaustively
  validated; use only disposable test state.
- The SH1106 and SSD1306 Safe 3 profiles still need physical-panel validation.
- Safe 5 or another touchscreen model, SDL touch mirroring, haptics, battery,
  Bluetooth and secure-element fidelity are separate later projects.
