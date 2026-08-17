#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UPSTREAM_DIR="$PROJECT_ROOT/upstream/trezor-firmware"
LEGACY_DIR="$UPSTREAM_DIR/legacy"
PROTOC_BIN="${PROTOC_BIN:-$(command -v protoc || true)}"

if [[ "$(uname -s)" != Linux ]]; then
  echo "The FunctionFS worker build requires Linux." >&2
  exit 1
fi

"$PROJECT_ROOT/scripts/check-upstream.sh"

for tool in make pkg-config uv ar; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Missing required build tool: $tool" >&2
    exit 1
  fi
done

if [[ -z "$PROTOC_BIN" || ! -x "$PROTOC_BIN" ]]; then
  echo "Set PROTOC_BIN to an executable protoc 33.5 binary." >&2
  exit 1
fi
if [[ "$("$PROTOC_BIN" --version)" != "libprotoc 33.5" ]]; then
  echo "Trezor v1.14.1 requires protoc 33.5; found: $("$PROTOC_BIN" --version)" >&2
  exit 1
fi
if ! pkg-config --exists sdl2 SDL2_image; then
  echo "The initial worker requires SDL2 and SDL2_image development files." >&2
  exit 1
fi

uv sync --directory "$UPSTREAM_DIR" --locked --no-dev

TOOL_BIN="$PROJECT_ROOT/build/upstream-tools/bin"
mkdir -p "$TOOL_BIN" "$PROJECT_ROOT/build"
ln -sfn "$PROTOC_BIN" "$TOOL_BIN/protoc"
export PATH="$TOOL_BIN:$UPSTREAM_DIR/.venv/bin:$PATH"
export EMULATOR=1
export DEBUG_LINK=0

make -C "$LEGACY_DIR/firmware/protob" clean
make -C "$LEGACY_DIR/firmware" -f "$PROJECT_ROOT/mk/worker-firmware.mk" clean
make -C "$LEGACY_DIR" clean
make -C "$LEGACY_DIR/emulator" clean

make -C "$LEGACY_DIR/emulator"
# The upstream emulator archive supplies SDL, flash, timer, and buttons. Its
# UDP member is generated but intentionally removed from this derived archive.
ar d "$LEGACY_DIR/emulator/libemulator.a" udp.o

make -C "$LEGACY_DIR"
make -C "$LEGACY_DIR/firmware/protob"
make -C "$LEGACY_DIR/firmware" \
  -f "$PROJECT_ROOT/mk/worker-firmware.mk" \
  NAME=virtual-trezor-worker

ARTIFACT="$LEGACY_DIR/firmware/virtual-trezor-worker.elf"
test -x "$ARTIFACT"
install -m 0755 "$ARTIFACT" "$PROJECT_ROOT/build/virtual-trezor-worker"
file "$PROJECT_ROOT/build/virtual-trezor-worker"
sha256sum "$PROJECT_ROOT/build/virtual-trezor-worker"
