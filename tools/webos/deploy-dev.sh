#!/bin/bash
# Install a BUILT webOS .ipk onto a device via ares-install. This is the ONLY
# step that touches a real/emulated device -- it is NOT part of the build (see
# tools/webos/build.sh, which stops at producing the .ipk). Run this
# separately, after a build, whenever you want to test on-device.
#
# Usage (from WSL):
#   bash tools/webos/deploy-dev.sh [path-to.ipk]
#
#   path-to.ipk   defaults to the newest dist/com.halfbrick.fruitninja_*_arm.ipk
#                 (the output of tools/webos/build.sh).
#
# Env overrides:
#   ARES_DEVICE   Device name registered with `ares-setup-device`. If unset,
#                 ares-install uses its own default device.
set -eo pipefail

PROJ="$(cd "$(dirname "$0")/../.." && pwd)"
DIST_DIR="${DIST_DIR:-$PROJ/dist}"

IPK="${1:-}"
if [ -z "$IPK" ]; then
    IPK="$(find "$DIST_DIR" -maxdepth 1 -name 'com.halfbrick.fruitninja_*_arm.ipk' | sort | tail -n1)"
fi

if [ -z "$IPK" ] || [ ! -f "$IPK" ]; then
    echo "ERROR: no .ipk found. Build first:" >&2
    echo "  bash tools/webos/build.sh" >&2
    exit 1
fi

if ! command -v ares-install >/dev/null 2>&1; then
    echo "ERROR: ares-install not found on PATH (webosbrew/ares-cli-rs)." >&2
    exit 1
fi

ARGS=()
if [ -n "$ARES_DEVICE" ]; then
    ARGS+=(-d "$ARES_DEVICE")
fi

ares-install "${ARGS[@]}" "$IPK"
echo "Installed $IPK"
