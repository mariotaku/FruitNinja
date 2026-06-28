#!/usr/bin/env bash
# In-container helper for signature-mismatch.py: cross-builds the port objects
# (keep-going) and dumps `nm` for EVERY compiled .o so the host-side analysis
# can see ALL port symbols -- including ones the normal pipeline never pairs
# (manifest pairs on EXACT mangled name; signature-divergent overloads are
# invisible there). Writes /staging/tmp/asm-verify/port-nm.txt.
#
# Mirrors verify.sh's staging/env, but builds with `-k` (a single WIP TU break
# must not deny us nm for the other ~100 TUs) and skips the diff pipeline.
set -uo pipefail

PROJECT=/work
SRC=/staging
BUILD=/build

mkdir -p "$SRC"
rsync -aq --delete \
    --exclude=build --exclude='build-*/' --exclude=bada_SDK \
    --exclude=tmp --exclude=Testing --exclude=.git \
    --exclude=node_modules --exclude=_deps \
    "$PROJECT/" "$SRC/"

export FN_TOOLCHAIN_DIR=/opt/codesourcery
NM="${FN_TOOLCHAIN_DIR}/bin/arm-samsung-nucleuseabi-nm"

echo "=== configure ==="
if [[ ! -f "$BUILD/Makefile" ]]; then
    rm -rf "$BUILD"/* "$BUILD"/.* 2>/dev/null || true
    cmake -S "$SRC/tools/asm-verify/cross-build" -B "$BUILD" -G "Unix Makefiles" \
          -DCMAKE_TOOLCHAIN_FILE="$SRC/tools/asm-verify/toolchain.cmake" > /dev/null
fi

echo "=== build (keep-going) ==="
# -k: keep building other TUs even if one fails (WIP break in a single TU).
cmake --build "$BUILD" -j"$(nproc)" -- -k || true

echo "=== nm dump ==="
OUT="$SRC/tmp/asm-verify/port-nm.txt"
mkdir -p "$(dirname "$OUT")"
: > "$OUT"
n=0
while IFS= read -r f; do
    # Header line lets the host map each symbol back to its TU.
    echo "## OBJ ${f#"$BUILD"/}" >> "$OUT"
    "$NM" --print-size --defined-only "$f" >> "$OUT" 2>/dev/null || true
    n=$((n+1))
done < <(find "$BUILD" \( -name '*.o' -o -name '*.obj' \) | sort)
echo "  dumped nm for $n objects -> ${OUT#"$SRC"/}"
