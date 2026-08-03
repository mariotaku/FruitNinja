#!/usr/bin/env bash
# Single-TU cross-build diagnostic. Compiles ONE .cpp under the Bada-faithful
# Sourcery 2010q1 toolchain and prints errors only -- no nm, no symbol diff,
# no docker volume staging. Intended for fast root-causing of static_assert
# failures or cross-toolchain header mismatches surfaced by run.sh / asm-verify.
#
#   bash tools/asm-verify/check-tu.sh src/entities/Entity.cpp
#   bash tools/asm-verify/check-tu.sh src/engine/util/AsciiString.cpp
#
# Pre-requisite: bash tools/asm-verify/setup.sh (one-time fnverify image build).
#
# Use cases:
#   - asm-verify or symbol-diff fails on TU X; want a focused error trace.
#   - Just edited Foo.h; want to know if Foo.cpp still compiles cross-toolchain.
#   - Adding a new layout assert; want to confirm it fires/passes locally.
#
# Flags match toolchain.cmake exactly (Tag_ABI_VFP_args, fshort-enums,
# fshort-wchar) plus -D__bada__ so layout-asserts fire.

set -euo pipefail

if [[ $# -lt 1 ]]; then
    cat <<'USAGE' >&2
Usage: bash tools/asm-verify/check-tu.sh <path/to/file.cpp> [extra-cxx-flags...]

Examples:
  bash tools/asm-verify/check-tu.sh src/entities/Entity.cpp
  bash tools/asm-verify/check-tu.sh src/engine/util/AsciiString.cpp -E   # preprocess
  bash tools/asm-verify/check-tu.sh src/entities/Bomb.cpp -S             # asm output
USAGE
    exit 2
fi

CPP="$1"; shift
EXTRA_FLAGS="$*"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
# Image name MUST track run.sh/compile-one.sh's default (fnverify-bada). This said
# "fnverify" -- the name setup.sh does NOT build -- so the inspect below always
# missed and the script printed a notice and exited 0. Third instance of this same
# typo (see asm-verify-hook.sh, fixed in b49a2342), and the worst of the three:
# this is the LAYOUT GATE, so "cannot verify" was reporting as "verified".
IMAGE="${ASM_VERIFY_IMAGE:-fnverify-bada}"

# Resolve the .cpp path to a project-relative form.
if [[ "$CPP" = /* ]]; then
    REL="${CPP#$PROJECT_ROOT/}"
else
    REL="$CPP"
fi
if [[ ! -f "$PROJECT_ROOT/$REL" ]]; then
    echo "ERROR: $REL not found under $PROJECT_ROOT" >&2
    exit 1
fi

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

if ! command -v docker > /dev/null; then
    echo "ERROR: docker not on PATH." >&2
    exit 1
fi

if ! docker image inspect "$IMAGE" > /dev/null 2>&1; then
    echo "Image '$IMAGE' missing. Run tools/asm-verify/setup.sh first." >&2
    exit 1
fi

export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL='*'

echo "Compiling $REL with Bada-faithful flags..."

docker run --rm \
    -v "$PROJECT_ROOT_DOCKER:/work" \
    -e REL="$REL" \
    -e EXTRA="$EXTRA_FLAGS" \
    "$IMAGE" bash -c '
set -e
mkdir -p /tmp/portsrc/src /tmp/portsrc/cross-headers
rsync -aq /work/src/ /tmp/portsrc/src/
rsync -aq /work/tools/asm-verify/cross-headers/ /tmp/portsrc/cross-headers/

# C++11 -> C++03 sed transforms (same as full sym-diff/asm-verify).
find /tmp/portsrc/src -name "*.h" -o -name "*.cpp" | xargs sed -i \
    -e "s/explicit operator bool/operator bool/g" \
    -e "s|using \([A-Za-z_][A-Za-z_0-9]*\) = \(.*\);|typedef \2 \1;|g"

CXXFLAGS="-mthumb -mcpu=cortex-a8 -mfloat-abi=hard -mfpu=vfpv3 -fshort-enums -fshort-wchar"
CXXFLAGS="$CXXFLAGS -std=gnu++0x -O2 -fno-exceptions -fno-rtti"
CXXFLAGS="$CXXFLAGS -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables"
CXXFLAGS="$CXXFLAGS -fpermissive -include /tmp/portsrc/cross-headers/fn-cxx11-shims.h -D__bada__"
INCS="-I/tmp/portsrc/src -I/tmp/portsrc/src/engine -I/tmp/portsrc/src/game -I/tmp/portsrc/src/screens -I/tmp/portsrc/src/hud -I/tmp/portsrc/src/entities -I/tmp/portsrc/src/platform -I/tmp/portsrc/src/debug -I/tmp/portsrc/cross-headers"

OUT=/tmp/check.o
[ -n "$EXTRA" ] && OUT=/tmp/check.out  # for -E or -S the output is text

set +e
arm-samsung-nucleuseabi-g++ $CXXFLAGS $INCS $EXTRA -c "/tmp/portsrc/$REL" -o "$OUT" 2>&1
EXIT=$?
set -e

if [ "$EXIT" -eq 0 ]; then
    echo
    echo "OK -- $REL compiled cleanly."
else
    echo
    echo "FAIL -- $REL exited $EXIT"
    exit "$EXIT"
fi
'
