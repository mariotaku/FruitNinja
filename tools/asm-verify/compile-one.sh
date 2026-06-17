#!/usr/bin/env bash
# Compile one port .cpp with the Bada-faithful Sourcery 2010q1 toolchain and
# extract a single function's disassembly for side-by-side diffing against
# Ghidra's disassembly of the binary.
#
#   bash tools/asm-verify/compile-one.sh src/entities/SlashEntity.cpp \
#       _ZN11SlashEntity5ResetEv slash_reset
#
# Output: tmp/asm-compare/<tag>_port.s
#
# Pre-requisite: bash tools/asm-verify/setup.sh (one-time fnverify image build)
#                + native build/_deps/tinyxml2-src/tinyxml2.h (cmake -B build).

set -euo pipefail

if [[ $# -lt 3 ]]; then
    cat <<'USAGE' >&2
Usage: bash tools/asm-verify/compile-one.sh <src/file.cpp> <mangled-func> <tag>

Examples:
  bash tools/asm-verify/compile-one.sh src/entities/SlashEntity.cpp _ZN11SlashEntity5ResetEv slash_reset
  bash tools/asm-verify/compile-one.sh src/game/WaveManager.cpp _ZN11WaveManager10UpdateWaveEf wave_update
USAGE
    exit 2
fi

CPP="$1"
FUNC="$2"
TAG="$3"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
IMAGE="${ASM_VERIFY_IMAGE:-fnverify}"

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
    echo "Image '$IMAGE' missing. Run tools/asm-verify/setup.sh first." >&2
    exit 1
fi
if [[ ! -f "$PROJECT_ROOT/build/_deps/tinyxml2-src/tinyxml2.h" ]]; then
    echo "ERROR: build/_deps/tinyxml2-src/tinyxml2.h missing." >&2
    echo "       Run cmake -B build at least once." >&2
    exit 1
fi

export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL='*'

OUTDIR="$PROJECT_ROOT/tmp/asm-compare"
mkdir -p "$OUTDIR"

echo "Compiling $REL -> $TAG (function: $FUNC)"

docker run --rm \
    -v "$PROJECT_ROOT_DOCKER:/work" \
    -e REL="$REL" \
    -e FUNC="$FUNC" \
    -e TAG="$TAG" \
    "$IMAGE" -c '
set -e
mkdir -p /tmp/portsrc/src /tmp/portsrc/cross-headers /tmp/portsrc/tinyxml2
rsync -aq /work/src/ /tmp/portsrc/src/
rsync -aq /work/tools/asm-verify/cross-headers/ /tmp/portsrc/cross-headers/
cp /work/build/_deps/tinyxml2-src/tinyxml2.h /tmp/portsrc/tinyxml2/

# C++11 -> C++03 sed transforms.
find /tmp/portsrc/src -name "*.h" -o -name "*.cpp" | xargs sed -i \
    -e "s/explicit operator bool/operator bool/g" \
    -e "s|using \([A-Za-z_][A-Za-z_0-9]*\) = \(.*\);|typedef \2 \1;|g"

CXXFLAGS="-mthumb -mcpu=cortex-a8 -mfloat-abi=hard -mfpu=vfpv3 -fshort-enums -fshort-wchar"
CXXFLAGS="$CXXFLAGS -std=gnu++0x -O2 -fno-exceptions -fno-rtti"
CXXFLAGS="$CXXFLAGS -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables"
CXXFLAGS="$CXXFLAGS -fpermissive -include /tmp/portsrc/cross-headers/fn-cxx11-shims.h -D__bada__"
INCS="-I/tmp/portsrc/src -I/tmp/portsrc/src/engine -I/tmp/portsrc/src/game -I/tmp/portsrc/src/screens -I/tmp/portsrc/src/hud -I/tmp/portsrc/src/entities -I/tmp/portsrc/src/platform -I/tmp/portsrc/src/debug -I/tmp/portsrc/cross-headers -I/tmp/portsrc/tinyxml2"

arm-none-eabi-g++ $CXXFLAGS $INCS -c "/tmp/portsrc/$REL" -o /tmp/t.o

arm-none-eabi-objdump -d /tmp/t.o \
    | sed -n "/<$FUNC>:/,/^\$/p" \
    > "/work/tmp/asm-compare/${TAG}_port.s"

echo "Wrote tmp/asm-compare/${TAG}_port.s"
'
