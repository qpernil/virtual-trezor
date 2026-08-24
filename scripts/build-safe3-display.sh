#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UPSTREAM_DIR="$PROJECT_ROOT/upstream/trezor-firmware"
CORE_DIR="$UPSTREAM_DIR/core"
DISPLAY_BACKENDS_DIR="${DISPLAY_BACKENDS_DIR:-$PROJECT_ROOT/../display-backends}"
DISPLAY_BUILD_DIR="${SAFE3_DISPLAY_BUILD_DIR:-$PROJECT_ROOT/build/safe3-t3b1-display}"
DISPLAY_CARGO_TARGET="$DISPLAY_BUILD_DIR/cargo-target"
OVERLAY_PATCH="$PROJECT_ROOT/patches/safe3-headless-display.patch"
RANDOM_PATCH="$PROJECT_ROOT/patches/safe3-secure-random.patch"
DISPLAY_LIBRARY="$DISPLAY_BACKENDS_DIR/target/release/libdisplay_backends.so"
BUTTON_SOURCE="${SAFE3_BUTTON_SOURCE:-$PROJECT_ROOT/platform/safe3/button_headless.c}"
BUTTON_RESOURCES_SOURCE="${SAFE3_BUTTON_RESOURCES_SOURCE:-}"
OUTPUT_NAME="${SAFE3_OUTPUT_NAME:-virtual-trezor-safe3-display}"
OUTPUT="$DISPLAY_BUILD_DIR/$OUTPUT_NAME"

"$PROJECT_ROOT/scripts/check-upstream.sh"

if [[ "$(uname -s)" != Linux ]]; then
  echo "The physical Safe 3 display target is Linux-only; use safe3-baseline on macOS." >&2
  exit 1
fi

for tool in cargo make nm rustc strings uv; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Missing required Safe 3 build tool: $tool" >&2
    exit 1
  fi
done

git -C "$UPSTREAM_DIR" submodule update --init --recursive \
  vendor/micropython \
  vendor/nanopb \
  vendor/QR-Code-generator \
  vendor/secp256k1-zkp \
  vendor/ts-tvl

RUST_VERSION="$(rustc --version)"
if [[ "$RUST_VERSION" == *nightly* ]]; then
  echo "Safe 3 project builds use global stable Rust; got: $RUST_VERSION" >&2
  exit 1
fi

uv sync --directory "$UPSTREAM_DIR" --locked --no-dev
cargo build --manifest-path "$DISPLAY_BACKENDS_DIR/Cargo.toml" \
  --release --locked
test -f "$DISPLAY_LIBRARY"

mkdir -p "$DISPLAY_BUILD_DIR" "$DISPLAY_CARGO_TARGET"

git -C "$UPSTREAM_DIR" apply --check "$OVERLAY_PATCH" "$RANDOM_PATCH"
git -C "$UPSTREAM_DIR" apply "$OVERLAY_PATCH" "$RANDOM_PATCH"
overlay_applied=true
cleanup() {
  if [[ "${overlay_applied:-false}" == true ]]; then
    git -C "$UPSTREAM_DIR" apply -R "$RANDOM_PATCH" "$OVERLAY_PATCH"
  fi
}
trap cleanup EXIT

export PATH="$PROJECT_ROOT/tools/safe3-rust:$PROJECT_ROOT/tools:$UPSTREAM_DIR/.venv/bin:$PATH"
export CARGO_TARGET_DIR="$DISPLAY_CARGO_TARGET"
export CFLAGS="${CFLAGS:+$CFLAGS }-Wno-error=unterminated-string-initialization"
export VIRTUAL_TREZOR_HEADLESS_DISPLAY=1
export VIRTUAL_TREZOR_DISPLAY_SOURCE="$PROJECT_ROOT/platform/safe3/display_core.c"
export VIRTUAL_TREZOR_DISPLAY_RESOURCES_SOURCE="$PROJECT_ROOT/platform/safe3/display_resources.c"
export VIRTUAL_TREZOR_DISPLAY_CONFIG_SOURCE="$PROJECT_ROOT/platform/raspberry-pi/worker_config.c"
export VIRTUAL_TREZOR_BUTTON_SOURCE="$BUTTON_SOURCE"
export VIRTUAL_TREZOR_BUTTON_RESOURCES_SOURCE="$BUTTON_RESOURCES_SOURCE"
export VIRTUAL_TREZOR_DISPLAY_INCLUDE="$DISPLAY_BACKENDS_DIR/include"
export VIRTUAL_TREZOR_DISPLAY_CONFIG_INCLUDE="$PROJECT_ROOT/platform/raspberry-pi"
export VIRTUAL_TREZOR_DISPLAY_LIBRARY="$DISPLAY_LIBRARY"

(
  cd "$CORE_DIR/embed"
  cargo xtask build firmware --emulator --model T3B1 --pyopt false \
    --disable-animation --debug-link false
)

CORE_OUTPUT="$DISPLAY_CARGO_TARGET/artifacts/T3B1/firmware-emu"
test -x "$CORE_OUTPUT"
if nm "$CORE_OUTPUT" | grep -E '[[:space:]]random_reseed$' >/dev/null; then
  echo "Safe 3 worker unexpectedly contains the deterministic RNG reseed API" >&2
  exit 1
fi
if ! strings "$CORE_OUTPUT" | grep -F '/dev/urandom' >/dev/null; then
  echo "Safe 3 worker does not contain the secure Unix RNG path" >&2
  exit 1
fi
install -m 0755 "$CORE_OUTPUT" "$OUTPUT"
install -m 0755 "$DISPLAY_LIBRARY" "$DISPLAY_BUILD_DIR/libdisplay_backends.so"

git -C "$UPSTREAM_DIR" rev-parse HEAD >"$DISPLAY_BUILD_DIR/upstream-revision"
printf '%s\n' "$RUST_VERSION" >"$DISPLAY_BUILD_DIR/rust-version"

if command -v ldd >/dev/null 2>&1; then
  if ldd "$OUTPUT" | grep -qi SDL; then
    echo "Headless Safe 3 display build unexpectedly links SDL" >&2
    exit 1
  fi
elif command -v otool >/dev/null 2>&1; then
  if otool -L "$OUTPUT" | grep -qi SDL; then
    echo "Headless Safe 3 display build unexpectedly links SDL" >&2
    exit 1
  fi
fi

file "$OUTPUT"
sha256sum "$OUTPUT" 2>/dev/null || shasum -a 256 "$OUTPUT"
