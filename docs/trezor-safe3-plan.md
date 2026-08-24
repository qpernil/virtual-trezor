# Virtual Trezor Safe 3 plan

## Status

Implementation is active. The first checked-in build boundary uses the
project-owned `mk/safe3-worker.mk`, consumes the same pinned upstream checkout,
selects `TREZOR_MODEL=T3B1`, and stages its output under
`build/safe3-t3b1/unix/`. The complete generated and object tree—not merely a
copied executable—lives in that destination. It does not change the current
Virtual Trezor runtime.

The initial `make safe3-baseline` target intentionally produces the untouched
upstream Unix emulator and calls the artifact `trezor-emu-core`. It proves the
genuine T3B1 Core, MicroPython, Rust UI, cryptography, model, and dependency
graph before platform substitution. It retains SDL display/input, UDP USB, and
the upstream 1 ms idle poll, and remains useful as a desktop diagnostic.

The completed `make safe3-display` target is the first platform substitution
and is deliberately display-only. It links no SDL library or SDL source. The
existing Trezor One adapter now accepts a described producer frame instead of
assuming that every firmware supplies its legacy OLED layout. Trezor One
continues to submit its unchanged 1-bit page buffer. The project-owned Core
display driver implements the genuine T3B1 display API and submits Core's
unchanged 8192-byte `Mono8` framebuffer through the same adapter. Unit coverage
verifies both formats, the driver is compiled against the pinned upstream
headers, and the genuine Core homescreen has run on the Pi's ST7789 display.

Because SDL previously supplied both rendering and event collection, this
target also selects an inert implementation of the Core button API and removes
the SDL event pump. It does not yet implement input. USB remains on the
upstream UDP transport, while lifecycle, timing, storage, and the Unix wait
loop remain unchanged. The diagnostic target may open the Pi display resources
directly, or accept already-open display descriptors through its environment;
the final worker will receive named resources from the supervisor.

The completed `make safe3-input` target preserves the display-only artifact
and creates a separate cumulative build under `build/safe3-t3b1-input/`. It
replaces the inert shim with a GPIO state provider while retaining Core's
genuine `button_poll.c` state machines and press/release event generation.
Physical Pi validation confirms independent left and right down/up transitions
and the center-to-both mapping. This stage still leaves UDP USB, lifecycle, and
timing unchanged.

The completed `make safe3-usb` target preserves the genuine display and input
paths and replaces Core Unix's UDP transport with the supervisor protocol.
Core's normal `usb_init`, `usb_webusb_add`, and `usb_hid_add` calls feed an
opaque Rust-owned `UsbPersonalityBuilder`; `usb_start` finishes the builder and
publishes schema-1 CBOR. Firmware descriptor discovery uses that same builder
with raw control-transfer responses, so native construction and genuine EP0
discovery converge on one validation and serialization path. The Safe 3
worker keeps only endpoint runtime state in C. It receives direct FunctionFS
endpoint capabilities and uses one blocking helper per endpoint direction.
OUT completion wakes Core's pollable read handle; an IN helper drains a
single controller submission slot so a stale host cannot block Core's entire
firmware scheduler.

The target produces
`build/safe3-t3b1-usb/virtual-trezor-safe3-usb` with co-located
`libdisplay_backends.so` and `libusb_gadget_worker.so`. The helper is shared
for this target so Core retains its one statically linked Rust runtime. On the
Pi the supervisor accepted the builder-produced Safe 3 personality, macOS
enumerated it as `1209:53c1`, and `trezorctl` read genuine T3B1 Core features
before and after a supervisor reload. This milestone deliberately retains the
upstream periodic Unix event loop. Its 1 ms sleep/poll cadence produces about
4-8% idle CPU on one Raspberry Pi 4 core, compared with roughly 0.3% for the
event-driven Trezor One worker. Replacing that cadence with an event/deadline
wait is the next timing stage.

Display-HAT KEY3 supplies a worker-owned, spring-loaded disconnect. Press sends
an empty personality and the supervisor removes the USB generation without
terminating Core; release sends the complete builder-produced personality and
binds a new generation without an artificial delay.

USB lifecycle remains separate from genuine Core display policy: suspend,
unbind, and KEY3 do not directly power the panel down. Intentional worker
shutdown is different; the worker acknowledges quiescence, exits normally, and
deinitializes the display so the backlight cannot remain on after service stop.

Initial configuration is empty because Core deliberately opens USB only after
its boot/unlock flow. That empty record declares the worker ready without
creating a generation; the later `usb_start` publishes generation one. KEY3
transitions are ignored until that first start, so boot-screen button activity
cannot be mistaken for ejection.

The pinned Core source uses nightly-only Rust language features. During initial
development every project follows the current stable Rust toolchain; the Safe 3
build invokes Cargo with an explicit `RUSTC_BOOTSTRAP=1` because the unchanged
upstream workspace uses the unstable `panic-immediate-abort` Cargo feature and
its crates declare their required nightly language features themselves. This
is intentionally a rolling compiler boundary: later stable regressions will be
fixed when encountered rather than hidden behind a repository-specific
toolchain pin.

The untouched baseline retains the upstream Unix SDL renderer. On the tested
Ubuntu system its native build prerequisites are:

```sh
sudo apt install libsdl2-dev libsdl2-image-dev libjpeg-dev \
  libclang-common-21-dev
make safe3-baseline
```

The Clang package supplies the standard C resource headers used by Rust
bindgen. On an Ubuntu release with a different LLVM major, install the matching
`libclang-common-<major>-dev` package. SDL belongs only to the optional
diagnostic baseline. The physical display target requires the non-SDL
prerequisites and builds with:

```sh
sudo apt install libjpeg-dev libclang-common-21-dev
make safe3-display
```

It produces `build/safe3-t3b1-display/virtual-trezor-safe3-display` and a
co-located `libdisplay_backends.so`; its runtime dependency check rejects any
accidental SDL linkage.

The first Core target will be Trezor Safe 3 revision B, upstream model `T3B1`.
The existing Trezor One (`T1B1`) worker remains supported as an independent
worker, profile, and persistent device state. Safe 5, touchscreen input, SDL,
and mirrored desktop rendering are later projects and are not part of this
plan.

## Goals

- Run the genuine upstream Safe 3 Core firmware logic as an unprivileged
  worker managed by `usb-gadget-supervisor`.
- Preserve the current Trezor One build, binary, profiles, state, and behavior.
- Retain upstream Core UI composition and its native 128x64, 8-bit grayscale
  framebuffer without reducing it to 1-bit in the worker.
- Use the Pi's physical buttons and the existing selectable display hardware.
- Wait for real input or the next firmware deadline so an idle worker does not
  busy-loop, while keeping timers and firmware behavior faithful.
- Keep the upstream firmware checkout unmodified and make the integration
  boundary explicit and auditable.

This remains a development simulator, not a hardware wallet. File-backed
secrets on a general-purpose Pi do not receive Safe 3's physical or
side-channel protections.

## Coexistence with Trezor One

Safe 3 is an addition, not a conversion of the existing worker. The intended
runtime objects are separate:

| Concern | Existing Trezor One | Future Safe 3 |
| --- | --- | --- |
| Upstream model | `T1B1` | `T3B1` |
| Firmware family | Legacy | Core |
| Worker | `virtual-trezor-worker` | `virtual-trezor-safe3-worker` |
| State | Existing Trezor One directory | `/var/lib/virtual-trezor-safe3-t3b1` |
| Example profile | Existing profiles | `virtual-trezor-safe3-t3b1-st7789` |
| Framebuffer | 128x64 1-bit | 128x64 8-bit grayscale |
| Input | Two buttons | Two buttons |

Each profile identifies one USB gadget. A Pi normally has one usable USB
device controller, so the supervisor selects either Trezor One or Safe 3 at a
time. Their state directories must never be shared. Switching profiles must
not rebuild, migrate, or overwrite the other model's state.

## Core platform boundary

The upstream Unix Core emulator currently supplies SDL display/input, UDP USB,
file-backed storage, and a polling system-event loop. The Pi worker will retain
Core firmware, MicroPython, Rust UI, protocol handling, storage semantics,
timers, and cryptography, while replacing only the host-facing platform
pieces:

- a headless Core entry point in place of the SDL application lifecycle;
- FunctionFS USB endpoints inherited from the supervisor in place of UDP;
- GPIO-backed Core button input in place of SDL events;
- a display adapter that submits Core's framebuffer to `display-backends`;
- the supervisor's named-resource, CBOR personality, endpoint-generation, and
  liveness protocol;
- file-backed state rooted in the dedicated Safe 3 state directory; and
- an event-driven system wait in place of the Unix emulator's periodic idle
  polling.

Project-owned integration code may select upstream sources, set model/build
options, and implement required platform symbols. The current build applies a
small, checked project-owned source-selection overlay and reverses it on every
exit. The pinned upstream submodule therefore remains clean and its revision is
recorded with the build.

## Display contract

Safe 3 Core composes a row-major 128x64 `Mono8` framebuffer: one intensity byte
per pixel with a stride of 128 bytes. The Safe 3 worker passes those bytes to
`display-backends` unchanged and describes the producer format accurately.
The worker must not threshold, pack, dither, or otherwise convert the image to
1-bit.

Controller-specific conversion belongs to `display-backends`:

- SSD1306 and SH1106 backends threshold and pack `Mono8` into their native
  1-bit page layout;
- the ST7789 backend maps each intensity to RGB565 grayscale, scales with the
  aspect ratio preserved, and centers the 128x64 image on its 240x240 panel;
  and
- a future native 8-bit display backend may preserve all intensity values.

The completed supervisor worker selects `ssd1306-i2c`, `sh1106-i2c`,
`sh1106-spi`, or `st7789-spi` from the named bus resource delivered with the
profile. This is the same backend set exposed by Trezor One; Safe 3 does not
parse display options from Core's Python command line.

This boundary lets the same firmware output reach different displays without
changing Core UI code or silently discarding information before the selected
backend sees it.

## Buttons

Safe 3 uses two physical buttons, which is a direct fit for the current Pi
hardware. The platform driver reports an active-low GPIO state bitmask to
Core's existing poller, which preserves upstream per-task state and
press/release event generation. GPIO5 is left, GPIO26 is right, and GPIO13
maps the physical HAT's joystick press to both logical buttons. The diagnostic
target requests those lines directly; it can instead accept the exact inherited
line-request handle through `VIRTUAL_TREZOR_BUTTONS_FD`. The completed worker
will receive that named resource from the supervisor and will not open the GPIO
chip itself.

The line handle uses pull-ups, logical active-low values, and both-edge
detection. During this polling stage only the current logical values are
consumed. A later event-driven wait will also drain the same handle's edge
events. `VIRTUAL_TREZOR_BUTTON_TRACE=1` enables transition logging for target
diagnostics and is absent from normal operation.

## USB and supervisor integration

The Safe 3 worker follows the same privilege boundary as Trezor One. The
supervisor owns ConfigFS, FunctionFS creation, descriptor validation, UDC
binding, privileged device access, and privilege drop. The worker receives
only already-open endpoints and declared hardware resources, and owns runtime
Trezor protocol processing.

The current profile exposes the non-debug interfaces expected from T3B1: the
main Trezor vendor/WebUSB transport and the FIDO HID transport, with two
interrupt endpoints each. Debug interfaces remain disabled.

The supervisor's typed Microsoft OS 1.0 and WebUSB projection is reusable for
this worker. Safe 3's Unix platform receives semantic device and interface
configuration through Core's genuine USB API. The shared Rust builder derives
its VID/PID, interface ordering, descriptors, WinUSB compatible ID, and WebUSB
declaration from those calls rather than copying the legacy Trezor One byte
table.

Worker startup, readiness, shutdown, supervisor loss, and endpoint recovery
use the shared CBOR personality and endpoint-generation protocol rather than
adding a model-specific privileged control path. The C platform adapter calls
the opaque native builder API; firmware with a real EP0 engine instead uses
the discovery callback.

## Idle behavior and timing

Real hardware avoids a busy loop by sleeping until an interrupt or timer is
due. The virtual worker should provide the equivalent Linux behavior.

Core's system-event wait will calculate the nearest firmware deadline and
block in the kernel across all relevant descriptors: FunctionFS endpoints,
the supervisor control/liveness channel, GPIO events, and any internal wakeup
descriptor required by the runtime. It wakes when an event arrives or the
deadline expires, then lets Core process the resulting events and timers.

This replaces the upstream Unix emulator's idle 1 ms polling delay. It must
not replace the wait with an infinite sleep: automatic lock, animations,
transport timeouts, storage work, and other scheduled firmware activity still
need to run at their requested deadlines. Tests should verify both low idle
CPU use and correct timer behavior.

## Build and source layout

The Safe 3 worker will be a separate reproducible build target with
`TREZOR_MODEL=T3B1`. It should use project-owned platform sources or a small
overlay outside the upstream submodule and produce a distinct binary. The
existing `make worker` behavior for Trezor One must remain unchanged; a new,
explicit target should build Safe 3.

Dependency initialization should stay selective. The Safe 3 target may add
the Core, MicroPython, Rust, model, and secure-element emulator dependencies it
actually needs without forcing the legacy Trezor One path to initialize or
compile them. The design must be reconciled with the result of the current
cryptography refactor before source lists or ownership are fixed.

## Delivery stages

1. **Baseline complete:** pin and compile the genuine T3B1 Core source graph
   separately from Trezor One, recording provenance and the compiler boundary.
2. **Display boundary complete:** remove SDL and deliver the unchanged `Mono8`
   framebuffer through the shared adapter and `display-backends`. The button
   API is inert; USB, lifecycle, and timing retain their upstream behavior.
3. **Input complete:** preserve the display-only artifact, add a cumulative
   GPIO target, and verify genuine Core left/right press and release states on
   the Pi. Center maps to both logical buttons.
4. **USB and lifecycle complete:** enumerate through the supervisor, handle
   protocol traffic and reload reconnects, and isolate state and resources
   from Trezor One.
5. **Timing:** replace idle polling with the bounded event-driven wait and
   measure idle CPU and firmware deadlines on the Pi.
6. **Fidelity:** complete the normal Safe 3 USB interface set and validate
   initialization, recovery, signing confirmations with test-only data,
   automatic lock, failure recovery, and repeated profile switching.
7. **Deployment:** add documented profiles, installation steps, service
   validation, and reproducible Ubuntu/Raspberry Pi builds.

## Acceptance criteria

- The current Trezor One worker still builds, passes its tests, and runs with
  its existing profiles and state.
- Safe 3 identifies as the intended virtual `T3B1` model and can complete the
  agreed Suite/CLI development workflows with test-only state.
- Trezor One and Safe 3 state remain independent across repeated profile
  switches.
- Safe 3's 8-bit framebuffer arrives byte-for-byte at `display-backends`.
- Monochrome and ST7789 outputs are correct, centered, and derived only in the
  selected backend.
- Physical buttons produce correct Core press/release behavior.
- Idle CPU remains low, with no busy loop, blanked screen regression, missed
  automatic-lock deadline, or delayed USB/button response.
- Losing the supervisor or USB host causes a controlled stop or recovery, not
  an orphaned worker.
- The upstream checkout remains unmodified and the build records its exact
  revision and dependencies.

## Deferred work

Additional display controllers require a `display-backends` implementation and
a corresponding supervisor resource profile. Core will continue to submit the
same unchanged `Mono8` frame; controller-specific conversion remains
exclusively a backend concern.

Safe 5 (`T3T1`) is the most natural later touchscreen target because its
240x240 RGB565 framebuffer matches the existing ST7789 panel. An optional SDL
companion could mirror that framebuffer and provide mouse/touch input without
moving desktop privileges into the firmware worker. That design, Safe 7, haptic
feedback, Bluetooth, battery behavior, and other touchscreen models are
explicitly outside the Safe 3 work described here.
