# Raspberry Pi platform boundary

This directory will implement the host-facing functions required by the
unmodified Trezor One firmware logic.

The initial worker must not compile either upstream datagram transport
implementation. It keeps the upstream SDL display and button objects so USB
compatibility can be tested independently of the hardware UI.

The initial replacement surface is:

| Replacement | Responsibility |
| --- | --- |
| `usb_functionfs.c` | `usbInit`, `usbPoll`, `waitAndProcessUSBRequests`, `usbTiny`, and `usbFlush` using FunctionFS endpoints |
| `runtime.c` | Supervisor control descriptor and resource-FD discovery |

After the FunctionFS device works with `trezorctl` and Trezor Suite, the
hardware-UI milestone adds:

| Replacement | Responsibility |
| --- | --- |
| `display.c` | `oledInit` and `oledRefresh`; send the framebuffer returned by upstream `oledGetBuffer` to a 128x64 OLED |
| `buttons.c` | `buttonRead` using two GPIO inputs; upstream `buttonUpdate` retains debounce and event semantics |

The supervisor may open I2C/GPIO character devices while privileged and pass
their descriptors to the worker. All device-specific transactions and policy
remain here in the unprivileged process.
