#!/usr/bin/env bash
# Symbol-diff pipeline: cross-compile every portable src/**.cpp under the
# Sourcery 2010q1 toolchain (matches Bada mangling + ABI), nm the resulting
# .o files, and diff text symbols against FruitNinja.exe's nm output.
#
# Outputs (tmp/symbol-diff/):
#   binary_symbols_{mangled,demangled}.txt
#   port_full_{mangled,demangled}.txt
#   missing_full_{mangled,demangled}.txt
#   compile_failures.txt
#   missing_organized.md            (the headline coverage report)
#
#   bash tools/symbol-diff/run.sh
#
# Pre-requisite: bash tools/asm-verify/setup.sh (one-time fnverify image build)
#                + native build/host/_deps/tinyxml2-src/tinyxml2.h (i.e. cmake -B build
#                has been configured at least once).
#
# Notes:
#   - Filters platform-glue out: *SDL.cpp / *Posix.cpp / *Win32.cpp suffixes,
#     src/platform/sdl/* directory, and src/main.cpp by name. See
#     CLAUDE.md "Platform-specific files" for the exclusion convention.
#   - Defines __bada__ so port-side `#ifdef __bada__` static_asserts on
#     binary-faithful struct layouts fire under cross-build too.
#   - C++11 -> C++03 sed transforms applied: `explicit operator bool` -> drop
#     `explicit`; `using Foo = Bar;` template-aliases -> `typedef`. Both are
#     unparseable by GCC 4.4.1 even with -std=gnu++0x.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
IMAGE="${ASM_VERIFY_IMAGE:-fnverify}"

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

if [[ ! -f "$PROJECT_ROOT/build/host/_deps/tinyxml2-src/tinyxml2.h" ]]; then
    echo "ERROR: build/host/_deps/tinyxml2-src/tinyxml2.h missing." >&2
    echo "       Run cmake -B build at least once before symbol-diff." >&2
    exit 1
fi

export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL='*'

mkdir -p "$PROJECT_ROOT/tmp/symbol-diff"

# Step 1+2: extract binary symbols, cross-compile every portable .cpp,
# nm the resulting object files, diff against binary symbols.
docker run --rm \
    -v "$PROJECT_ROOT_DOCKER:/work" \
    "$IMAGE" -c '
set -e
arm-none-eabi-nm /work/FruitNinjaBada/Bin/FruitNinja.exe \
  | awk "\$2 ~ /^[Tt]\$/ {print \$3}" \
  | sort -u > /work/tmp/symbol-diff/binary_symbols_mangled.txt

arm-none-eabi-nm --demangle /work/FruitNinjaBada/Bin/FruitNinja.exe \
  | awk "\$2 ~ /^[Tt]\$/ { \$1=\"\"; \$2=\"\"; sub(/^  */,\"\"); print }" \
  | sort -u > /work/tmp/symbol-diff/binary_symbols_demangled.txt

mkdir -p /tmp/portsrc/src /tmp/portsrc/cross-headers /tmp/portsrc/tinyxml2
rsync -aq /work/src/ /tmp/portsrc/src/
rsync -aq /work/tools/asm-verify/cross-headers/ /tmp/portsrc/cross-headers/
cp /work/build/host/_deps/tinyxml2-src/tinyxml2.h /tmp/portsrc/tinyxml2/

# C++11 -> C++03 sed transforms (GCC 4.4.1 cannot parse these).
find /tmp/portsrc/src -name "*.h" -o -name "*.cpp" | xargs sed -i \
    -e "s/explicit operator bool/operator bool/g" \
    -e "s|using \([A-Za-z_][A-Za-z_0-9]*\) = \(.*\);|typedef \2 \1;|g"

mkdir -p /tmp/portsyms
CXXFLAGS="-mthumb -mcpu=cortex-a8 -mfloat-abi=hard -mfpu=vfpv3 -fshort-enums -fshort-wchar"
CXXFLAGS="$CXXFLAGS -std=gnu++0x -O2 -fno-exceptions -fno-rtti"
CXXFLAGS="$CXXFLAGS -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables"
CXXFLAGS="$CXXFLAGS -fpermissive -include /tmp/portsrc/cross-headers/fn-cxx11-shims.h -D__bada__ -DFN_ASM_VERIFY_CROSS"
INCS="-I/tmp/portsrc/src -I/tmp/portsrc/src/engine -I/tmp/portsrc/src/game -I/tmp/portsrc/src/screens -I/tmp/portsrc/src/hud -I/tmp/portsrc/src/entities -I/tmp/portsrc/src/platform -I/tmp/portsrc/src/debug -I/tmp/portsrc/cross-headers -I/tmp/portsrc/tinyxml2"

ok=0; fail=0
> /tmp/compile_failures.txt
cd /tmp/portsrc

# Platform-glue exclusion (mirrors CLAUDE.md three-layered rule):
#   1. *SDL.cpp / *Posix.cpp / *Win32.cpp suffixes
#   2. src/platform/sdl/* directory
#   3. Explicit-name list (src/main.cpp -- SDL entry point, never has portable symbols)
for cpp in $(find src -name "*.cpp" \
                 ! -name "*SDL.cpp" ! -name "*Posix.cpp" ! -name "*Win32.cpp" \
                 ! -path "src/platform/sdl/*" ! -path "src/main.cpp"); do
    rel=${cpp#src/}
    obj=/tmp/portsyms/$(echo "$rel" | tr "/" "_").o
    if arm-none-eabi-g++ $CXXFLAGS $INCS -c "$cpp" -o "$obj" 2>/dev/null; then
        ok=$((ok+1))
    else
        fail=$((fail+1))
        echo "$cpp" >> /tmp/compile_failures.txt
    fi
done
echo "Cross-compile: OK=$ok, FAILED=$fail"

ls /tmp/portsyms/*.o | xargs arm-none-eabi-nm 2>/dev/null \
  | awk "\$2 ~ /^[Tt]\$/ {print \$3}" | grep -v "^\." | sort -u \
  > /work/tmp/symbol-diff/port_full_mangled.txt
arm-none-eabi-c++filt < /work/tmp/symbol-diff/port_full_mangled.txt | sort -u \
  > /work/tmp/symbol-diff/port_full_demangled.txt

comm -23 /work/tmp/symbol-diff/binary_symbols_mangled.txt /work/tmp/symbol-diff/port_full_mangled.txt \
  > /work/tmp/symbol-diff/missing_full_mangled.txt
arm-none-eabi-c++filt < /work/tmp/symbol-diff/missing_full_mangled.txt | sort -u \
  > /work/tmp/symbol-diff/missing_full_demangled.txt
cp /tmp/compile_failures.txt /work/tmp/symbol-diff/compile_failures.txt
'

# Step 3: classify + write missing_organized.md (host-side python).
PY="${PYTHON:-python}"
if ! command -v "$PY" > /dev/null; then
    echo "WARNING: python not on PATH; missing_organized.md not generated."
    echo "         Raw symbol files are in tmp/symbol-diff/."
    exit 0
fi

CLASSIFY_PY="$SCRIPT_DIR/classify.py"
# Windows pythons hate MSYS-style /c/Users paths; convert if available.
if command -v cygpath > /dev/null; then
    CLASSIFY_PY="$(cygpath -w "$CLASSIFY_PY")"
fi
"$PY" "$CLASSIFY_PY"

echo
echo "Report: tmp/symbol-diff/missing_organized.md"
