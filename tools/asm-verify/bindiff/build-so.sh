#!/usr/bin/env bash
# Build fnverify.so from cross-compiled .o files.
# Runs inside the fnverify-bada Docker container.
set -euo pipefail

SRC=/staging
BUILD=/build

# Stage source (same as verify.sh) — HOST is the bind-mount path
HOST="${HOST:-/work}"
rsync -aq --delete --exclude=build --exclude=tmp --exclude=.git --exclude=node_modules --exclude=_deps "$HOST/" "$SRC/"
mkdir -p "$SRC/FruitNinjaBada/Bin"
cp "$HOST/tmp/FruitNinja_v1_6_1.exe" "$SRC/FruitNinjaBada/Bin/" 2>/dev/null || true

# Configure and build .o files (OBJECT library)
mkdir -p "$BUILD"
cd "$BUILD"
cmake -S "$SRC/tools/asm-verify/cross-build" -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE="$SRC/tools/asm-verify/toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -1

echo "=== Building .o files ==="
cmake --build . -j$(nproc) 2>&1 | tail -3

# Collect all .o files (skip demos)
ALL_O=$(find "$BUILD" -name '*.obj' -o -name '*.o' | grep -v 'fnverify_demo' | tr '\n' ' ')
O_COUNT=$(echo "$ALL_O" | wc -w)
echo "=== Linking $O_COUNT object files into fnverify.so ==="

# FN_ARM_MODE (default -marm) selects the link mode; it MUST match the compile
# mode toolchain.cmake used (toolchain.cmake reads the same FN_ARM_MODE env var),
# so the twin-build pipeline can produce an ARM and a Thumb .so from one script.
FLAGS="${FN_ARM_MODE:--marm} -mcpu=cortex-a8 -mfloat-abi=hard -mfpu=vfpv3"
arm-samsung-nucleuseabi-g++ $FLAGS -shared -Wl,--allow-shlib-undefined $ALL_O -o "$BUILD/fnverify.so" 2>&1 | tail -5
echo "Link exit: $?"

# Save the artifact to the host bind-mount IMMEDIATELY after linking, before the
# diagnostic pipelines below. A `... | head -N` closes the pipe early; under
# `set -o pipefail` the upstream `nm` gets SIGPIPE and the pipeline returns
# non-zero, which `set -e` would treat as fatal and abort BEFORE the copy.
# FN_SO_NAME (default fnverify.so) lets the twin pipeline write arm/thumb side by side.
OUT_SO="$HOST/tmp/${FN_SO_NAME:-fnverify.so}"
mkdir -p "$HOST/tmp"
cp "$BUILD/fnverify.so" "$OUT_SO"
echo ".so saved to $OUT_SO ($(stat -c%s "$OUT_SO") bytes, mode ${FN_ARM_MODE:--marm})"

echo ""
echo "=== Symbol counts ==="
arm-samsung-nucleuseabi-nm "$BUILD/fnverify.so" | grep ' T ' | wc -l
echo "defined"
arm-samsung-nucleuseabi-nm "$BUILD/fnverify.so" | grep ' U ' | wc -l
echo "undefined"

echo ""
echo "=== First 20 undefined ==="
arm-samsung-nucleuseabi-nm "$BUILD/fnverify.so" | grep ' U ' | head -20 || true
