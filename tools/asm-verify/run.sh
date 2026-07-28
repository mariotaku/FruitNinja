#!/usr/bin/env bash
# Host-side launcher: bind-mounts the project into the asm-verify container
# and invokes the in-container verify driver.
#
#   bash tools/asm-verify/run.sh
#
# Pre-requisite: bash tools/asm-verify/setup.sh (one-time image build).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
IMAGE="${ASM_VERIFY_IMAGE:-fnverify-bada}"

# Normalise project root to a docker-friendly path. On Git Bash / MSYS the
# CWD looks like /c/Users/..., but docker (Rancher Desktop / Docker Desktop
# on Windows) wants C:/Users/... or //c/Users/... for bind-mounts.
source "$SCRIPT_DIR/../lib/docker-paths.sh"
PROJECT_ROOT_DOCKER="$(to_docker_path "$PROJECT_ROOT")"

if ! command -v docker > /dev/null; then
    echo "ERROR: docker not on PATH." >&2
    exit 1
fi

if ! docker image inspect "$IMAGE" > /dev/null 2>&1; then
    echo "Image '$IMAGE' missing. Run tools/asm-verify/setup.sh first." >&2
    exit 1
fi

# Disable MSYS / Git Bash path translation so /work/... isn't rewritten
# to C:/Program Files/Git/work/... when handed to docker.
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL='*'

# Optional filter: pass --filter <glob> or --class <Foo> or --symbol <name>
# to limit verification to a subset (lets you iterate on one class without
# re-running ~886 symbols).
FILTER=""
RESOLVE=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --filter)  FILTER="$2"; shift 2 ;;
        --resolve-operands) RESOLVE=1; shift ;;
        --class)   FILTER="*${2}*"; shift 2 ;;          # loose substring on class
        --symbol)  FILTER="*${2}*"; shift 2 ;;          # loose substring on function
        --help|-h)
            cat <<USAGE
Usage: bash tools/asm-verify/run.sh [options]

Options:
  --filter <glob>    Run only symbols whose mangled name matches the glob.
                     Example: --filter '_ZN11WaveManager*'
  --class <Foo>      Shortcut for --filter '*Foo*' (loose substring match).
                     Example: --class WaveManager
  --symbol <name>    Same as --class but for one specific function name.
                     Example: --symbol GetNextWave
  --resolve-operands Recover CALL-target and literal-pool DATA-symbol identity
                     instead of masking them to CALL / <SYM>. Writes
                     report.resolved.* so the baseline report.json -- and the
                     sticky triage verdicts keyed on the un-annotated asm_hash
                     -- stay valid. See operand_resolve.py for the rule.

Without a filter, runs all symbols in tmp/asm-verify/manifest.generated.toml
(currently ~886 symbols across 110 portable TUs).
USAGE
            exit 0
            ;;
        *) echo "Unknown arg: $1 (try --help)" >&2; exit 2 ;;
    esac
done

# Bind-mount project (read-only) + tmpfs for build/cache.
# /staging uses a named volume (ext4) solely for the report extraction
# step — the source rsync inside verify.sh is fresh each run. /build and
# /cache are tmpfs, guaranteeing no stale .o files between runs.
docker run --rm \
    -e "ASM_VERIFY_FILTER=$FILTER" \
    -e "ASM_VERIFY_RESOLVE_OPERANDS=$RESOLVE" \
    -v "$PROJECT_ROOT_DOCKER:/work:ro" \
    -v fnverify-src:/staging \
    --tmpfs /build:exec,size=2G \
    --tmpfs /cache:exec,size=256M \
    "$IMAGE" bash /work/tools/asm-verify/verify.sh

# A FILTERED run covers only a handful of symbols, so it must never overwrite
# the last full sweep: report.json/report.md always mean "whole-program sweep,
# baseline normalizer". Without this, a stray `--class Foo` run leaves a
# 16-symbol residue sitting at report.json that reads as the current state of
# the entire port -- which is exactly how the pipeline looked healthy for three
# weeks while it was in fact failing to compile.
#
# --resolve-operands is segregated the same way and for the same reason: it
# changes the normalized instruction stream, hence every asm_hash, hence the
# validity of every sticky triage verdict.
#
# The destination name is decided BEFORE the extraction and the copy targets it
# directly -- a cp-to-report.json-then-mv would destroy the baseline sweep on
# its way past.
REPORT_BASE="report"
if [ -n "$RESOLVE" ]; then
    REPORT_BASE="report.resolved"
fi
if [ -n "$FILTER" ]; then
    REPORT_BASE="report.scoped"
fi

# verify.sh writes the report into /staging/tmp; lift it back out via a
# scratch container that mounts the named volume read-only.
docker run --rm \
    -e "REPORT_BASE=$REPORT_BASE" \
    -v "$PROJECT_ROOT_DOCKER:/work" \
    -v fnverify-src:/staging:ro \
    "$IMAGE" bash -c '
        mkdir -p /work/tmp/asm-verify
        cp /staging/tmp/asm-verify/report.md   "/work/tmp/asm-verify/${REPORT_BASE}.md"
        cp /staging/tmp/asm-verify/report.json "/work/tmp/asm-verify/${REPORT_BASE}.json"
        # Full binary+port symbol sets and the c++filt demangle map, written by
        # discover-symbols.py. Independent of --filter (discovery always runs
        # whole-program), so it keeps one fixed name. signature-mismatch.py
        # reads it host-side, where the cross toolchain does not exist.
        cp /staging/tmp/asm-verify/symbol-index.json \
           /work/tmp/asm-verify/symbol-index.json 2>/dev/null || true
    '

if [ "$REPORT_BASE" != "report" ]; then
    echo "-> tmp/asm-verify/${REPORT_BASE}.json (baseline full-sweep report.json left untouched)."
fi

# Host-side: classify divergences and ENRICH report.json in place with a
# per-symbol cause + real-bug-likelihood (HIGH/MED/LOW), plus write
# tmp/asm-verify/suggested-triage.json. The ranked shortlist is printed to
# stdout below (no markdown product). Non-fatal — report.json is the source of
# truth. Run from PROJECT_ROOT with a RELATIVE script path in a subshell: native
# Windows Python mangles an MSYS-absolute "/c/..." arg, but resolves a relative
# path against the (correct) OS cwd. Cross-platform (no path translation).
(
    cd "$PROJECT_ROOT" || exit 0
    if command -v python > /dev/null 2>&1; then
        python tools/asm-verify/classify-divergences.py "tmp/asm-verify/${REPORT_BASE}.json"
    elif command -v py > /dev/null 2>&1; then
        py tools/asm-verify/classify-divergences.py "tmp/asm-verify/${REPORT_BASE}.json"
    fi
) || true

# Host-side: stale-marker lint (#204 regression guard). Checks every src/ @0x
# marker against the binary symbol table (report.json). Non-fatal -- surfaces
# STALE / MID-SYMBOL-MISMATCH (wrong-address mis-stamps) the same run they're
# introduced, not sessions later.
(
    cd "$PROJECT_ROOT" || exit 0
    PY=""
    command -v python > /dev/null 2>&1 && PY=python
    [ -z "$PY" ] && command -v py > /dev/null 2>&1 && PY=py
    if [ -n "$PY" ]; then
        echo
        echo "=== stale-marker lint (#204 guard) ==="
        "$PY" tools/asm-verify/stale-marker-lint.py 2>&1 \
            | grep -iE "Total markers|MID-SYMBOL-MISMATCH|^ +STALE |NO-VERSION" | head -8
    fi
) || true

# Host-side: gutted-__bada__-body detector (#107 regression guard). The
# cross-build's -D__bada__ can strip a STORE while the LOAD stays, so a function
# gets diffed against a value frozen at its constructor -- and the resulting
# score is meaningless in EITHER direction, so it cannot be spotted from the
# ranking. Non-fatal; full findings land in tmp/gutted-bada/findings.json.
(
    cd "$PROJECT_ROOT" || exit 0
    PY=""
    command -v python > /dev/null 2>&1 && PY=python
    [ -z "$PY" ] && command -v py > /dev/null 2>&1 && PY=py
    if [ -n "$PY" ]; then
        echo
        echo "=== gutted __bada__ bodies (#107 guard) ==="
        "$PY" tools/asm-verify/detect-gutted-bada.py \
            --report-json "tmp/asm-verify/${REPORT_BASE}.json" --min-rank HIGH --top 8 2>&1 \
            | grep -vE '^\s*$' | head -32
    fi
) || true

# Host-side: signature-mismatch (PAIRING-GAP guard). run.sh pairs binary<->port
# on the EXACT mangled name, so a function whose fully-qualified name matches on
# both sides but whose SIGNATURE differs is never paired and never diffed -- it
# cannot fail, so it silently reads as "no problem". This surfaces those, ranked
# by live undiffed bytes. Findings become real diffs via a `port_mangled` alias
# (signature-mismatch.py --write-back). Non-fatal.
(
    cd "$PROJECT_ROOT" || exit 0
    PY=""
    command -v python > /dev/null 2>&1 && PY=python
    [ -z "$PY" ] && command -v py > /dev/null 2>&1 && PY=py
    if [ -n "$PY" ] && [ -f tmp/asm-verify/symbol-index.json ]; then
        echo
        echo "=== signature-mismatch (pairing-gap guard) ==="
        "$PY" tools/asm-verify/signature-mismatch.py --top 10 2>&1 | head -24
    fi
) || true

echo
echo "Report:    tmp/asm-verify/${REPORT_BASE}.md"
echo "${REPORT_BASE}.json enriched with per-symbol cause + likelihood (ranked shortlist printed above)."
