#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UPSTREAM_DIR="$PROJECT_ROOT/upstream/trezor-firmware"
CORE_DIR="$UPSTREAM_DIR/core"
SAFE3_BUILD_DIR="${SAFE3_BUILD_DIR:-$PROJECT_ROOT/build/safe3-t3b1}"
SAFE3_MODEL="${TREZOR_MODEL:-T3B1}"
SAFE3_UNIX_DIR="$SAFE3_BUILD_DIR/unix"
SAFE3_CARGO_TARGET="$SAFE3_BUILD_DIR/cargo-target"

if [[ "$SAFE3_MODEL" != "T3B1" ]]; then
  echo "Safe 3 baseline requires TREZOR_MODEL=T3B1, got $SAFE3_MODEL" >&2
  exit 1
fi

"$PROJECT_ROOT/scripts/check-upstream.sh"

for tool in cargo make rustc uv; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Missing required Safe 3 build tool: $tool" >&2
    exit 1
  fi
done

# Core uses a selectively initialized subset of the pinned upstream
# submodules. MicroPython itself contains nested library submodules, hence the
# recursive update for this exact list rather than the whole upstream tree.
git -C "$UPSTREAM_DIR" submodule update --init --recursive \
  vendor/micropython \
  vendor/nanopb \
  vendor/QR-Code-generator \
  vendor/secp256k1-zkp \
  vendor/ts-tvl

RUST_VERSION="$(rustc --version)"
if [[ "$RUST_VERSION" == *nightly* ]]; then
  echo "Safe 3 project builds use the global stable Rust toolchain; got: $RUST_VERSION" >&2
  exit 1
fi

uv sync --directory "$UPSTREAM_DIR" --locked --no-dev

mkdir -p "$SAFE3_UNIX_DIR" "$SAFE3_CARGO_TARGET"

export PATH="$PROJECT_ROOT/tools/safe3-rust:$PROJECT_ROOT/tools:$UPSTREAM_DIR/.venv/bin:$PATH"
export CARGO_TARGET_DIR="$SAFE3_CARGO_TARGET"
export CFLAGS="${CFLAGS:+$CFLAGS }-Wno-error=unterminated-string-initialization"

make -C "$CORE_DIR" TREZOR_MODEL=T3B1 PYOPT=0 build_unix

SAFE3_ARTIFACT="$SAFE3_CARGO_TARGET/artifacts/T3B1/firmware-emu"
SAFE3_BINARY="$SAFE3_UNIX_DIR/trezor-emu-core"
test -x "$SAFE3_ARTIFACT"
install -m 0755 "$SAFE3_ARTIFACT" "$SAFE3_BINARY"
test -x "$SAFE3_BINARY"

git -C "$UPSTREAM_DIR" rev-parse HEAD >"$SAFE3_BUILD_DIR/upstream-revision"
printf '%s\n' "$RUST_VERSION" >"$SAFE3_BUILD_DIR/rust-version"

file "$SAFE3_BINARY"
sha256sum "$SAFE3_BINARY" 2>/dev/null || shasum -a 256 "$SAFE3_BINARY"
