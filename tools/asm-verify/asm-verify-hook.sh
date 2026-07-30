#!/usr/bin/env bash
# Stop-hook companion to rebuild-web.sh: when a portable src file changed since
# the last asm-verify run, re-run the GCC-4.4.1-vs-binary cross-build diff and
# surface the divergence score. Detached/background (never blocks the turn);
# no-op if the fnverify image isn't built or nothing changed; lock-guarded.
# Output: tmp/asm-verify/hook-score.txt (headline verdict counts) + the full
# tmp/asm-verify/report.{md,json} refreshed by run.sh.
#
# One-time prereq: bash tools/asm-verify/setup.sh (builds the fnverify image).
set -u

PROJ="$(cd "$(dirname "$0")/../.." && pwd)"   # script is tools/asm-verify/ -> repo root is two up
# This said "/.." (one up), so PROJ was <repo>/tools: $PROJ/src never existed, the
# change-detect find matched nothing, and the dispatcher exit 0'd on every single
# invocation. Guard the assumption instead of trusting it -- a hook whose only
# failure mode is "silently does nothing" is indistinguishable from "up to date".
if [ ! -d "$PROJ/src" ]; then
    echo "[asm-verify] repo source dir missing: $PROJ/src (bad PROJ resolution)" >&2
    exit 1
fi
TMP="$PROJ/tmp/asm-verify"
REPORT="$TMP/report.json"
LOG="$TMP/hook.log"
SCORE="$TMP/hook-score.txt"
LOCK="$TMP/hook.lock"
CLS="$TMP/.changed-classes"

command -v docker >/dev/null 2>&1 || exit 0
# Image name MUST track run.sh's default (ASM_VERIFY_IMAGE:-fnverify-bada). This
# gate said "fnverify" while the image setup.sh builds -- and run.sh uses -- is
# "fnverify-bada", so the inspect always failed and this hook silently exit 0'd:
# it had never actually run. Honour the same override so the two cannot drift.
ASM_VERIFY_IMAGE="${ASM_VERIFY_IMAGE:-fnverify-bada}"
docker image inspect "$ASM_VERIFY_IMAGE" >/dev/null 2>&1 || exit 0   # setup.sh not run -> no-op

# --- worker: run the actual cross-build diff (launched detached) ---
if [ "${1:-}" = "--worker" ]; then
    {
        echo "[$(date -Is 2>/dev/null || date)] asm-verify start"
        bash "$PROJ/tools/asm-verify/run.sh"
        echo "[$(date -Is 2>/dev/null || date)] asm-verify done"
    } >"$LOG" 2>&1
    {
        echo "asm-verify score  $(date -Is 2>/dev/null || date)"
        echo "changed classes: $(tr '\n' ' ' < "$CLS" 2>/dev/null)"
        echo
        echo "== overall verdict counts =="
        grep -oE '"verdict"[[:space:]]*:[[:space:]]*"[^"]+"' "$REPORT" 2>/dev/null \
            | sed -E 's/.*"([^"]+)"$/\1/' | sort | uniq -c
        echo
        echo "(per-symbol detail incl. score/max_score: tmp/asm-verify/report.md)"
    } >"$SCORE" 2>&1
    rm -f "$LOCK"
    exit 0
fi

# --- dispatcher: gate on portable-src change, then launch the worker detached ---
mkdir -p "$TMP"
if [ -f "$REPORT" ]; then
    mapfile -t FILES < <(find "$PROJ/src" -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' -o -name '*.cc' \) -newer "$REPORT" 2>/dev/null)
else
    mapfile -t FILES < <(find "$PROJ/src" -type f -name '*.cpp' 2>/dev/null)
fi
[ "${#FILES[@]}" -gt 0 ] || exit 0          # up to date
[ -e "$LOCK" ] && { echo "[asm-verify] a run is already in progress; skipping"; exit 0; }
printf '%s\n' "${FILES[@]}" | sed -E 's#.*/##; s#\.(cpp|h|hpp|cc)$##' | sort -u > "$CLS"
: >"$LOCK"
nohup bash "$0" --worker >/dev/null 2>&1 &
echo "[asm-verify] $(grep -c . "$CLS") class(es) changed; cross-build diff started in background -> tmp/asm-verify/hook-score.txt"
exit 0
