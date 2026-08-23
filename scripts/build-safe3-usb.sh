#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SUPERVISOR_DIR="${USB_GADGET_SUPERVISOR_DIR:-$PROJECT_ROOT/../usb-gadget-supervisor}"
USB_BUILD_DIR="${SAFE3_DISPLAY_BUILD_DIR:-$PROJECT_ROOT/build/safe3-t3b1-usb}"

cargo build --manifest-path "$SUPERVISOR_DIR/crates/usb-gadget-worker/Cargo.toml" \
  --release --locked
WORKER_LIBRARY="$SUPERVISOR_DIR/target/release/libusb_gadget_worker.so"
test -f "$WORKER_LIBRARY"

export SAFE3_BUTTON_SOURCE="$PROJECT_ROOT/platform/safe3/button_gpio.c"
export SAFE3_BUTTON_RESOURCES_SOURCE="$PROJECT_ROOT/platform/safe3/button_resources.c"
export SAFE3_DISPLAY_BUILD_DIR="$USB_BUILD_DIR"
export SAFE3_OUTPUT_NAME="virtual-trezor-safe3-usb"
export VIRTUAL_TREZOR_SUPERVISOR_USB=1
export VIRTUAL_TREZOR_USB_SOURCE="$PROJECT_ROOT/platform/safe3/usb_supervisor.c"
export VIRTUAL_TREZOR_SUPERVISOR_INCLUDE="$SUPERVISOR_DIR/worker"
export VIRTUAL_TREZOR_WORKER_LIBRARY="$WORKER_LIBRARY"

"$PROJECT_ROOT/scripts/build-safe3-display.sh"
install -m 0755 "$WORKER_LIBRARY" \
  "$USB_BUILD_DIR/libusb_gadget_worker.so"
