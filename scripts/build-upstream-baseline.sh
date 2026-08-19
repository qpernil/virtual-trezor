#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UPSTREAM_DIR="$PROJECT_ROOT/upstream/trezor-firmware"
LEGACY_DIR="$UPSTREAM_DIR/legacy"
PROTOC_TOOL="grpcio-tools==1.81.0"
PROTOC_WRAPPER="$PROJECT_ROOT/tools/protoc"

"$PROJECT_ROOT/scripts/check-upstream.sh"

for tool in make pkg-config uv; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Missing required build tool: $tool" >&2
    exit 1
  fi
done

PROTOC_VERSION="$("$PROTOC_WRAPPER" --version)"
if [[ "$PROTOC_VERSION" != "libprotoc 33.5" ]]; then
  echo "Trezor v1.14.1 requires protoc 33.5; $PROTOC_TOOL provides: $PROTOC_VERSION" >&2
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

uv sync --directory "$UPSTREAM_DIR" --locked --no-dev
make -C "$LEGACY_DIR/firmware/protob" clean

HOST_CFLAGS=""
if [[ "$(uname -s)" == "Darwin" ]]; then
  HOST_CFLAGS="-Wno-gnu-folding-constant"
fi
if printf 'int main(void) { return 0; }\n' | \
  "${CC:-cc}" -Werror -Wno-error=unterminated-string-initialization \
    -x c -c -o /dev/null - >/dev/null 2>&1; then
  HOST_CFLAGS="$HOST_CFLAGS -Wno-error=unterminated-string-initialization"
fi

env \
  PATH="$PROJECT_ROOT/tools:$PATH" \
  CFLAGS="$HOST_CFLAGS" \
  EMULATOR=1 \
  DEBUG_LINK=1 \
  uv run --no-sync --directory "$UPSTREAM_DIR" "$LEGACY_DIR/script/cibuild"

ARTIFACT="$LEGACY_DIR/firmware/trezor.elf"
test -x "$ARTIFACT"
shasum -a 256 "$ARTIFACT"
