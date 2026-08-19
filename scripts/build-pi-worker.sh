#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UPSTREAM_DIR="$PROJECT_ROOT/upstream/trezor-firmware"
LEGACY_DIR="$UPSTREAM_DIR/legacy"
PROTOC_TOOL="grpcio-tools==1.81.0"
PROTOC_ENTRYPOINT="python-grpc-tools-protoc"
PROTOC_COMMAND="uv tool run --quiet --from $PROTOC_TOOL $PROTOC_ENTRYPOINT"

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

PROTOC_VERSION="$(uv tool run --quiet --from "$PROTOC_TOOL" "$PROTOC_ENTRYPOINT" --version)"
if [[ "$PROTOC_VERSION" != "libprotoc 33.5" ]]; then
  echo "Trezor v1.14.1 requires protoc 33.5; $PROTOC_TOOL provides: $PROTOC_VERSION" >&2
  exit 1
fi
uv sync --directory "$UPSTREAM_DIR" --locked --no-dev

mkdir -p "$PROJECT_ROOT/build"
export PATH="$UPSTREAM_DIR/.venv/bin:$PATH"
export PROTOC="$PROTOC_COMMAND"
if printf 'int main(void) { return 0; }\n' | \
  "${CC:-cc}" -Werror -Wno-error=unterminated-string-initialization \
    -x c -c -o /dev/null - >/dev/null 2>&1; then
  export CFLAGS="${CFLAGS:-} -Wno-error=unterminated-string-initialization"
fi
export EMULATOR=1
export DEBUG_LINK=0

make -C "$LEGACY_DIR/firmware/protob" clean
make -C "$LEGACY_DIR/firmware" -f "$PROJECT_ROOT/mk/worker-firmware.mk" clean
make -C "$LEGACY_DIR" clean
make -C "$LEGACY_DIR/emulator" clean

make -C "$LEGACY_DIR/emulator" setup.o memory.o timer.o strl.o
# Build only the non-UI host facilities needed by the firmware worker. USB,
# display, and buttons are supplied entirely by project platform objects.
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
install -m 0755 "$ARTIFACT" "$PROJECT_ROOT/build/virtual-trezor-worker"
file "$PROJECT_ROOT/build/virtual-trezor-worker"
sha256sum "$PROJECT_ROOT/build/virtual-trezor-worker"
