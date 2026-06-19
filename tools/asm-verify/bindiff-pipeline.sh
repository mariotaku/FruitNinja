#!/usr/bin/env bash
# Whole-program binary-vs-port BinDiff, mode-matched. One command replaces the
# hand-run steps: build ARM + Thumb twin .so's, BinExport each, BinDiff each
# against the binary, then merge per-function by the binary's actual ARM/Thumb
# mode (see mode-match-merge.py). Produces a ranked real-divergence list.
#
# Stages:
#   1. build twins   : fnverify.arm.so (-marm) + fnverify.thumb.so (-mthumb)
#   2. export twins   : .BinExport for each (+ the binary, if not cached)
#   3. diff           : BinDiff binary-vs-arm, binary-vs-thumb
#   4. merge          : mode-matched ranked divergence list (+ CSV)
#
# Usage:  tools/asm-verify/bindiff-pipeline.sh [--twins-only] [--skip-build]
#   --twins-only : stop after stage 2 (build + export the twins; the user's
#                  "twins and their binexport in one execution" minimum)
#   --skip-build : reuse existing tmp/fnverify.{arm,thumb}.so (stages 2-4 only)
#
# Env overrides (Windows + Docker Desktop defaults):
#   GHIDRA/BINDIFF/PY paths, BUILD_IMAGE=fnverify-bada, EXPORT_IMAGE=binexport-cli
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
TMP="$ROOT/tmp"
OUTDIR="$TMP/bindiff-out"
mkdir -p "$OUTDIR"

BINARY_EXE="$TMP/FruitNinja_v1_6_1.exe"
BINARY_BX="$TMP/binary.cli.BinExport"          # cached binary export (reused)
: "${BUILD_IMAGE:=fnverify-bada}"
: "${EXPORT_IMAGE:=binexport-cli:latest}"
: "${BINDIFF:=/c/Program Files/BinDiff/bin/bindiff.exe}"
: "${PY:=py}"
export MSYS_NO_PATHCONV=1
win() { cygpath -w "$1"; }                       # host path -> Windows path for docker

TWINS_ONLY=0; SKIP_BUILD=0
for a in "$@"; do
  case "$a" in
    --twins-only) TWINS_ONLY=1 ;;
    --skip-build) SKIP_BUILD=1 ;;
    -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
    *) echo "unknown arg: $a" >&2; exit 2 ;;
  esac
done

build_twin() {  # $1=mode flag  $2=output .so name
  local mode="$1" name="$2"
  echo ">> building twin: $name ($mode)"
  docker run --rm \
    -e HOST=/work -e "FN_ARM_MODE=$mode" -e "FN_SO_NAME=$name" \
    -v "$(win "$ROOT")":/work \
    -v fnverify-src:/staging \
    --tmpfs /build:exec,size=2G \
    "$BUILD_IMAGE" bash /work/tools/asm-verify/build-so.sh > "$TMP/build-$name.log" 2>&1
  grep -E "saved to" "$TMP/build-$name.log" || { echo "  BUILD FAILED -- see tmp/build-$name.log" >&2; tail -5 "$TMP/build-$name.log" >&2; exit 1; }
}

export_bx() {  # $1=input .so/.exe (in tmp)  $2=output .BinExport (in tmp)
  echo ">> exporting $(basename "$1") -> $(basename "$2")"
  docker run --rm -v "$(win "$TMP")":/work "$EXPORT_IMAGE" \
    "/work/$1" "/work/$2" 2>&1 | grep -E "Wrote|error:" | tail -1
}

diff_one() {  # $1=secondary .BinExport (in tmp)  $2=output basename
  echo ">> bindiff binary vs $(basename "$1")"
  rm -f "$OUTDIR/$2.BinDiff"
  "$BINDIFF" --primary="$(win "$BINARY_BX")" --secondary="$(win "$TMP/$1")" \
             --output_dir="$(win "$OUTDIR")" --output_format=bin \
    2>&1 | grep -iE "^matched|Similarity" | tail -2
  # bindiff names output <primary>_vs_<secondary>.BinDiff; normalize to $2
  local produced="$OUTDIR/$(basename "$BINARY_BX" .BinExport)_vs_$(basename "$1" .BinExport).BinDiff"
  [ -f "$produced" ] && mv -f "$produced" "$OUTDIR/$2.BinDiff"
}

echo "=== stage 1: build twins ==="
if [ "$SKIP_BUILD" -eq 0 ]; then
  build_twin "-marm"   "fnverify.arm.so"
  build_twin "-mthumb" "fnverify.thumb.so"
else
  echo "(--skip-build: reusing existing tmp/fnverify.{arm,thumb}.so)"
fi

echo "=== stage 2: export to BinExport ==="
export_bx "fnverify.arm.so"   "fnverify.arm.BinExport"
export_bx "fnverify.thumb.so" "fnverify.thumb.BinExport"
if [ ! -f "$BINARY_BX" ]; then
  echo "(binary BinExport missing -- exporting the .exe, this takes a few minutes)"
  export_bx "$(basename "$BINARY_EXE")" "$(basename "$BINARY_BX")"
fi

if [ "$TWINS_ONLY" -eq 1 ]; then
  echo "=== done (--twins-only): tmp/fnverify.{arm,thumb}.{so,BinExport} ready ==="
  exit 0
fi

echo "=== stage 3: bindiff each twin ==="
diff_one "fnverify.arm.BinExport"   "binary_vs_arm"
diff_one "fnverify.thumb.BinExport" "binary_vs_thumb"

echo "=== stage 4: mode-matched merge ==="
"$PY" "$(win "$SCRIPT_DIR/mode-match-merge.py")" \
  --binary "$(win "$BINARY_EXE")" \
  --arm-bindiff "$(win "$OUTDIR/binary_vs_arm.BinDiff")" \
  --thumb-bindiff "$(win "$OUTDIR/binary_vs_thumb.BinDiff")" \
  --out "$(win "$OUTDIR/mode-matched-divergences.csv")"

echo "=== done: ranked list -> tmp/bindiff-out/mode-matched-divergences.csv ==="
