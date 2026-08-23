#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

export SAFE3_BUTTON_SOURCE="$PROJECT_ROOT/platform/safe3/button_gpio.c"
export SAFE3_BUTTON_RESOURCES_SOURCE="$PROJECT_ROOT/platform/safe3/button_resources.c"
export SAFE3_OUTPUT_NAME="virtual-trezor-safe3-input"

exec "$PROJECT_ROOT/scripts/build-safe3-display.sh"
