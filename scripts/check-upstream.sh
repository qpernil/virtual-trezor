#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UPSTREAM_DIR="$PROJECT_ROOT/upstream/trezor-firmware"
EXPECTED_COMMIT="30be4e8c9488eeab68f994af23b3d9c9b7334266"
EXPECTED_TAG="core/v2.12.4"

if [[ ! -e "$UPSTREAM_DIR/.git" ]]; then
  echo "Upstream firmware is not initialized; run: make init" >&2
  exit 1
fi

ACTUAL_COMMIT="$(git -C "$UPSTREAM_DIR" rev-parse HEAD)"
if [[ "$ACTUAL_COMMIT" != "$EXPECTED_COMMIT" ]]; then
  echo "Expected upstream $EXPECTED_COMMIT, found $ACTUAL_COMMIT" >&2
  exit 1
fi

ACTUAL_TAG="$(git -C "$UPSTREAM_DIR" describe --tags --exact-match 2>/dev/null || true)"
if [[ "$ACTUAL_TAG" != "$EXPECTED_TAG" ]]; then
  echo "Expected upstream tag $EXPECTED_TAG, found ${ACTUAL_TAG:-none}" >&2
  exit 1
fi

if [[ -n "$(git -C "$UPSTREAM_DIR" status --porcelain --untracked-files=no)" ]]; then
  echo "Upstream tracked files are modified; the integration requires a clean submodule." >&2
  exit 1
fi

echo "Upstream verified: $EXPECTED_TAG ($EXPECTED_COMMIT)"
