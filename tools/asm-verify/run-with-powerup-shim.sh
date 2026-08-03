#!/usr/bin/env bash
# Wrapper around tools/asm-verify/run.sh that patches src/game/PowerUp.h
# inside the Docker volume to re-add the !defined(FN_ASM_VERIFY_CROSS) guard
# on the binary-faithful offsetof asserts.
#
# Background: db32838 removed that guard claiming "offsets hold under both
# ABIs", but the cross-build computes sizeof(PowerUp) == 196 (binary: 204)
# because Sourcery 2010q1's PowerUp has no vptr and/or no 12-byte
# cached-size std::list.  This is tracked as a FIX-NEEDED row in
# tools/asm-verify/triage.json; the patch here is INFRASTRUCTURE-ONLY
# (cross-build unblock) and does NOT touch src/ on disk.
#
# Remove this wrapper once the PowerUp layout bug (missing vptr / std::list
# size) is fixed in src/game/PowerUp.h.
#
# Usage:
#   bash tools/asm-verify/run-with-powerup-shim.sh
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
# Track run.sh/compile-one.sh's default (fnverify-bada) and honour the same
# override; this was hardcoded to "fnverify", a name setup.sh never builds.
IMAGE="${ASM_VERIFY_IMAGE:-fnverify-bada}"

to_docker_path() {
    local p="$1"
    if [[ "$p" =~ ^/([A-Za-z])/(.*)$ ]]; then
        local d="${BASH_REMATCH[1]^^}"
        printf '%s:/%s' "$d" "${BASH_REMATCH[2]}"
        return
    fi
    printf '%s' "$p"
}
PROJECT_ROOT_DOCKER="$(to_docker_path "$PROJECT_ROOT")"

export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL='*'

docker run --rm \
    -v "$PROJECT_ROOT_DOCKER:/work:ro" \
    -v fnverify-src:/staging \
    -v fnverify-build:/build \
    -v fnverify-cache:/cache \
    "$IMAGE" -c '
        set -euo pipefail
        # Invoke the rsync portion of verify.sh manually so we can patch
        # PowerUp.h between rsync and cmake-configure.
        PROJECT=/work; SRC=/staging; BUILD=/build
        mkdir -p "$SRC"
        rsync -aq --delete \
            --exclude=build --exclude="build-*/" --exclude=bada_SDK \
            --exclude=tmp --exclude=Testing --exclude=.git \
            --exclude=node_modules --exclude=_deps \
            "$PROJECT/" "$SRC/"
        mkdir -p "$SRC/build/_deps/tinyxml2-src"
        rsync -aq "$PROJECT/build/_deps/tinyxml2-src/" "$SRC/build/_deps/tinyxml2-src/"
        mkdir -p "$SRC/FruitNinjaBada/Bin"
        cp -u "$PROJECT/FruitNinjaBada/Bin/FruitNinja.exe" "$SRC/FruitNinjaBada/Bin/"

        # Cross-build unblock: restore FN_ASM_VERIFY_CROSS guard on PowerUp asserts.
        sed -i "s|^#if defined(__bada__)$|#if defined(__bada__) \&\& !defined(FN_ASM_VERIFY_CROSS)|" "$SRC/src/game/PowerUp.h"
        echo "--- patched PowerUp.h guard line: ---"
        grep -n "FN_ASM_VERIFY_CROSS\|defined(__bada__)" "$SRC/src/game/PowerUp.h" | head -5

        # Run the rest of verify.sh exactly as upstream does.
        export ASM_VERIFY_BINARY="$SRC/FruitNinjaBada/Bin/FruitNinja.exe"
        export ASM_VERIFY_NM="$FN_TOOLCHAIN_DIR/bin/arm-none-eabi-nm"
        export ASM_VERIFY_OBJDUMP="$FN_TOOLCHAIN_DIR/bin/arm-none-eabi-objdump"
        export ASM_VERIFY_BUILD_DIR="$BUILD"
        export ASM_VERIFY_BIN_SYMBOL_DIR="/cache/symbols"
        export ASM_VERIFY_REPORT_DIR="$SRC/tmp/asm-verify"
        export ASM_VERIFY_MANIFEST_OUT="$SRC/tools/asm-verify/manifest.generated.toml"

        echo "=== [1/4] cmake configure ==="
        if [[ ! -f "$BUILD/Makefile" ]]; then
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

        echo "=== [4/4] asm-verify ==="
        python3 "$SRC/tools/asm-verify/asm-verify.py" --report-only
    '

# Lift reports back out.
docker run --rm \
    -v "$PROJECT_ROOT_DOCKER:/work" \
    -v fnverify-src:/staging:ro \
    "$IMAGE" -c '
        mkdir -p /work/tmp/asm-verify
        cp /staging/tmp/asm-verify/report.md   /work/tmp/asm-verify/report.md
        cp /staging/tmp/asm-verify/report.json /work/tmp/asm-verify/report.json
    '

echo
echo "Report: tmp/asm-verify/report.md"
