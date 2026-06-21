#!/usr/bin/env bash
# Compile one port .cpp with the binary's actual compiler (Samsung Sourcery
# 4.4.1, arm-samsung-nucleuseabi, in the fnverify-bada image) and extract a
# single function's disassembly for side-by-side diffing against Ghidra's
# disassembly of the binary.
#
#   bash tools/asm-verify/compile-one.sh src/entities/SlashEntity.cpp \
#       _ZN11SlashEntity5ResetEv slash_reset
#
# Mode defaults to ARM (the binary is ~94% ARM). Pass --thumb as a 4th arg for
# the minority of functions the binary compiles as Thumb. Flags mirror
# toolchain.cmake exactly (incl. -fpic) so the codegen matches the cross-build.
#
# Output: tmp/asm-compare/<tag>_port.s
#
# Pre-requisite: the fnverify-bada image (run tools/asm-verify/setup.sh once).

set -euo pipefail

if [[ $# -lt 3 ]]; then
    cat <<'USAGE' >&2
Usage: bash tools/asm-verify/compile-one.sh <src/file.cpp> <mangled-func> <tag> [--arm|--thumb]

Examples:
  bash tools/asm-verify/compile-one.sh src/entities/SlashEntity.cpp _ZN11SlashEntity5ResetEv slash_reset
  bash tools/asm-verify/compile-one.sh src/game/WaveManager.cpp _ZN11WaveManager10UpdateWaveEf wave_update --thumb
USAGE
    exit 2
fi

CPP="$1"
FUNC="$2"
TAG="$3"
MODE="-marm"                                   # binary is ~94% ARM -> default ARM
case "${4:-}" in
    --thumb) MODE="-mthumb" ;;
    --arm|"") MODE="-marm" ;;
    *) echo "ERROR: 4th arg must be --arm or --thumb (got '$4')" >&2; exit 2 ;;
esac

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
IMAGE="${ASM_VERIFY_IMAGE:-fnverify-bada}"

# Resolve .cpp to project-relative.
if [[ "$CPP" = /* ]]; then
    REL="${CPP#$PROJECT_ROOT/}"
else
    REL="$CPP"
fi
if [[ ! -f "$PROJECT_ROOT/$REL" ]]; then
    echo "ERROR: $REL not found under $PROJECT_ROOT" >&2
    exit 1
fi

# MSYS2 /c/Users/... -> C:/Users/... for Docker bind mount.
to_docker_path() {
    local p="$1"
    if [[ "$p" =~ ^/([A-Za-z])/(.*)$ ]]; then
        printf '%s:/%s' "${BASH_REMATCH[1]^^}" "${BASH_REMATCH[2]}"
        return
    fi
    printf '%s' "$p"
}
PROJECT_ROOT_DOCKER="$(to_docker_path "$PROJECT_ROOT")"

if ! command -v docker > /dev/null; then
    echo "ERROR: docker not on PATH." >&2
    exit 1
fi
if ! docker image inspect "$IMAGE" > /dev/null 2>&1; then
    echo "ERROR: image '$IMAGE' missing (the asm-verify cross-build image)." >&2
    exit 1
fi
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL='*'

OUTDIR="$PROJECT_ROOT/tmp/asm-compare"
mkdir -p "$OUTDIR"

echo "Compiling $REL -> $TAG (function: $FUNC, mode: $MODE)"

docker run --rm \
    -v "$PROJECT_ROOT_DOCKER:/work" \
    -e REL="$REL" \
    -e FUNC="$FUNC" \
    -e TAG="$TAG" \
    -e MODE="$MODE" \
    "$IMAGE" bash -c '
set -e
export PATH="/opt/codesourcery/bin:$PATH"   # toolchain dir (matches toolchain.cmake _TC)
mkdir -p /tmp/portsrc/src /tmp/portsrc/cross-headers
rsync -aq /work/src/ /tmp/portsrc/src/
rsync -aq /work/tools/asm-verify/cross-headers/ /tmp/portsrc/cross-headers/

# C++11 -> C++03 sed transforms.
find /tmp/portsrc/src -name "*.h" -o -name "*.cpp" | xargs sed -i \
    -e "s/explicit operator bool/operator bool/g" \
    -e "s|using \([A-Za-z_][A-Za-z_0-9]*\) = \(.*\);|typedef \2 \1;|g"

# Flags mirror toolchain.cmake (incl. -fpic) so codegen matches the cross-build.
CXXFLAGS="$MODE -mcpu=cortex-a8 -mfloat-abi=hard -mfpu=vfpv3 -fshort-enums -fshort-wchar -fpic"
CXXFLAGS="$CXXFLAGS -std=gnu++0x -O2 -fno-exceptions -fno-rtti"
CXXFLAGS="$CXXFLAGS -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables"
CXXFLAGS="$CXXFLAGS -fpermissive -include /tmp/portsrc/cross-headers/fn-cxx11-shims.h -D__bada__"
INCS="-I/tmp/portsrc/src -I/tmp/portsrc/src/engine -I/tmp/portsrc/src/game -I/tmp/portsrc/src/screens -I/tmp/portsrc/src/hud -I/tmp/portsrc/src/entities -I/tmp/portsrc/src/platform -I/tmp/portsrc/src/debug -I/tmp/portsrc/cross-headers"

arm-samsung-nucleuseabi-g++ $CXXFLAGS $INCS -c "/tmp/portsrc/$REL" -o /tmp/t.o

arm-samsung-nucleuseabi-objdump -d /tmp/t.o \
    | sed -n "/<$FUNC>:/,/^\$/p" \
    > "/work/tmp/asm-compare/${TAG}_port.s"

echo "Wrote tmp/asm-compare/${TAG}_port.s"
'
