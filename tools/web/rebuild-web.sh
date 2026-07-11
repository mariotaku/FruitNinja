#!/usr/bin/env bash
# Auto-rebuild the Emscripten web bundle (build/web/) when src/ changed since the
# last build. Invoked by the Claude Code Stop hook (.claude/settings.local.json),
# so the web build the LAN dev server serves stays current without manual rebuilds.
#
# Dispatcher (default): gate on src-change vs the existing wasm, then launch the
#   worker DETACHED (nohup) so the agent turn never blocks on the ~30-60s build.
# Worker (--worker): run the incremental Docker build via tools/web/build.sh --
#   the same in-container entrypoint CI (.github/workflows/pages.yml) uses, so
#   local and CI share one pipeline (pre-clean, race-safe two-step build,
#   verify + link retry).
#
# EXCLUSIVITY: the worker holds an atomic directory lock (mkdir) around the whole
#   build, so two builds can never write build/web/ concurrently (that race caused
#   mismatched js/wasm -> LinkError). The lock is acquired by the WORKER (not just
#   the dispatcher gate) so a direct `--worker` invocation is serialized too. It
#   records the holder PID; a lock left by a dead process (crash/kill) is detected
#   as stale and stolen, so a killed build can't wedge future builds forever. A
#   second worker waits up to LOCK_WAIT for an in-progress build, then gives up.
#
# No-op when build/web/ is absent, no src file is newer than the wasm, or a build
# is already running. Output/errors -> tmp/web-rebuild.log.
# POSIX/bash; works in MSYS2 (Windows) and on Linux.
set -u

PROJ="$(cd "$(dirname "$0")/../.." && pwd)"   # script is tools/web/ -> repo root is two up
BUILD_WEB="$PROJ/build/web"
# The build emits fruit-ninja.wasm, then web-hash-assets.py RENAMES it to
# fruit-ninja-<hash>.wasm, so the un-hashed name never persists. Gate the
# staleness check on the newest hashed wasm (fall back to the un-hashed name
# when nothing has been built yet -> forces a first build).
WASM="$(ls -t "$BUILD_WEB"/fruit-ninja-*.wasm 2>/dev/null | head -1)"
[ -n "$WASM" ] || WASM="$BUILD_WEB/fruit-ninja.wasm"
TMP="$PROJ/tmp"
LOG="$TMP/web-rebuild.log"
# Lock is a DIRECTORY (mkdir is atomic on every FS incl. MSYS2), not a plain file:
# a file lock needs a non-atomic test-then-create that two racers can both pass.
LOCKDIR="$TMP/web-rebuild.lock.d"
LOCK_WAIT=300        # worker: max seconds to wait for an in-progress build before giving up
# Emsdk image pin lives in tools/web/config.sh (single source of truth, shared
# with .github/workflows/pages.yml). See that file for the pinning rationale.
. "$(dirname "$0")/config.sh"
IMAGE="$EMSDK_IMAGE"

[ -d "$BUILD_WEB" ] || exit 0      # web build not configured -> nothing to do

# --- lock helpers (atomic dir lock + PID-based stale detection) ---
# lock_holder_alive: 0 (true) if LOCKDIR exists and its recorded PID is still running.
lock_holder_alive() {
    [ -d "$LOCKDIR" ] || return 1
    local opid
    opid="$(cat "$LOCKDIR/pid" 2>/dev/null)"
    # No PID recorded yet -> treat as alive (a racer is mid-acquire); a truly
    # orphaned empty lock is reaped by the age check in acquire_lock.
    [ -n "$opid" ] || return 0
    kill -0 "$opid" 2>/dev/null
}

# acquire_lock [timeout_seconds]: atomically take the lock. Steals a stale lock
# (dead holder). Waits up to timeout for a live holder, then returns 1.
acquire_lock() {
    local timeout="${1:-0}" waited=0
    while :; do
        if mkdir "$LOCKDIR" 2>/dev/null; then
            echo "$$" >"$LOCKDIR/pid"
            return 0
        fi
        # Couldn't create -> someone holds it. Steal if the holder is dead.
        if ! lock_holder_alive; then
            rm -rf "$LOCKDIR"
            continue
        fi
        [ "$timeout" -gt 0 ] && [ "$waited" -lt "$timeout" ] || return 1
        sleep 1
        waited=$((waited + 1))
    done
}
release_lock() { rm -rf "$LOCKDIR"; }

# --- worker: run the actual build (launched detached) ---
if [ "${1:-}" = "--worker" ]; then
    # Serialize against any other build touching build/web/. Wait up to LOCK_WAIT
    # for an in-progress build; if it's still going, skip rather than race it.
    if ! acquire_lock "$LOCK_WAIT"; then
        echo "[$(date -Is 2>/dev/null || date)] rebuild SKIPPED (another build holds the lock)" >>"$LOG"
        exit 0
    fi
    # Release the lock on any exit path (normal, error, or kill) so a dead build
    # never wedges future ones.
    trap 'release_lock' EXIT INT TERM
    # Windows/MSYS: keep /src and -w literal; hand Docker a native host path.
    export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*'
    if command -v cygpath >/dev/null 2>&1; then HOST="$(cygpath -m "$PROJ")"; else HOST="$PROJ"; fi
    {
        echo "[$(date -Is 2>/dev/null || date)] rebuild start ($HOST -> /src)"
        # Single in-container pipeline shared with CI: build.sh owns the
        # pre-clean of link outputs, the lib-first race workaround, and the
        # verify + link-retry. No flags = respect the existing build/web
        # configure (preserves a locally-configured FN_WEB_DEBUG build).
        docker run --rm -v "${HOST}:/src" -w /src "$IMAGE" \
            bash /src/tools/web/build.sh
        code=$?
        if [ "$code" -eq 0 ]; then echo "[$(date -Is 2>/dev/null || date)] rebuild OK"
        else echo "[$(date -Is 2>/dev/null || date)] rebuild FAILED (exit $code)"; fi
    } >"$LOG" 2>&1
    exit 0
fi

# --- dispatcher: gate on src-change, then launch the worker detached ---
mkdir -p "$TMP"
if [ -f "$WASM" ]; then
    CHANGED="$(find "$PROJ/src" -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.c' -o -name '*.cc' -o -name '*.hpp' \) -newer "$WASM" -print -quit 2>/dev/null)"
else
    CHANGED="$(find "$PROJ/src" -type f -name '*.cpp' -print -quit 2>/dev/null)"
fi
[ -n "$CHANGED" ] || exit 0         # build is up to date

# Fast-path skip: don't launch a redundant worker while a live build runs. This is
# advisory only -- the authoritative serialization is the worker's acquire_lock,
# which safely handles the small window where two dispatchers both launch.
if lock_holder_alive; then echo "[rebuild-web] a rebuild is already running; skipping"; exit 0; fi
nohup bash "$0" --worker >/dev/null 2>&1 &
echo "[rebuild-web] src changed ($(basename "$CHANGED")); web rebuild started in background -> tmp/web-rebuild.log (hard-refresh the phone in ~1 min)"
exit 0
