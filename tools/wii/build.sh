#!/bin/bash
# Wii build orchestration: build -> install -> package.
#
# elf2dol (boot.dol) and the prebaked-font bake now run as part of the CMake
# build itself (POST_BUILD on the fruit-ninja target / fn_wii_fonts, see
# CMakeLists.txt), so a plain `cmake --build` is reliable -- no more separate
# mtime-guarded elf2dol step here. This script just drives the standard
# install + CPack flow on top of that and tells you where the output landed.
#
# Deploying to Dolphin is a SEPARATE, opt-in step -- see
# tools/wii/deploy-dolphin.sh. This script never touches AppData.
#
# Usage (from the devkitPro MSYS2 shell -- see the env guard below):
#   bash tools/wii/build.sh
#
# -e: exit on any non-zero. -o pipefail: a pipeline fails if ANY stage fails.
set -eo pipefail

# Project root + build dir, derived from this script's own location so the
# script works from any checkout, not just one hardcoded machine path.
PROJ="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="$PROJ/build/wii"
DIST_DIR="$BUILD_DIR/dist"

# Env guard: this build only works inside the devkitPro MSYS2 shell, where the
# toolchain is mounted at /opt/devkitpro (the CMake cache hardcodes those paths).
# Running it from Git Bash / plain MSYS2 gives cryptic "elf2dol: not found" +
# CMakeCache path-mismatch errors -- fail fast with a clear pointer instead.
if [ ! -x /opt/devkitpro/devkitPPC/bin/powerpc-eabi-g++.exe ]; then
    echo "ERROR: devkitPPC not found at /opt/devkitpro." >&2
    echo "Run this from the devkitPro MSYS2 shell, NOT Git Bash:" >&2
    echo "  Start Menu -> 'devkitPro' -> 'MSYS2' (or C:\\devkitPro\\msys2\\msys2_shell.cmd)" >&2
    echo "  then: bash tools/wii/build.sh" >&2
    exit 1
fi

export PATH=/opt/devkitpro/tools/bin:/opt/devkitpro/devkitPPC/bin:/usr/bin:$PATH
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC
export TMP=/tmp TEMP=/tmp TMPDIR=/tmp

cmake --build "$BUILD_DIR" -j8
cmake --install "$BUILD_DIR" --prefix "$DIST_DIR"
(cd "$BUILD_DIR" && cpack -G ZIP)

echo
echo "Shippable package: $DIST_DIR/apps/fruitninja/"
echo "Zip (unzip onto SD/USB root): $BUILD_DIR/fruit-ninja-wii.zip"
echo
echo "To test in Dolphin: bash tools/wii/deploy-dolphin.sh"
