#!/bin/bash
# PS3 build orchestration (PSL1GHT).
#
# Usage:
#   bash tools/ps3/build.sh          # configure + build ELF
#   bash tools/ps3/build.sh pkg      # build ELF + TestBuildFruitNinja1.0.pkg
#
# Requires:
#   export PS3DEV=/usr/local/ps3dev
#   export PSL1GHT=$PS3DEV/psl1ght
#   export PATH=$PS3DEV/bin:$PS3DEV/ppu/bin:$PATH

set -eo pipefail

PROJ="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="$PROJ/build/ps3"
DO_PKG=0
if [ "${1:-}" = "pkg" ]; then
    DO_PKG=1
fi

if [ -z "${PS3DEV:-}" ] || [ ! -x "${PS3DEV}/ppu/bin/ppu-g++" ]; then
    echo "ERROR: PS3DEV not set or ppu-g++ not found." >&2
    echo "  export PS3DEV=/usr/local/ps3dev" >&2
    echo "  export PSL1GHT=\$PS3DEV/psl1ght" >&2
    echo "  export PATH=\$PS3DEV/bin:\$PS3DEV/ppu/bin:\$PATH" >&2
    exit 1
fi

if [ -z "${PSL1GHT:-}" ]; then
    echo "ERROR: PSL1GHT is not set." >&2
    exit 1
fi

echo "==> Configuring PS3 build in $BUILD_DIR"
cmake -B "$BUILD_DIR" -G "Unix Makefiles" \
  -DCMAKE_TOOLCHAIN_FILE="$PROJ/cmake/ps3.toolchain.cmake" \
  -DFRUIT_PLATFORM_PS3=ON \
  -DCMAKE_BUILD_TYPE=Release

echo "==> Building"
cmake --build "$BUILD_DIR" -j"$(nproc 2>/dev/null || echo 4)"

echo
echo "Scaffold build finished under $BUILD_DIR"

if [ "$DO_PKG" -eq 1 ]; then
    echo "==> Packaging TestBuildFruitNinja1.0.pkg"
    bash "$PROJ/tools/ps3/package.sh"
fi
