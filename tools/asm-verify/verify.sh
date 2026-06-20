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

mkdir -p "$SRC/build/_deps/tinyxml2-src"
rsync -aq "$PROJECT/build/_deps/tinyxml2-src/" "$SRC/build/_deps/tinyxml2-src/"

mkdir -p "$SRC/FruitNinjaBada/Bin"
# v1.6.1 verification target (port + RE both target v1.6.1). tmp/ is excluded
# from the staging rsync, but /work (the read-only project bind-mount) has it;
# copy onto the ext4 staging volume so the i386 objdump can stat() it.
cp -u "$PROJECT/tmp/FruitNinja_v1_6_1.exe" "$SRC/FruitNinjaBada/Bin/"

# Pipeline env (Python tools read these).
export ASM_VERIFY_BINARY="$SRC/FruitNinjaBada/Bin/FruitNinja_v1_6_1.exe"
# Use Samsung Sourcery 4.4-157 (binary's actual compiler) from the
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
    cmake -S "$SRC/tools/asm-verify/cross-build" -B "$BUILD" -G "Unix Makefiles" \
          -DCMAKE_TOOLCHAIN_FILE="$SRC/tools/asm-verify/toolchain.cmake" \
          -DCMAKE_BUILD_TYPE=Release > /dev/null
fi

echo "=== [2/4] cmake build ==="
cmake --build "$BUILD" -j"$(nproc)"

echo "=== [3/4] discover + export ==="
python3 "$SRC/tools/asm-verify/discover-symbols.py"
python3 "$SRC/tools/asm-verify/export-binary-symbols.py"

mkdir -p "$ASM_VERIFY_REPORT_DIR"
echo "=== [4/4] asm-verify ==="
# Optional filter from host: ASM_VERIFY_FILTER (glob on mangled name).
if [[ -n "${ASM_VERIFY_FILTER:-}" ]]; then
    echo "  filter: $ASM_VERIFY_FILTER"
    python3 "$SRC/tools/asm-verify/asm-verify.py" --report-only --filter "$ASM_VERIFY_FILTER"
else
    python3 "$SRC/tools/asm-verify/asm-verify.py" --report-only
fi
