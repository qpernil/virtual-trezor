#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UPSTREAM_DIR="$PROJECT_ROOT/upstream/trezor-firmware"
LEGACY_DIR="$UPSTREAM_DIR/legacy"
PROTOC_BIN="${PROTOC_BIN:-$(command -v protoc || true)}"

"$PROJECT_ROOT/scripts/check-upstream.sh"

for tool in make pkg-config uv; do
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
  echo "The upstream baseline requires SDL2 and SDL2_image development files." >&2
  exit 1
fi

if [[ ! -e "$UPSTREAM_DIR/vendor/libopencm3/.git" ]]; then
  echo "The upstream baseline requires libopencm3 headers; run: make init-baseline" >&2
  exit 1
fi

TOOL_BIN="$PROJECT_ROOT/build/upstream-tools/bin"
mkdir -p "$TOOL_BIN"
ln -sfn "$PROTOC_BIN" "$TOOL_BIN/protoc"

uv sync --directory "$UPSTREAM_DIR" --locked --no-dev
make -C "$LEGACY_DIR/firmware/protob" clean

HOST_CFLAGS=""
if [[ "$(uname -s)" == "Darwin" ]]; then
  HOST_CFLAGS="-Wno-gnu-folding-constant -Wno-error=unterminated-string-initialization"
fi

env \
  PATH="$TOOL_BIN:$PATH" \
  CFLAGS="$HOST_CFLAGS" \
  EMULATOR=1 \
  DEBUG_LINK=1 \
  uv run --no-sync --directory "$UPSTREAM_DIR" "$LEGACY_DIR/script/cibuild"

ARTIFACT="$LEGACY_DIR/firmware/trezor.elf"
test -x "$ARTIFACT"
shasum -a 256 "$ARTIFACT"
