#!/usr/bin/env bash
# Auto-rebuild the Emscripten web bundle (build/web/) when src/ changed since the
# last build. Invoked by the Claude Code Stop hook (.claude/settings.local.json),
# so the web build the LAN dev server serves stays current without manual rebuilds.
#
# Dispatcher (default): gate on src-change vs the existing wasm, then launch the
#   worker DETACHED (nohup) so the agent turn never blocks on the ~30-60s build.
# Worker (--worker): run the actual incremental Docker build (same image as CI).
#
# No-op when build/web/ is absent, no src file is newer than the wasm, or a build
# is already running (lockfile). Output/errors -> tmp/web-rebuild.log.
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
LOCK="$TMP/web-rebuild.lock"
IMAGE="emscripten/emsdk:latest"

[ -d "$BUILD_WEB" ] || exit 0      # web build not configured -> nothing to do

# --- worker: run the actual build (launched detached) ---
if [ "${1:-}" = "--worker" ]; then
    # Windows/MSYS: keep /src and -w literal; hand Docker a native host path.
    export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*'
    if command -v cygpath >/dev/null 2>&1; then HOST="$(cygpath -m "$PROJ")"; else HOST="$PROJ"; fi
    {
        echo "[$(date -Is 2>/dev/null || date)] rebuild start ($HOST -> /src)"
        docker run --rm -v "${HOST}:/src" -w /src "$IMAGE" cmake --build build/web -j
        code=$?
        if [ "$code" -eq 0 ]; then echo "[$(date -Is 2>/dev/null || date)] rebuild OK"
        else echo "[$(date -Is 2>/dev/null || date)] rebuild FAILED (exit $code)"; fi
    } >"$LOG" 2>&1
    rm -f "$LOCK"
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

if [ -e "$LOCK" ]; then echo "[rebuild-web] a rebuild is already running; skipping"; exit 0; fi
: >"$LOCK"
nohup bash "$0" --worker >/dev/null 2>&1 &
echo "[rebuild-web] src changed ($(basename "$CHANGED")); web rebuild started in background -> tmp/web-rebuild.log (hard-refresh the phone in ~1 min)"
exit 0
