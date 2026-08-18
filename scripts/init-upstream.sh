#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UPSTREAM_DIR="$PROJECT_ROOT/upstream/trezor-firmware"

git -C "$PROJECT_ROOT" submodule update --init upstream/trezor-firmware

git -C "$UPSTREAM_DIR" submodule update --init \
  vendor/nanopb \
  vendor/QR-Code-generator \
  vendor/libopencm3 \
  vendor/secp256k1-zkp \
  vendor/ts-tvl

"$PROJECT_ROOT/scripts/check-upstream.sh"
