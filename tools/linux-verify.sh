#!/usr/bin/env bash
# End-to-end asm-verify pipeline driven entirely from inside WSL.
#
# Run via:
#   wsl.exe -d Debian -- bash /c/Users/.../tools/linux-verify.sh
# or, from within WSL:
#   bash /c/Users/.../tools/linux-verify.sh
#
# Pre-requisites (one-time):
#   - Toolchain at $TC_DIR (Sourcery G++ Lite 2010q1 / arm-none-eabi-gcc 4.4.1).
#   - WSL Debian with libc6:i386 + libstdc++6:i386 installed.
#   - rsync available.

set -euo pipefail

PROJECT_ROOT_WIN="/c/Users/Mariotaku/Projects/webosbrew/fruit-ninja"
SRC_DIR="$HOME/fn-src"
BUILD_DIR="$HOME/fn-build"
TC_DIR="$HOME/toolchain/sourcery-2010q1"

export ASM_VERIFY_BINARY="$SRC_DIR/FruitNinjaBada/Bin/FruitNinja.exe"
export ASM_VERIFY_NM="$TC_DIR/bin/arm-none-eabi-nm"
export ASM_VERIFY_OBJDUMP="$TC_DIR/bin/arm-none-eabi-objdump"
export ASM_VERIFY_BUILD_DIR="$BUILD_DIR"
export ASM_VERIFY_BIN_SYMBOL_DIR="$SRC_DIR/bada-binary/symbols"
export ASM_VERIFY_REPORT_DIR="$SRC_DIR/tmp/asm-verify"
export ASM_VERIFY_MANIFEST_OUT="$SRC_DIR/tools/asm-verify-manifest.generated.toml"

echo "=== [1/5] sync project tree to ext4 ==="
mkdir -p "$SRC_DIR"
rsync -aq --delete \
    --exclude=build --exclude='build-*/' --exclude=bada_SDK \
    --exclude=tmp --exclude=Testing --exclude=.git \
    --exclude=node_modules --exclude=_deps \
    "$PROJECT_ROOT_WIN/" "$SRC_DIR/"

mkdir -p "$SRC_DIR/build/_deps/tinyxml2-src"
rsync -aq "$PROJECT_ROOT_WIN/build/_deps/tinyxml2-src/" \
          "$SRC_DIR/build/_deps/tinyxml2-src/"

mkdir -p "$SRC_DIR/FruitNinjaBada/Bin"
cp -u "$PROJECT_ROOT_WIN/FruitNinjaBada/Bin/FruitNinja.exe" \
      "$SRC_DIR/FruitNinjaBada/Bin/"

echo "=== [2/5] cmake configure (4.4.1 toolchain) ==="
if [[ ! -f "$BUILD_DIR/Makefile" ]]; then
    rm -rf "$BUILD_DIR"
    cmake -S "$SRC_DIR/cross-build" -B "$BUILD_DIR" -G "Unix Makefiles" \
          -DCMAKE_TOOLCHAIN_FILE="$SRC_DIR/cmake/toolchain-arm-bada-linux.cmake" \
          -DCMAKE_BUILD_TYPE=Release > /dev/null
fi

echo "=== [3/5] cmake build ==="
cmake --build "$BUILD_DIR" -j$(nproc)

echo "=== [4/5] discover + export ==="
python3 "$SRC_DIR/tools/discover-symbols.py"
python3 "$SRC_DIR/tools/export-binary-symbols.py"

echo "=== [5/5] asm-verify ==="
python3 "$SRC_DIR/tools/asm-verify.py" --report-only

# Copy report back to /c/ so the user can read it from Windows.
mkdir -p "$PROJECT_ROOT_WIN/tmp/asm-verify"
cp "$SRC_DIR/tmp/asm-verify/report.md" "$PROJECT_ROOT_WIN/tmp/asm-verify/report-441.md"
echo
echo "Report: tmp/asm-verify/report-441.md"
