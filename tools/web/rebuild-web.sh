#!/usr/bin/env bash
# Auto-rebuild the Emscripten web bundle (build/web/) when src/ changed since the
# last build. Invoked by the Claude Code Stop hook (.claude/settings.local.json),
# so the web build the LAN dev server serves stays current without manual rebuilds.
#
# Dispatcher (default): gate on src-change vs the existing wasm, preflight the
#   toolchain SYNCHRONOUSLY (so a broken environment is reported in the hook
#   output, not buried in a log), then launch the worker DETACHED (nohup) so the
#   agent turn never blocks on the ~30-60s build.
# Worker (--worker): run the incremental NATIVE build via tools/web/build.sh --
#   the same entrypoint CI (.github/workflows/pages.yml) uses (also native, via
#   mymindstorm/setup-emsdk), so local and CI share one pipeline (pre-clean,
#   race-safe two-step build, verify + link retry, artifact-hash verification).
#
# FAILURE VISIBILITY (this script used to fail silently -- see incidents below):
#   - strict mode + an ERR trap naming the failing line/command/status
#   - every preflight failure says exactly what is missing and how to fix it
#   - unknown flags are a hard error listing the accepted flags
#   - the DETACHED worker cannot swallow failures: on exit it writes
#       tmp/web-rebuild.status   (KV: status/exit/when/args/wasm before-after)
#       tmp/web-rebuild.failed   (only on failure; human summary + FATAL lines)
#       tmp/web-rebuild.warned   (only on success-with-warnings)
#     and the NEXT dispatcher run (i.e. the next Stop hook, which the user is
#     already watching) prints those to stderr and exits non-zero, so a detached
#     failure surfaces in the session instead of dying in tmp/web-rebuild.log.
#     Markers are consumed once (renamed to *.reported / deleted) so a single
#     failure is not re-reported forever.
#
# Incidents this hardening exists for:
#   1. no ninja on PATH -> worker bailed, dispatcher still exited 0 -> an
#      UNCHANGED wasm was mistaken for a shipped fix.
#   2. every flag except --profiling was silently ignored -> a requested
#      "release" build quietly was not one.
#
# NATIVE TOOLCHAIN (no Docker): the worker puts emcc/ninja/ffmpeg on PATH before
#   calling build.sh.
#   - emcc: if not already on PATH, sourced from $FN_EMSDK/emsdk_env.sh
#     (default /c/tools/emsdk; override FN_EMSDK to point at another emsdk 6.0.0
#     checkout -- see tools/web/config.sh for the pinned version).
#   - ninja: must be on PATH, or set FN_NINJA_DIR to a directory containing it
#     (e.g. a CLion-bundled ninja). If neither is available the build/web cache's
#     own CMAKE_MAKE_PROGRAM is accepted as a fallback (it is an absolute path),
#     with a warning; if that is gone too, it is a hard failure.
#   - ffmpeg: must be on PATH, or set FN_FFMPEG_DIR to a directory containing it.
#   - Do NOT export MSYS_NO_PATHCONV for this build -- that was only needed for
#     the old Docker path's -v/-w mount argument; it breaks CMake's internal
#     try_compile (libwebp config fails) under the native MSYS2 toolchain.
#   - stage-assets.py / svg-to-webp.py self-provision their own deps (Pillow,
#     fonttools, resvg-py) -- no action needed here.
#
# Flags (dispatcher and worker accept the same set; the dispatcher forwards them
# to the detached worker, which forwards the build ones to build.sh):
#   --worker       run the build here instead of dispatching (internal + manual)
#   --profiling    build.sh -DFN_WEB_PROFILING=ON (keeps C++ names in the wasm
#                  for a DevTools flame graph; forces a reconfigure; sticky until
#                  the next plain/--release/--debug rebuild)
#   --release      build.sh --release (hashed outputs, FN_WEB_DEBUG=OFF)
#   --debug        build.sh --debug   (FN_WEB_DEBUG=ON, unhashed outputs)
#   --reconfigure  build.sh --reconfigure
#   --force        skip the "src is newer than the wasm" gate
# The Stop-hook auto-dispatch passes none of these, so unattended rebuilds stay
# lean by default.
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

PROJ="$(cd "$(dirname "$0")/../.." && pwd)"   # script is tools/web/ -> repo root is two up
# Emsdk version pin + shared failure helpers (fn_web_fatal / fn_web_strict / ...)
# live in tools/web/config.sh (single source of truth, shared with
# .github/workflows/pages.yml). See that file for the pinning rationale.
. "$(dirname "$0")/config.sh"
fn_web_strict

BUILD_WEB="$PROJ/build/web"
# The build emits fruit-ninja.wasm, then web-hash-assets.py RENAMES it to
# fruit-ninja-<hash>.wasm, so the un-hashed name never persists. Gate the
# staleness check on the newest hashed wasm (fall back to the un-hashed name
# when nothing has been built yet -> forces a first build).
WASM="$(ls -t "$BUILD_WEB"/fruit-ninja-*.wasm 2>/dev/null | head -1 || true)"
[ -n "$WASM" ] || WASM="$BUILD_WEB/fruit-ninja.wasm"
TMP="$PROJ/tmp"
LOG="$TMP/web-rebuild.log"
STATUS="$TMP/web-rebuild.status"
FAILED_MARKER="$TMP/web-rebuild.failed"
WARNED_MARKER="$TMP/web-rebuild.warned"
# Lock is a DIRECTORY (mkdir is atomic on every FS incl. MSYS2), not a plain file:
# a file lock needs a non-atomic test-then-create that two racers can both pass.
LOCKDIR="$TMP/web-rebuild.lock.d"
LOCK_WAIT=300        # worker: max seconds to wait for an in-progress build before giving up

now() { date -Is 2>/dev/null || date; }

# --- argument parsing (incident 2: flags must never be silently dropped) -------
IS_WORKER=0
FORCE=0
BUILD_SH_ARGS=()
PASSTHRU=()          # what the dispatcher forwards to the detached worker
ACCEPTED_FLAGS="--worker --profiling --release --debug --reconfigure --force"
for arg in "$@"; do
    case "$arg" in
        --worker) IS_WORKER=1 ;;
        --force)  FORCE=1; PASSTHRU+=("$arg") ;;
        --profiling|--release|--debug|--reconfigure)
            BUILD_SH_ARGS+=("$arg"); PASSTHRU+=("$arg") ;;
        *)
            FN_WEB_FATAL_CODE=2 fn_web_fatal "rebuild-web.sh: unknown flag: $arg" \
                "Accepted flags: $ACCEPTED_FLAGS" \
                "Nothing was built. (Flags used to be ignored silently -- they are not any more.)" ;;
    esac
done

mkdir -p "$TMP"

# --- lock helpers (atomic dir lock + PID-based stale detection) ---
# lock_holder_alive: 0 (true) if LOCKDIR exists and its recorded PID is still running.
lock_holder_alive() {
    [ -d "$LOCKDIR" ] || return 1
    local opid
    opid="$(cat "$LOCKDIR/pid" 2>/dev/null || true)"
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

# --- toolchain preflight ------------------------------------------------------
# Shared by the dispatcher (synchronous -> visible in the hook output) and the
# worker (which may be invoked directly). Each failure names ONE missing thing.
# Sources emsdk_env.sh / extends PATH as a side effect, so the worker gets a
# ready-to-build environment from the same code the dispatcher validated with.
#
# FN_NINJA_DIR / FN_EMSDK / FN_FFMPEG_DIR are local-workstation concerns: CI
# installs emsdk + ninja + ffmpeg in the workflow, so these hints are suppressed
# there (fn_web_is_ci -> $CI / $GITHUB_ACTIONS). CI never runs this script
# anyway (it calls build.sh directly); the gate keeps the messages honest if it
# ever does.
preflight_toolchain() {
    if ! fn_web_have emcc; then
        local emsdk_dir="${FN_EMSDK:-/c/tools/emsdk}"
        if [ -f "$emsdk_dir/emsdk_env.sh" ]; then
            # shellcheck disable=SC1090
            . "$emsdk_dir/emsdk_env.sh" >/dev/null 2>&1 || true
        fi
    fi
    if ! fn_web_have emcc; then
        fn_web_fatal "emcc not found on PATH, and \$FN_EMSDK/emsdk_env.sh did not provide it" \
            "Looked for: ${FN_EMSDK:-/c/tools/emsdk}/emsdk_env.sh" \
            "Fix: install emsdk $EMSDK_VERSION, then either activate it on PATH or set" \
            "     FN_EMSDK=/path/to/emsdk (default /c/tools/emsdk)." \
            "Nothing was built -- the web bundle is STALE."
    fi
    if ! fn_web_have ninja && [ -n "${FN_NINJA_DIR:-}" ]; then
        export PATH="$FN_NINJA_DIR:$PATH"
    fi
    if ! fn_web_have ninja; then
        # Fall back to the ninja recorded in build/web/CMakeCache.txt: it is an
        # absolute path (e.g. CLion's bundled ninja.exe) and `cmake --build`
        # uses it directly, so the build genuinely works without ninja on PATH.
        # Only when THAT is gone too is the build impossible.
        local cached_ninja=""
        [ -f "$BUILD_WEB/CMakeCache.txt" ] && cached_ninja="$(sed -n 's/^CMAKE_MAKE_PROGRAM:[A-Z]*=//p' "$BUILD_WEB/CMakeCache.txt" | head -1 || true)"
        if [ -n "$cached_ninja" ] && [ -f "$cached_ninja" ]; then
            fn_web_warn "ninja is not on PATH; using the cached CMAKE_MAKE_PROGRAM instead" \
                "  $cached_ninja" \
                "Set FN_NINJA_DIR to that directory to make this explicit."
        elif [ -n "${FN_NINJA_DIR:-}" ]; then
            fn_web_fatal "FN_NINJA_DIR is set but contains no usable ninja: $FN_NINJA_DIR" \
                "Expected \$FN_NINJA_DIR/ninja (or ninja.exe) to be executable." \
                "Fix: point FN_NINJA_DIR at the directory holding ninja, e.g." \
                "     C:/Users/<you>/AppData/Local/Programs/CLion/bin/ninja/win/x64" \
                "Nothing was built -- the web bundle is STALE."
        else
            fn_web_fatal "ninja not found on PATH and FN_NINJA_DIR is not set" \
                "This is the failure that used to make rebuild-web.sh exit 0 while" \
                "building nothing, so an unchanged wasm looked like a shipped fix." \
                "Fix: install ninja, or set FN_NINJA_DIR to a directory containing it" \
                "     (e.g. CLion's bundled ninja: .../CLion/bin/ninja/win/x64)." \
                "Nothing was built -- the web bundle is STALE."
        fi
    fi
    if ! fn_web_have ffmpeg && [ -n "${FN_FFMPEG_DIR:-}" ]; then
        export PATH="$FN_FFMPEG_DIR:$PATH"
    fi
    if ! fn_web_have ffmpeg; then
        fn_web_fatal "ffmpeg not found on PATH and FN_FFMPEG_DIR did not provide it" \
            "Asset staging transcodes sfx/*.wav.pcm to Ogg/Vorbis with ffmpeg." \
            "Fix: install ffmpeg, or set FN_FFMPEG_DIR to a directory containing it." \
            "Nothing was built -- the web bundle is STALE."
    fi
    fn_web_have cmake || fn_web_fatal "cmake not found on PATH" \
        "Fix: install CMake (>= 3.16) and put it on PATH." \
        "Nothing was built -- the web bundle is STALE."
    if ! fn_web_have python3 && ! fn_web_have python; then
        fn_web_fatal "no python3/python on PATH" \
            "Needed by tools/assets/stage-assets.py, tools/assets/svg-to-webp.py" \
            "and tools/web/web-hash-assets.py (the content-hash rename)." \
            "Fix: install Python 3 and put it on PATH."
    fi
    [ -d "$PROJ/src" ] || fn_web_fatal "repo source dir missing: $PROJ/src"
    [ -d "$PROJ/FruitNinjaBada/Data" ] || fn_web_fatal \
        "game asset dump missing: $PROJ/FruitNinjaBada/Data" \
        "Asset staging reads it; without it the build stages an empty .data."
}

# =============================================================================
# WORKER
# =============================================================================
if [ "$IS_WORKER" -eq 1 ]; then
    # Serialize against any other build touching build/web/. Wait up to LOCK_WAIT
    # for an in-progress build; if it's still going, skip rather than race it.
    if ! acquire_lock "$LOCK_WAIT"; then
        echo "[$(now)] rebuild SKIPPED (another build holds the lock)" >>"$LOG"
        printf 'status=SKIPPED\nexit=0\nwhen=%s\nreason=another build holds the lock\n' "$(now)" >"$STATUS"
        exit 0
    fi

    WORKER_ARGS="${BUILD_SH_ARGS[*]:-(none)}"
    WASM_BEFORE_NAME="$(basename "$WASM")"
    WASM_BEFORE_HASH="$(fn_web_hash_file "$WASM")"

    # Exit trap: release the lock AND record the outcome where the next
    # dispatcher run (and the user) will find it. This is the whole reason a
    # detached nohup build can no longer swallow a failure: the exit status and
    # the FATAL text land in files, not just in a log nobody reads.
    worker_finish() {
        local code=$?
        release_lock
        local wasm_after
        wasm_after="$(ls -t "$BUILD_WEB"/fruit-ninja-*.wasm 2>/dev/null | head -1 || true)"
        [ -n "$wasm_after" ] || wasm_after="$BUILD_WEB/fruit-ninja.wasm"
        local after_hash
        after_hash="$(fn_web_hash_file "$wasm_after")"
        {
            printf 'status=%s\n' "$([ "$code" -eq 0 ] && echo OK || echo FAIL)"
            printf 'exit=%s\n'   "$code"
            printf 'when=%s\n'   "$(now)"
            printf 'args=%s\n'   "$WORKER_ARGS"
            printf 'wasm_before=%s %s\n' "$WASM_BEFORE_NAME" "${WASM_BEFORE_HASH:-none}"
            printf 'wasm_after=%s %s\n'  "$(basename "$wasm_after")" "${after_hash:-none}"
            printf 'log=%s\n'    "$LOG"
        } >"$STATUS"
        local fatals warns
        fatals="$(grep -n '^FATAL:\|FATAL:' "$LOG" 2>/dev/null | head -5 || true)"
        warns="$(grep -n 'WARNING:' "$LOG" 2>/dev/null | head -5 || true)"
        if [ "$code" -ne 0 ]; then
            {
                echo "web rebuild FAILED (exit $code) at $(now)"
                echo "  args: $WORKER_ARGS"
                echo "  wasm: $WASM_BEFORE_NAME -> $(basename "$wasm_after")"
                echo "  full log: $LOG"
                if [ -n "$fatals" ]; then echo "  --- reported errors ---"; echo "$fatals" | sed 's/^/  /'; fi
                if [ -z "$fatals" ]; then
                    echo "  --- last 15 log lines ---"
                    tail -15 "$LOG" 2>/dev/null | sed 's/^/  /'
                fi
            } >"$FAILED_MARKER"
            rm -f "$WARNED_MARKER"
        else
            rm -f "$FAILED_MARKER" "$FAILED_MARKER.reported"
            if [ -n "$warns" ]; then
                { echo "web rebuild OK with warnings at $(now)"; echo "$warns" | sed 's/^/  /'; echo "  full log: $LOG"; } >"$WARNED_MARKER"
            else
                rm -f "$WARNED_MARKER"
            fi
        fi
    }
    trap 'worker_finish' EXIT
    trap 'exit 143' INT TERM     # -> EXIT trap still runs, marker still written

    {
        echo "[$(now)] rebuild start (native) args: $WORKER_ARGS"
        preflight_toolchain
        # Single pipeline shared with CI: build.sh owns the pre-clean of link
        # outputs, the lib-first race workaround, the verify + link-retry AND the
        # artifact-hash verification (a build that leaves the wasm untouched
        # while sources are newer fails there, not here).
        # No build flags = respect the existing build/web configure (preserves a
        # locally-configured FN_WEB_DEBUG build).
        code=0
        bash "$PROJ/tools/web/build.sh" ${BUILD_SH_ARGS[@]+"${BUILD_SH_ARGS[@]}"} || code=$?
        if [ "$code" -eq 0 ]; then
            echo "[$(now)] rebuild OK"
        else
            echo "FATAL: [$(now)] rebuild FAILED -- tools/web/build.sh exited $code"
            exit "$code"
        fi
    } >"$LOG" 2>&1
    exit 0
fi

# =============================================================================
# DISPATCHER
# =============================================================================
# 1) Surface the PREVIOUS detached run's outcome first -- this is what makes a
#    nohup'd failure visible. A Stop hook exiting non-zero (not 2) shows its
#    stderr to the user without blocking the turn.
REPORT_EXIT=0
if [ -f "$FAILED_MARKER" ]; then
    echo "FATAL: the previous background web rebuild FAILED -- the served bundle is STALE" >&2
    sed 's/^/       /' "$FAILED_MARKER" >&2
    mv -f "$FAILED_MARKER" "$FAILED_MARKER.reported"   # report once, then move on
    REPORT_EXIT=1
fi
if [ -f "$WARNED_MARKER" ]; then
    echo "WARNING: the previous background web rebuild reported warnings" >&2
    sed 's/^/         /' "$WARNED_MARKER" >&2
    rm -f "$WARNED_MARKER"
fi

# 2) Gate on src-change (no build/web -> nothing configured -> nothing to do)
if [ ! -d "$BUILD_WEB" ]; then exit "$REPORT_EXIT"; fi
if [ "$FORCE" -eq 1 ]; then
    CHANGED="(forced)"
elif [ -f "$WASM" ]; then
    CHANGED="$(find "$PROJ/src" -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.c' -o -name '*.cc' -o -name '*.hpp' \) -newer "$WASM" -print -quit 2>/dev/null || true)"
else
    CHANGED="$(find "$PROJ/src" -type f -name '*.cpp' -print -quit 2>/dev/null || true)"
fi
[ -n "$CHANGED" ] || exit "$REPORT_EXIT"    # build is up to date

# 3) Preflight SYNCHRONOUSLY, before detaching: a broken toolchain must fail here
#    (visible in the hook output) instead of vanishing into the worker's log.
preflight_toolchain

# 4) Fast-path skip: don't launch a redundant worker while a live build runs. This
#    is advisory only -- the authoritative serialization is the worker's
#    acquire_lock, which safely handles the small window where two dispatchers
#    both launch.
if lock_holder_alive; then
    echo "[rebuild-web] a rebuild is already running; skipping"
    exit "$REPORT_EXIT"
fi
nohup bash "$0" --worker ${PASSTHRU[@]+"${PASSTHRU[@]}"} >/dev/null 2>&1 &
echo "[rebuild-web] src changed ($(basename "$CHANGED")); web rebuild started in background -> tmp/web-rebuild.log (hard-refresh the phone in ~1 min)"
echo "[rebuild-web] its outcome is recorded in tmp/web-rebuild.status; a failure is reported by the next run."
exit "$REPORT_EXIT"
