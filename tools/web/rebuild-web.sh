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
# Pinned (not :latest): a floating tag silently changed the default wasm STACK_SIZE
# (5MB -> 64KB at emscripten 3.1.27), which overflowed the HUD-text render path into
# static globals. Pin to a concrete version so the toolchain can't drift underneath us.
IMAGE="emscripten/emsdk:6.0.0"

[ -d "$BUILD_WEB" ] || exit 0      # web build not configured -> nothing to do

# --- worker: run the actual build (launched detached) ---
if [ "${1:-}" = "--worker" ]; then
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
