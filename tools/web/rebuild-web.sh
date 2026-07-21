#!/usr/bin/env bash
# Auto-rebuild the Emscripten web bundle (build/web/) when src/ changed since the
# last build. Invoked by the Claude Code Stop hook (.claude/settings.local.json),
# so the web build the LAN dev server serves stays current without manual rebuilds.
#
# Dispatcher (default): gate on src-change vs the existing wasm, then launch the
#   worker DETACHED (nohup) so the agent turn never blocks on the ~30-60s build.
# Worker (--worker): run the incremental NATIVE build via tools/web/build.sh --
#   the same entrypoint CI (.github/workflows/pages.yml) uses (also native, via
#   mymindstorm/setup-emsdk), so local and CI share one pipeline (pre-clean,
#   race-safe two-step build, verify + link retry).
#
# NATIVE TOOLCHAIN (no Docker): the worker puts emcc/ninja/ffmpeg on PATH before
#   calling build.sh.
#   - emcc: if not already on PATH, sourced from $FN_EMSDK/emsdk_env.sh
#     (default /c/tools/emsdk; override FN_EMSDK to point at another emsdk 6.0.0
#     checkout -- see tools/web/config.sh for the pinned version).
#   - ninja: must be on PATH, or set FN_NINJA_DIR to a directory containing it
#     (e.g. a CLion-bundled ninja).
#   - ffmpeg: must be on PATH, or set FN_FFMPEG_DIR to a directory containing it.
#   - Do NOT export MSYS_NO_PATHCONV for this build -- that was only needed for
#     the old Docker path's -v/-w mount argument; it breaks CMake's internal
#     try_compile (libwebp config fails) under the native MSYS2 toolchain.
#   - stage-assets.py / svg-to-webp.py self-provision their own deps (Pillow,
#     fonttools, resvg-py) -- no action needed here.
#
# --profiling: forward to build.sh -> -DFN_WEB_PROFILING=ON (keeps C++ function
#   names in the wasm for a Chrome/Firefox DevTools flame graph; forces a
#   reconfigure; sticky until the next plain/--release/--debug rebuild). Only
#   meaningful with --worker (manual invocation) -- the Stop-hook auto-dispatch
#   never passes it, so unattended rebuilds stay lean by default.
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
# Emsdk version pin lives in tools/web/config.sh (single source of truth, shared
# with .github/workflows/pages.yml). See that file for the pinning rationale.
. "$(dirname "$0")/config.sh"

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
    # --profiling (second CLI arg to --worker): forwarded to build.sh verbatim.
    BUILD_SH_ARGS=()
    [ "${2:-}" = "--profiling" ] && BUILD_SH_ARGS+=(--profiling)
    {
        echo "[$(date -Is 2>/dev/null || date)] rebuild start (native)${BUILD_SH_ARGS:+ (${BUILD_SH_ARGS[*]})}"

        # --- native toolchain setup (no Docker; do NOT set MSYS_NO_PATHCONV --
        #     it breaks CMake's internal try_compile / libwebp config) ---
        if ! command -v emcc >/dev/null 2>&1; then
            EMSDK_DIR="${FN_EMSDK:-/c/tools/emsdk}"
            if [ -f "$EMSDK_DIR/emsdk_env.sh" ]; then
                # shellcheck disable=SC1090
                . "$EMSDK_DIR/emsdk_env.sh"
            fi
        fi
        if ! command -v emcc >/dev/null 2>&1; then
            echo "ERROR: emcc not found on PATH and \$FN_EMSDK/emsdk_env.sh unavailable."
            echo "  Install emsdk $EMSDK_VERSION and either put it on PATH or set"
            echo "  FN_EMSDK=/path/to/emsdk (default: /c/tools/emsdk)."
            exit 1
        fi
        if ! command -v ninja >/dev/null 2>&1; then
            [ -n "${FN_NINJA_DIR:-}" ] && export PATH="$FN_NINJA_DIR:$PATH"
        fi
        if ! command -v ninja >/dev/null 2>&1; then
            echo "ERROR: ninja not found on PATH."
            echo "  Install ninja or set FN_NINJA_DIR to a directory containing it"
            echo "  (e.g. CLion's bundled ninja)."
            exit 1
        fi
        if ! command -v ffmpeg >/dev/null 2>&1; then
            [ -n "${FN_FFMPEG_DIR:-}" ] && export PATH="$FN_FFMPEG_DIR:$PATH"
        fi
        if ! command -v ffmpeg >/dev/null 2>&1; then
            echo "ERROR: ffmpeg not found on PATH."
            echo "  Install ffmpeg or set FN_FFMPEG_DIR to a directory containing it."
            exit 1
        fi

        # Single pipeline shared with CI: build.sh owns the pre-clean of link
        # outputs, the lib-first race workaround, and the verify + link-retry.
        # No flags = respect the existing build/web configure (preserves a
        # locally-configured FN_WEB_DEBUG build).
        bash "$PROJ/tools/web/build.sh" "${BUILD_SH_ARGS[@]}"
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
