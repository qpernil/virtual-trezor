# I2C display plan

## Purpose and fidelity boundary

The genuine Trezor One firmware composes its complete 128x64 monochrome UI in
the 1024-byte framebuffer owned by upstream `legacy/oled.c`. That framebuffer,
the drawing primitives, layouts, prompts, and confirmation behavior remain
unmodified in this project.

A physical Trezor One sends the framebuffer to its OLED over STM32 SPI, using
separate chip-select, data/command, and reset GPIOs. The Raspberry Pi backend
instead emits a selectable SSD1306 or SH1106 I2C stream at address `0x3c`.
This is a deliberate platform adaptation: firmware/UI fidelity is retained
above `oledRefresh`, while the board-level display transport is different.

## Validation topology

The initial wired setup uses two Linux systems and a real I2C bus:

```text
controller Pi                                      target Pi

unmodified Trezor UI composition                   display protocol consumer
legacy/oled.c framebuffer                           framebuffer reconstruction
platform oledRefresh                               virtual-display SDL renderer
        |                                                   ^
        v                                                   |
    /dev/i2c-1 -- physical SDA, SCL, and ground --> /dev/bsc-target0
```

The controller runs the Virtual Trezor worker and sends real electrical I2C.
The target runs the interrupt-driven driver from
[`raspberry-pi-i2c-target`](https://github.com/qpernil/raspberry-pi-i2c-target)
and the `virtual-display` protocol interpreter. A Siglent SDS824X HD observes SCL,
SDA, and optional controller/target timing markers.

Pi 3 and Pi 4 can run the existing BSC target driver. Pi 5 can be the
controller but does not expose the legacy target peripheral required by that
driver.

## Display transaction model

The project backend now provides the emulator-build symbols that upstream
expects:

- `oledInit` receives the required supervisor-opened I2C controller resource
  and emits the selected controller's initialization command stream;
- `oledRefresh` obtains the bytes from upstream `oledGetBuffer`, positions the
  display, and writes the frame;
- `emulatorPoll` uses the firmware transport's regular emulator poll call to
  retry a failed transfer after one second and retransmit the current
  framebuffer even if the UI does not refresh again.

Both supported streams prefix command bytes with control byte `0x00` and
display data with control byte `0x40`. SSD1306 uses a seven-byte horizontal
address-window message followed by one 1,025-byte framebuffer message. SH1106
uses page addressing: each of eight pages has a four-byte command message and
a 129-byte data message, for 1,064 bytes per refresh. At 400 kHz the 1024
display bytes require about 23 ms on the wire; at 100 kHz they require about
92 ms, before the small command overhead. The target interpreter must process
addressing, orientation, and initialization commands rather than assuming
that every write is a complete frame.

The worker options `--display=ssd1306-i2c|sh1106-i2c` select the I2C stream.
Explicit selection is required because both controllers normally acknowledge
address `0x3c` and provide no useful identification query. The checked-in I2C
profile selects SH1106 for the validated second-Pi setup; change it to SSD1306
for that controller family. SH1106 reset is GPIO25, requested through the
required supervisor-opened `/dev/gpiochip0` resource. The factory-configured
Waveshare HAT uses the separate `sh1106-spi` backend and SPI profile.

The upstream framebuffer has the ordering used by the original OLED setup.
The I2C backend and target renderer were compared against the upstream SDL
baseline when deciding byte, bit, page, and segment mapping. No transformation
belongs in upstream `legacy/oled.c`.

## Staged implementation

1. **Complete:** mirror the upstream SDL baseline to SSD1306 and SH1106 I2C.
2. **Complete:** reconstruct and render either controller stream on a second
   Pi with the generic `virtual-display` client.
3. **Complete:** compare startup, prompts, confirmations, and animations.
4. **Complete:** capture the bus at 400 kHz and verify target-driver overrun and
   drop counters under repeated animation traffic.
5. **Complete:** remove local SDL and use GPIO5/GPIO26 for firmware buttons;
   the remote viewer can hold those lines low from mouse input.
6. **Pending:** attach and validate an I2C-native physical 128x64 OLED.

`usb-gadget-supervisor` opens the required `display-i2c` and `display-gpio`
resources and exports their descriptors as
`USB_GADGET_RESOURCE_DISPLAY_I2C_FD` and
`USB_GADGET_RESOURCE_DISPLAY_GPIO_FD`. Pure C tests verify both exact command
streams, page/address construction, I2C/SPI option parsing, and byte-for-byte
framebuffer payloads.

## Wired validation

On 2026-08-18, a Pi 4 controller running the deployed Virtual Trezor worker
sent the stream at a scope-measured 400 kHz to two Pi 3 targets connected to
the same bus and activated one at a time at address `0x3c`. Each target
received 135,218 bytes:

```text
26-byte initialization + 131 * (7-byte address window + 1025-byte frame)
```

Both target runs reported `rx_overruns=0`, `rx_dropped=0`, and no transmitted
response data. The controller remained an enumerated, responsive Trezor USB
gadget while mirroring the display.

The two targets delivered 210 and 225 userspace records for the identical byte
total. The BSC target peripheral does not expose STOP directly, so its 100 us
completion timer can combine adjacent controller writes when the inter-message
idle interval is shorter. No bytes were lost. The SSD1306 interpreter must
therefore parse control bytes and command/data lengths as a byte stream rather
than assuming every character-device `read()` maps one-to-one to a controller
`write()`.

The SH1106 stream was validated on the same physical bus on 2026-08-18. Two
independent worker/target runs each received exactly 139,412 bytes:

```text
26-byte initialization + 2-byte display-on +
131 * (8 * (4-byte page command + 129-byte page data))
```

The ARM64 build, project option wrapper, supervisor resource handoff, GPIO v2
reset request, I2C address selection, and page stream were all exercised. The
service logged SH1106 selection at `0x3c`, and the kernel reported GPIO25 as
an output owned by `virtual-trezor-display`; the receive-only target returned
no data and both captures ended on the same complete-frame boundary. The
second-Pi renderer subsequently confirmed orientation and the two-column
SH1106 RAM offset; the physical Waveshare HAT remains to be tested.

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
