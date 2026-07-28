#!/usr/bin/env bash
# In-container verifier. Driven by ../run.sh (host).
#
# Layout inside the container:
#   /work       (read-only bind-mount of the project, drvfs / 9p)
#   /staging    (named volume, ext4 -- where rsync stages the source)
#   /build      (named volume, ext4 -- cmake build dir)
#   /cache      (named volume, ext4 -- bada-binary symbol cache)
#
# The staging step is needed because the toolchain's i386 binaries can't
# stat() drvfs paths (32-bit inode overflow). Internal Docker volumes are
# ext4-backed so the toolchain runs cleanly.

set -euo pipefail

PROJECT=/work
SRC=/staging
BUILD=/build

# Mirror the project tree to ext4 (incremental rsync).
mkdir -p "$SRC"
rsync -aq --delete \
    --exclude=build --exclude='build-*/' --exclude=bada_SDK \
    --exclude=tmp --exclude=Testing --exclude=.git \
    --exclude=node_modules --exclude=_deps \
    "$PROJECT/" "$SRC/"

mkdir -p "$SRC/FruitNinjaBada/Bin"
# FruitNinjaBada/Bin/FruitNinja.exe is the canonical v1.6.1 binary and is
# included in the rsync above (only tmp/ is excluded), so no explicit copy needed.

# Pipeline env (Python tools read these).
export ASM_VERIFY_BINARY="$SRC/FruitNinjaBada/Bin/FruitNinja.exe"
# Use the Samsung Sourcery GCC 4.4.1 toolchain (matching the binary's compiler
# family -- the binary's .comment is Samsung build 4.4-261/4.4-327) from the
# ghcr.io/mariotaku/bada-sdk:1.1.0 base baked into the fnverify-bada image.
export FN_TOOLCHAIN_DIR=/opt/codesourcery
export ASM_VERIFY_NM="${FN_TOOLCHAIN_DIR}/bin/arm-samsung-nucleuseabi-nm"
export ASM_VERIFY_OBJDUMP="${FN_TOOLCHAIN_DIR}/bin/arm-samsung-nucleuseabi-objdump"
export ASM_VERIFY_BUILD_DIR="$BUILD"
export ASM_VERIFY_BIN_SYMBOL_DIR="/cache/symbols"
export ASM_VERIFY_REPORT_DIR="$SRC/tmp/asm-verify"
export ASM_VERIFY_MANIFEST_OUT="$SRC/tools/asm-verify/manifest.generated.toml"

echo "=== [1/4] cmake configure ==="
if [[ ! -f "$BUILD/Makefile" ]]; then
    # /build is a docker named volume; can't `rm -rf` the mount point itself.
    rm -rf "$BUILD"/* "$BUILD"/.* 2>/dev/null || true
    # NO -DCMAKE_BUILD_TYPE=Release: Release appends -O3, which overrides the
    # toolchain's -O2 and bloats large functions (SlashEntity::DrawSlice,
    # TimeControl) past the ARM VFP load/store +-1020-byte offset limit ->
    # "co-processor offset out of range" assembler error that killed the whole
    # sweep. The binary is -O2 and compile-one.sh diffs at -O2, so the toolchain's
    # -O2 (CMAKE_CXX_FLAGS_INIT) is both the fix AND the faithful, consistent flag.
    cmake -S "$SRC/tools/asm-verify/cross-build" -B "$BUILD" -G "Unix Makefiles" \
          -DCMAKE_TOOLCHAIN_FILE="$SRC/tools/asm-verify/toolchain.cmake" > /dev/null
fi

echo "=== [2/4] cmake build ==="
cmake --build "$BUILD" -j"$(nproc)"

echo "=== [3/4] discover + export ==="
python3 "$SRC/tools/asm-verify/discover-symbols.py"
python3 "$SRC/tools/asm-verify/export-binary-symbols.py"

mkdir -p "$ASM_VERIFY_REPORT_DIR"
echo "=== [4/4] asm-verify ==="
# Optional filter from host: ASM_VERIFY_FILTER (glob on mangled name).
# ASM_VERIFY_RESOLVE_OPERANDS is read straight out of the environment by
# asm-verify.py (run.sh exports it via `docker run -e`), so there is nothing to
# forward on the command line.
VERIFY_ARGS=(--report-only)
if [[ -n "${ASM_VERIFY_FILTER:-}" ]]; then
    echo "  filter: $ASM_VERIFY_FILTER"
    VERIFY_ARGS+=(--filter "$ASM_VERIFY_FILTER")
fi
python3 "$SRC/tools/asm-verify/asm-verify.py" "${VERIFY_ARGS[@]}"
