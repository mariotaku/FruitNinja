#!/bin/bash
# webOS TV build orchestration: configure -> build -> install -> ares-package.
#
# Mirrors tools/wii/build.sh's shape (build -> install -> package), swapping
# CPack (Wii ships a plain ZIP) for ares-package (webOS's .ipk packager) since
# there's no CPack "External" generator wired for this platform (see the
# FRUIT_PLATFORM_WEBOS install() rules in the root CMakeLists.txt -- they stop
# at assembling a flat app-root dir; this script does the ares-package step).
#
# Runs under the arm-webos-linux-gnueabi buildroot cross-toolchain (WSL /
# Linux only -- no Windows toolchain for this target). Needs:
#   - WEBOS_SDK toolchain (relocate-sdk.sh already run)
#   - a Python3 venv with the packages tools/assets/{stage-assets,svg-to-webp}
#     need (Pillow, resvg-py/py-resvg, fonttools) -- see FN_WEBOS_VENV below
#   - ares-package on PATH (webosbrew/ares-cli-rs)
#
# Usage (from WSL, repo root):
#   bash tools/webos/build.sh
#
# Env overrides:
#   WEBOS_SDK      webOS buildroot toolchain root.
#                  Default: /opt/arm-webos-linux-gnueabi_sdk-buildroot
#   FN_WEBOS_VENV  Python3 venv with the asset-staging deps.
#                  Default: $HOME/.venvs/fn-webos
#   BUILD_DIR      CMake build tree. Default: build/webos
#   DIST_DIR       Where the assembled app root + .ipk land. Default: dist
#   FN_APP_VERSION appinfo.json version, and hence the .ipk filename.
#                  Unset: the CMake default (FN_APP_VERSION in the root
#                  CMakeLists.txt). CI derives it from the release tag.
#
# -e: exit on any non-zero. -o pipefail: a pipeline fails if ANY stage fails.
set -eo pipefail

PROJ="$(cd "$(dirname "$0")/../.." && pwd)"

WEBOS_SDK="${WEBOS_SDK:-/opt/arm-webos-linux-gnueabi_sdk-buildroot}"
FN_WEBOS_VENV="${FN_WEBOS_VENV:-$HOME/.venvs/fn-webos}"
BUILD_DIR="${BUILD_DIR:-$PROJ/build/webos}"
DIST_DIR="${DIST_DIR:-$PROJ/dist}"
APP_DIR="$BUILD_DIR/approot"

TOOLCHAIN_FILE="$WEBOS_SDK/share/buildroot/toolchainfile.cmake"
if [ ! -f "$TOOLCHAIN_FILE" ]; then
    echo "ERROR: webOS toolchain file not found at $TOOLCHAIN_FILE" >&2
    echo "Set WEBOS_SDK to the extracted+relocated buildroot SDK root." >&2
    exit 1
fi

VENV_PYTHON="$FN_WEBOS_VENV/bin/python3"
if [ ! -x "$VENV_PYTHON" ]; then
    echo "ERROR: venv python not found at $VENV_PYTHON" >&2
    echo "Create it first, e.g.:" >&2
    echo "  python3 -m venv --system-site-packages $FN_WEBOS_VENV" >&2
    echo "  $FN_WEBOS_VENV/bin/pip install Pillow resvg-py fonttools" >&2
    exit 1
fi

if ! command -v ares-package >/dev/null 2>&1; then
    echo "ERROR: ares-package not found on PATH (webosbrew/ares-cli-rs)." >&2
    exit 1
fi

# Pass -DFN_APP_VERSION only when the caller set one. An unconditional
# -DFN_APP_VERSION="$FN_APP_VERSION" would write an empty string into the cache
# and ship a version-less appinfo.json, instead of leaving CMake's default.
CMAKE_EXTRA_ARGS=()
if [ -n "${FN_APP_VERSION:-}" ]; then
    CMAKE_EXTRA_ARGS+=(-DFN_APP_VERSION="$FN_APP_VERSION")
fi

cmake -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
    -DFRUIT_GL_API=ES2 \
    -DFRUIT_PLATFORM_WEBOS=ON \
    -DPython3_EXECUTABLE="$VENV_PYTHON" \
    "${CMAKE_EXTRA_ARGS[@]}"

cmake --build "$BUILD_DIR" --target fruit-ninja -j"$(nproc)"

rm -rf "$APP_DIR"
# --component fruitninja: install ONLY the app's own install() rules
# (binary, Data/, appinfo.json, icons) and skip the default-component
# install() rules that FetchContent deps (tinyxml2, libwebp) register for
# themselves -- libwebp's errors ("file INSTALL cannot find ...") because
# we only build its decoder, not the full lib set its install() rule expects.
cmake --install "$BUILD_DIR" --prefix "$APP_DIR" --component fruitninja

mkdir -p "$DIST_DIR"
ares-package "$APP_DIR" -o "$DIST_DIR"

IPK="$(find "$DIST_DIR" -maxdepth 1 -name 'com.halfbrick.fruitninja_*_arm.ipk' -newer "$APP_DIR/appinfo.json" | head -n1)"
if [ -z "$IPK" ]; then
    IPK="$(find "$DIST_DIR" -maxdepth 1 -name 'com.halfbrick.fruitninja_*_arm.ipk' | sort | tail -n1)"
fi

echo
echo "App root:     $APP_DIR"
echo "Package:      ${IPK:-$DIST_DIR (see above for the .ipk filename)}"
echo
echo "To install on a device: bash tools/webos/deploy-dev.sh \"${IPK:-<path-to-ipk>}\""
