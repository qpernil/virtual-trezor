# I2C display plan

## Purpose and fidelity boundary

The genuine Trezor One firmware composes its complete 128x64 monochrome UI in
the 1024-byte framebuffer owned by upstream `legacy/oled.c`. That framebuffer,
the drawing primitives, layouts, prompts, and confirmation behavior remain
unmodified in this project.

A physical Trezor One sends the framebuffer to its OLED over STM32 SPI, using
separate chip-select, data/command, and reset GPIOs. The Raspberry Pi backend
will instead emit an SSD1306-compatible I2C stream at address `0x3c`. This is a
deliberate platform adaptation: firmware/UI fidelity is retained above
`oledRefresh`, while the board-level display transport is different.

## Validation topology

The initial wired setup uses two Linux systems and a real I2C bus:

```text
controller Pi                                      target Pi

unmodified Trezor UI composition                   SSD1306 interpreter
legacy/oled.c framebuffer                           framebuffer reconstruction
platform oledRefresh                               SDL/HDMI renderer
        |                                                   ^
        v                                                   |
    /dev/i2c-1 -- physical SDA, SCL, and ground --> /dev/bsc-target0
```

The controller runs the Virtual Trezor worker and sends real electrical I2C.
The target runs the interrupt-driven driver from
[`raspberry-pi-i2c-target`](https://github.com/qpernil/raspberry-pi-i2c-target)
and a future SSD1306 userspace interpreter. A Siglent SDS824X HD observes SCL,
SDA, and optional controller/target timing markers.

Pi 3 and Pi 4 can run the existing BSC target driver. Pi 5 can be the
controller but does not expose the legacy target peripheral required by that
driver.

## Display transaction model

The project backend will provide the emulator-build symbols that upstream
expects:

- `oledInit` opens or receives the I2C controller resource and emits the
  selected module's initialization command stream;
- `oledRefresh` obtains the bytes from upstream `oledGetBuffer`, positions the
  display, and writes the frame;
- `emulatorPoll` remains available because the firmware transport calls it on
  emulator builds.

An SSD1306-compatible I2C transaction normally prefixes command bytes with
control byte `0x00` and display data with control byte `0x40`. A complete data
refresh is therefore 1025 payload bytes before address and bus framing. At
400 kHz the 1024 display bytes require about 23 ms on the wire; at 100 kHz they
require about 92 ms. The target interpreter must also process addressing,
orientation, and initialization commands rather than assuming that every
write is a complete frame.

The upstream framebuffer has the ordering used by the original OLED setup.
The I2C backend and target renderer will compare known frames against the SDL
renderer before deciding whether a selected SSD1306 module needs byte, bit,
page, or segment remapping. No transformation belongs in upstream
`legacy/oled.c`.

## Staged implementation

1. Add a transitional platform display that retains the SDL window and
   `emulatorPoll` while mirroring every refresh to `/dev/i2c-1` at `0x3c`.
2. Add an SSD1306 target process that reconstructs controller state and renders
   the received framebuffer on the second Pi.
3. Compare SDL and target-rendered frames, including startup, home, PIN,
   confirmation, and progress layouts.
4. Capture address, ACK, command, frame data, rise time, and full-refresh
   duration with the oscilloscope; run repeated 1025-byte transfers while
   checking target-driver overrun/drop counters.
5. Add physical GPIO buttons and a non-SDL display backend.
6. Attach and validate a physical 128x64 I2C OLED.

Keeping SDL in the first stage preserves the already-tested click and keyboard
confirmation path. It also lets USB, firmware behavior, I2C transport, and
target rendering be compared without changing all platform boundaries at
once.

## Pi 4 controller clock verification

The configured adapter baud rate is not sufficient evidence of the physical
SCL rate. On a Pi 4 controller, Linux programmed BSC divider 1250 for a reported
500 MHz parent and requested 400 kHz, while firmware power management reduced
the physical core clock to 200 MHz. The scope measured 6.25 us rising edge to
rising edge, or 160 kHz:

```text
500 MHz / 1250 = 400 kHz  (programming assumption)
200 MHz / 1250 = 160 kHz  (measured idle result)
```

Setting `core_freq_min=500` under `[all]` in
`/boot/firmware/config.txt` held the peripheral/core clock at 500 MHz. After a
reboot, the live clock was 500000992 Hz with the same divider, corresponding
to approximately 400 kHz and a 2.5 us SCL period. This is a controller-side
clock-management issue, independent of the I2C target driver. The setting
modestly increases Pi 4 idle power while leaving ARM CPU frequency scaling
enabled.

Pi 5 header I2C is provided by RP1 and did not exhibit this BCM2711
core-clock behavior in the tested setup.

## Safety

Use 3.3 V signaling, connect SDA, SCL, and common ground between separate
boards, and do not connect their power rails. Use high-impedance 10x scope
probes with both ground clips connected to circuit ground. This remains a
development simulator; never load real wallet seeds or valuable secrets.
