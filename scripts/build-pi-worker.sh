#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UPSTREAM_DIR="$PROJECT_ROOT/upstream/trezor-firmware"
LEGACY_DIR="$UPSTREAM_DIR/legacy"
DISPLAY_BACKENDS_DIR="${DISPLAY_BACKENDS_DIR:-$PROJECT_ROOT/../display-backends}"
USB_GADGET_SUPERVISOR_DIR="${USB_GADGET_SUPERVISOR_DIR:-$PROJECT_ROOT/../usb-gadget-supervisor}"
PROTOC_TOOL="grpcio-tools==1.81.0"
PROTOC_WRAPPER="$PROJECT_ROOT/tools/protoc"

if [[ "$(uname -s)" != Linux ]]; then
  echo "The FunctionFS worker build requires Linux." >&2
  exit 1
fi

"$PROJECT_ROOT/scripts/check-upstream.sh"

for tool in make pkg-config uv ar cargo; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Missing required build tool: $tool" >&2
    exit 1
  fi
done

if [[ ! -f "$DISPLAY_BACKENDS_DIR/Cargo.toml" ]]; then
  echo "Missing sibling display-backends checkout: $DISPLAY_BACKENDS_DIR" >&2
  exit 1
fi
if [[ ! -f "$USB_GADGET_SUPERVISOR_DIR/worker/usb_personality_ffi.h" ||
      ! -f "$USB_GADGET_SUPERVISOR_DIR/worker/usb_worker_protocol.h" ]]; then
  echo "Missing sibling usb-gadget-supervisor checkout: $USB_GADGET_SUPERVISOR_DIR" >&2
  exit 1
fi

PROTOC_VERSION="$("$PROTOC_WRAPPER" --version)"
if [[ "$PROTOC_VERSION" != "libprotoc 33.5" ]]; then
  echo "Trezor v1.14.1 requires protoc 33.5; $PROTOC_TOOL provides: $PROTOC_VERSION" >&2
  exit 1
fi
uv sync --directory "$UPSTREAM_DIR" --locked --no-dev
cargo build --release --locked --manifest-path "$DISPLAY_BACKENDS_DIR/Cargo.toml" --lib
cargo build --release --locked \
  --manifest-path "$USB_GADGET_SUPERVISOR_DIR/crates/usb-gadget-worker/Cargo.toml" --lib

mkdir -p "$PROJECT_ROOT/build"
export PATH="$PROJECT_ROOT/tools:$UPSTREAM_DIR/.venv/bin:$PATH"
if printf 'int main(void) { return 0; }\n' | \
  "${CC:-cc}" -Werror -Wno-error=unterminated-string-initialization \
    -x c -c -o /dev/null - >/dev/null 2>&1; then
  export CFLAGS="${CFLAGS:-} -Wno-error=unterminated-string-initialization"
fi
export EMULATOR=1
export DEBUG_LINK=0
export DISPLAY_BACKENDS_DIR
export USB_GADGET_SUPERVISOR_DIR

make -C "$LEGACY_DIR/firmware/protob" clean
make -C "$LEGACY_DIR/firmware" -f "$PROJECT_ROOT/mk/worker-firmware.mk" clean
make -C "$LEGACY_DIR" clean
make -C "$LEGACY_DIR/emulator" clean

make -C "$LEGACY_DIR/emulator" setup.o memory.o strl.o
"${CC:-cc}" ${CFLAGS:-} -DEMULATOR=1 -I"$LEGACY_DIR" \
  -I"$PROJECT_ROOT/platform/raspberry-pi" -MMD -MP \
  -o "$LEGACY_DIR/emulator/timer.o" -c \
  "$PROJECT_ROOT/platform/raspberry-pi/timer_linux.c"
# Build only the non-UI host facilities needed by the firmware worker. USB,
# display, buttons, and the suspend-aware timer are supplied by project
# platform objects.
ar rcs "$LEGACY_DIR/emulator/libemulator.a" \
  "$LEGACY_DIR/emulator/setup.o" \
  "$LEGACY_DIR/emulator/memory.o" \
  "$LEGACY_DIR/emulator/timer.o" \
  "$LEGACY_DIR/emulator/strl.o"

make -C "$LEGACY_DIR"
make -C "$LEGACY_DIR/firmware/protob"
make -C "$LEGACY_DIR/firmware" \
  -f "$PROJECT_ROOT/mk/worker-firmware.mk" \
  NAME=virtual-trezor-worker

ARTIFACT="$LEGACY_DIR/firmware/virtual-trezor-worker.elf"
test -x "$ARTIFACT"
WORKER_PATH="$PROJECT_ROOT/build/virtual-trezor-worker"
WORKER_STAGING="$(mktemp "$PROJECT_ROOT/build/.virtual-trezor-worker.XXXXXX")"
trap 'rm -f "$WORKER_STAGING"' EXIT
install -m 0755 "$ARTIFACT" "$WORKER_STAGING"
mv -f "$WORKER_STAGING" "$WORKER_PATH"
trap - EXIT
file "$WORKER_PATH"
sha256sum "$WORKER_PATH"
