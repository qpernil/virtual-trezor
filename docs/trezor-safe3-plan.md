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

The pinned Core source uses nightly-only Rust language features. During initial
development every project follows the current stable Rust toolchain; the Safe 3
build invokes Cargo with an explicit `RUSTC_BOOTSTRAP=1` because the unchanged
upstream crate still declares those features. The overlay also injects the
`custom_test_frameworks` crate feature required by current stable for
upstream's existing test-harness attribute. This is intentionally a rolling
compiler boundary: later stable regressions will be fixed when encountered
rather than hidden behind a repository-specific toolchain pin.

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

This boundary lets the same firmware output reach different displays without
changing Core UI code or silently discarding information before the selected
backend sees it.

## Buttons

Safe 3 uses two physical buttons, which is a direct fit for the current Pi
hardware. The new platform driver will translate the inherited, pollable GPIO
line events into Core button events while preserving the upstream button state
machine, including press/release ordering and timing. The worker receives only
the exact line-request handles prepared by the supervisor; it does not open a
GPIO chip or acquire broader hardware access.

The current HAT's optional center action may continue to map to both logical
buttons if that behavior remains useful, but the two model buttons are the
fidelity boundary.

## USB and supervisor integration

The Safe 3 worker follows the same privilege boundary as Trezor One. The
supervisor owns ConfigFS, FunctionFS creation, descriptor validation, UDC
binding, privileged device access, and privilege drop. The worker receives
only already-open endpoints and declared hardware resources, and owns runtime
Trezor protocol processing.

The first milestone may expose the main Trezor vendor/WebUSB transport to
prove enumeration and management workflows. The completed Safe 3 profile
should match the non-debug interfaces expected from the selected upstream
model, including any separately described HID function. Debug interfaces must
remain disabled in the normal profile.

The supervisor's typed Microsoft OS 1.0 and WebUSB projection is reusable for
this worker. Safe 3 discovery must derive its VID/PID, interface ordering,
WinUSB compatible IDs or successor Microsoft descriptors, interface GUIDs,
BOS capabilities, and vendor request codes from the selected firmware rather
than copying the legacy Trezor One byte table.

Worker startup, readiness, shutdown, supervisor loss, and endpoint recovery
must use the shared CBOR personality and endpoint-generation protocol rather
than adding a model-specific privileged control path. A Rust Core worker may
call the discovery parser directly; only a C firmware boundary needs the small
callback FFI adapter.

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
3. **Input:** replace SDL buttons and exercise genuine Core press/release
   interaction through the Pi GPIO resources.
4. **USB and lifecycle:** enumerate through the supervisor, handle protocol
   traffic and reconnects, and isolate state and resources from Trezor One.
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

Safe 5 (`T3T1`) is the most natural later touchscreen target because its
240x240 RGB565 framebuffer matches the existing ST7789 panel. An optional SDL
companion could mirror that framebuffer and provide mouse/touch input without
moving desktop privileges into the firmware worker. That design, Safe 7, haptic
feedback, Bluetooth, battery behavior, and other touchscreen models are
explicitly outside the Safe 3 work described here.
