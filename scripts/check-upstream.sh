#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UPSTREAM_DIR="$PROJECT_ROOT/upstream/trezor-firmware"
EXPECTED_COMMIT="ace9215f2c09a43438fd2a33b11f9888355c5cd7"
UPSTREAM_COMMIT="037e506db454691bda188df2803df2fc26c75f36"
UPSTREAM_TAG="core/v2.13.0"

if [[ ! -e "$UPSTREAM_DIR/.git" ]]; then
  echo "Upstream firmware is not initialized; run: make init" >&2
  exit 1
fi

ACTUAL_COMMIT="$(git -C "$UPSTREAM_DIR" rev-parse HEAD)"
if [[ "$ACTUAL_COMMIT" != "$EXPECTED_COMMIT" ]]; then
  echo "Expected firmware fork $EXPECTED_COMMIT, found $ACTUAL_COMMIT" >&2
  exit 1
fi

TAG_COMMIT="$(git -C "$UPSTREAM_DIR" rev-parse "$UPSTREAM_TAG^{commit}")"
if [[ "$TAG_COMMIT" != "$UPSTREAM_COMMIT" ]] ||
   ! git -C "$UPSTREAM_DIR" merge-base --is-ancestor "$UPSTREAM_COMMIT" HEAD; then
  echo "Firmware fork must descend from $UPSTREAM_TAG ($UPSTREAM_COMMIT)" >&2
  exit 1
fi

if [[ -n "$(git -C "$UPSTREAM_DIR" status --porcelain --untracked-files=no)" ]]; then
  echo "Upstream tracked files are modified; the integration requires a clean submodule." >&2
  exit 1
fi

echo "Firmware fork verified: $EXPECTED_COMMIT (base $UPSTREAM_TAG)"
