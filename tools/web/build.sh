#!/usr/bin/env bash
# Single in-container web-build entrypoint. Local dev (tools/web/rebuild-web.sh)
# and CI (.github/workflows/pages.yml) both run THIS script inside the pinned
# emscripten/emsdk:6.0.0 image, so both paths share one deterministic pipeline:
#   configure-if-needed -> race-safe two-step build -> verify outputs (+retry link).
# CMake remains the packaging source of truth (shell.html, --preload-file .data,
# splash/UI webp copies, web-hash-assets.py POST_BUILD); this script only
# sequences the build and validates the link outputs.
#
# The asset staging step (tools/assets/stage-assets.py --web, transcodes sfx
# .wav.pcm to Ogg/Vorbis into the staging dir + emits a loop-point JSON,
# re-encodes textures to WebP, and subsets the CJK font, for the Web Audio
# API backend SoundManagerWebAudio.cpp) is a CMake target dependency
# (fn_asset_staging, see CMakeLists.txt) rather than a step in this script --
# CMake's dependency graph guarantees it runs before fruit-ninja links regardless
# of how the build is invoked (this script, a direct `cmake --build`, or an IDE).
# stage-assets.py self-provisions its tools (apt-get installs ffmpeg /
# fonttools when missing inside the container), so this script no longer does.
#
# Usage (inside the container, repo mounted at /src, cwd /src):
#   bash /src/tools/web/build.sh [--debug|--release] [--reconfigure] [--profiling]
#     (no flags)     reuse the existing build/web configure when present -- this
#                    preserves a locally-configured FN_WEB_DEBUG=ON tree for the
#                    auto-rebuild hook; configures Release when the cache is absent
#     --debug        (re)configure Release + FN_WEB_DEBUG=ON (separate-DWARF /
#                    overflow-check debug variant; outputs stay unhashed)
#     --release      (re)configure Release + FN_WEB_DEBUG=OFF (hashed outputs)
#     --reconfigure  force a re-configure with the default (Release) settings
#     --profiling    keep C++ function names in the wasm (-DFN_WEB_PROFILING=ON)
#                    for a browser flame-graph (Chrome/Firefox DevTools
#                    Performance); forces a reconfigure. Sticky in the build/web
#                    cache until the next --release/--debug/--reconfigure run
#                    (every configure branch below sets FN_WEB_PROFILING
#                    explicitly ON or OFF, so a later plain rebuild can't
#                    inherit a stale ON).
#
# Known-flaky link failures this pipeline mitigates:
#   - parallel -j race: fruit-ninja.html links before libfruit-ninja-game.a's
#     rule registers ("No rule to make target ..." / 168-byte wasm)
#     -> build the static lib target FIRST, then the executable
#   - corrupted/truncated wasm intermediate surviving across builds
#     (wasm-metadce "Section extends beyond end of input")
#     -> pre-clear ONLY the link outputs (never the 49MB .data) each attempt
#   - occasional missing fruit-ninja.html -> verify + up to 3 link retries
#
# After a verified build, the emcc entry HTML (fruit-ninja.html) is renamed to
# index.html so the served entry point is the directory root. Internal
# .js/.wasm/.data names are untouched (release: web-hash-assets.py has already
# rewritten the html's refs to fruit-ninja-<sha8>.* as a CMake POST_BUILD;
# debug: refs stay unhashed) -- the rename only changes the html's own filename.
#
# FAILURE POLICY (see also tools/web/config.sh):
#   - strict mode + an ERR trap that names the failing line/command/status
#   - every preflight failure names exactly what is missing and how to fix it
#   - an unknown flag is a hard error listing the accepted flags (never ignored)
#   - the PRIMARY success criterion is the wasm CONTENT HASH moving, not the
#     exit code: a build that "succeeds" while leaving the artifact untouched
#     even though sources are newer is reported as FATAL, not as success.
set -euo pipefail

FN_WEB_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=tools/web/config.sh
. "$FN_WEB_SCRIPT_DIR/config.sh"
fn_web_strict

if [ -f /src/CMakeLists.txt ]; then SRC=/src; else SRC="$(pwd)"; fi
SRC_SH="$SRC"          # shell-side (unconverted) repo root, for find/stat below
BUILD_DIR="$SRC/build/web"
# Native Windows/MSYS (no container): the emcc/cmake/ninja tools are native
# Windows exes that don't understand MSYS `/c/...` paths -- hand them mixed
# `C:/...` form (cygpath -m), which MSYS bash file ops also accept. No-op in the
# Linux container (cygpath absent), where /src stays a valid Linux path.
if command -v cygpath >/dev/null 2>&1; then
    SRC="$(cygpath -m "$SRC")"
    BUILD_DIR="$(cygpath -m "$BUILD_DIR")"
fi
NPROC="$(nproc 2>/dev/null || echo 4)"

# Asset-staging tools (ffmpeg / fonttools) are self-provisioned by
# tools/assets/stage-assets.py at the point of use; this script no longer
# apt-installs them.

MODE=""          # "" = auto (respect existing configure), or debug/release
RECONFIGURE=0
PROFILING=0
FN_WEB_ACCEPTED_FLAGS="--debug --release --reconfigure --profiling"
for arg in "$@"; do
    case "$arg" in
        --debug)       MODE=debug ;;
        --release)     MODE=release ;;
        --reconfigure) RECONFIGURE=1 ;;
        --profiling)   PROFILING=1; RECONFIGURE=1 ;;
        # A silently-ignored flag once produced a "release" build that was
        # nothing of the sort. Unknown flags are ALWAYS fatal.
        *) FN_WEB_FATAL_CODE=2 fn_web_fatal "build.sh: unknown flag: $arg" \
               "Accepted flags: $FN_WEB_ACCEPTED_FLAGS" \
               "No flag = reuse the existing build/web configure." ;;
    esac
done

# --- preflight ---------------------------------------------------------------
# Each check has its own message naming the missing piece and the fix; they are
# deliberately NOT collapsed into one generic "environment error".
[ -f "$SRC/CMakeLists.txt" ] || fn_web_fatal \
    "repo root not found: no CMakeLists.txt at $SRC" \
    "Run build.sh from the repo root (or with the repo mounted at /src)."
[ -d "$SRC/FruitNinjaBada/Data" ] || fn_web_fatal \
    "game asset dump missing: $SRC/FruitNinjaBada/Data" \
    "Asset staging (tools/assets/stage-assets.py, the fn_asset_staging target)" \
    "reads this directory; without it the build stages an empty .data." \
    "CI: the 'Fetch game assets' step must run before build.sh."
fn_web_have cmake || fn_web_fatal "cmake not found on PATH" \
    "Install CMake (>= 3.16) and put it on PATH."
if ! fn_web_have emcc || ! fn_web_have emcmake; then
    if fn_web_is_ci; then
        fn_web_fatal "emcc/emcmake not found on PATH (CI)" \
            "The 'Set up emsdk' step (mymindstorm/setup-emsdk) must run before build.sh," \
            "pinned to EMSDK_VERSION=$EMSDK_VERSION from tools/web/config.sh."
    fi
    fn_web_fatal "emcc/emcmake not found on PATH" \
        "Activate the emsdk before calling build.sh:" \
        "  . \${FN_EMSDK:-/c/tools/emsdk}/emsdk_env.sh" \
        "Pinned version: EMSDK_VERSION=$EMSDK_VERSION (tools/web/config.sh)." \
        "tools/web/rebuild-web.sh does this for you."
fi
# Toolchain-version drift is a WARNING, not a failure: it does not cause the
# silent no-op this hardening targets, CI installs the pin exactly, and a hard
# fail would block a local dev on a patch-level difference. It IS printed
# loudly, and rebuild-web.sh copies WARNING lines into its status marker so a
# detached build's warning still reaches the user.
FN_EMCC_VERSION="$(fn_web_emcc_version)"
if [ -n "$FN_EMCC_VERSION" ] && [ "$FN_EMCC_VERSION" != "$EMSDK_VERSION" ]; then
    fn_web_warn "emcc version $FN_EMCC_VERSION != pinned EMSDK_VERSION $EMSDK_VERSION" \
        "The pin exists because a floating emsdk silently changed the wasm" \
        "STACK_SIZE default (5MB -> 64KB) and corrupted the HUD text path." \
        "Fix: emsdk install $EMSDK_VERSION && emsdk activate $EMSDK_VERSION" \
        "Set FN_WEB_STRICT_EMSDK=1 to turn this warning into a hard failure."
    if [ "${FN_WEB_STRICT_EMSDK:-0}" = "1" ]; then
        fn_web_fatal "emcc version mismatch and FN_WEB_STRICT_EMSDK=1"
    fi
fi
# Python: CMake's find_package(Python3 REQUIRED) needs it at configure time and
# every asset-staging / hash step shells out to it.
if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
    FN_CACHED_PYTHON="$(sed -n 's/^Python3_EXECUTABLE:FILEPATH=//p' "$BUILD_DIR/CMakeCache.txt" | head -1)"
else
    FN_CACHED_PYTHON=""
fi
if [ -n "$FN_CACHED_PYTHON" ] && [ ! -x "$FN_CACHED_PYTHON" ] && [ ! -f "$FN_CACHED_PYTHON" ]; then
    fn_web_fatal "cached Python3_EXECUTABLE no longer exists: $FN_CACHED_PYTHON" \
        "build/web/CMakeCache.txt points at a python that was moved or uninstalled." \
        "Fix: reinstall it, or delete build/web/CMakeCache.txt and reconfigure" \
        "     (bash tools/web/build.sh --reconfigure)."
elif [ -z "$FN_CACHED_PYTHON" ] && ! fn_web_have python3 && ! fn_web_have python; then
    fn_web_fatal "no python3/python on PATH" \
        "Needed by find_package(Python3 REQUIRED) and by tools/assets/stage-assets.py," \
        "tools/assets/svg-to-webp.py and tools/web/web-hash-assets.py."
fi
# Generator make-program: `cmake --build` uses the CMAKE_MAKE_PROGRAM baked into
# the cache (here, usually CLion's bundled ninja.exe). If that path vanished the
# build fails in a confusing way, so name it precisely.
if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
    FN_MAKE_PROG="$(sed -n 's/^CMAKE_MAKE_PROGRAM:[A-Z]*=//p' "$BUILD_DIR/CMakeCache.txt" | head -1)"
    if [ -n "$FN_MAKE_PROG" ] && [ ! -f "$FN_MAKE_PROG" ] && ! fn_web_have "$FN_MAKE_PROG"; then
        fn_web_fatal "cached CMAKE_MAKE_PROGRAM does not exist: $FN_MAKE_PROG" \
            "build/web/CMakeCache.txt refers to a build tool that was moved or uninstalled." \
            "Fix: put that tool back on that path, or delete build/web/CMakeCache.txt" \
            "     and reconfigure (bash tools/web/build.sh --reconfigure)."
    fi
fi

# --- configure (only when the cache is absent or a force flag is given) ------
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ] || [ -n "$MODE" ] || [ "$RECONFIGURE" -eq 1 ]; then
    CFG_ARGS=(-S "$SRC" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release)
    case "$MODE" in
        debug)   CFG_ARGS+=(-DFN_WEB_DEBUG=ON) ;;
        release) CFG_ARGS+=(-DFN_WEB_DEBUG=OFF) ;;
    esac
    # Always pass FN_WEB_PROFILING explicitly (both ON and OFF): otherwise a
    # previous --profiling run's cached ON would survive an unrelated
    # --release/--debug/--reconfigure that doesn't re-specify it.
    if [ "$PROFILING" -eq 1 ]; then
        CFG_ARGS+=(-DFN_WEB_PROFILING=ON)
    else
        CFG_ARGS+=(-DFN_WEB_PROFILING=OFF)
    fi
    echo "[build.sh] configuring: emcmake cmake ${CFG_ARGS[*]}"
    emcmake cmake "${CFG_ARGS[@]}"
else
    echo "[build.sh] existing configure found ($BUILD_DIR/CMakeCache.txt); skipping configure"
fi

# --- helpers ------------------------------------------------------------------
# Link outputs only -- NEVER the .data (49MB, incremental, reused by web-hash).
clean_link_outputs() {
    rm -f "$BUILD_DIR/fruit-ninja.wasm" \
          "$BUILD_DIR/fruit-ninja.js" \
          "$BUILD_DIR/fruit-ninja.html" \
          "$BUILD_DIR/fruit-ninja.debug.wasm"
}

# Newest main wasm: hashed release (fruit-ninja-<sha8>.wasm) or unhashed debug
# (fruit-ninja.wasm); the DWARF sidecar fruit-ninja.debug.wasm never counts.
main_wasm() {
    ls -t "$BUILD_DIR"/fruit-ninja*.wasm 2>/dev/null | grep -v '\.debug\.wasm$' | head -1 || true
}

verify_outputs() {
    # Release: web-hash-assets.py (POST_BUILD) renames fruit-ninja.html ->
    # index.html; debug: fruit-ninja.html stays until the post-loop mv. Accept
    # EITHER, else verify wrongly fails after a good release build, forcing a
    # spurious retry whose re-link objcopy's an already-hashed-away fruit-ninja.wasm.
    { [ -f "$BUILD_DIR/index.html" ] || [ -f "$BUILD_DIR/fruit-ninja.html" ]; } && [ -n "$(main_wasm)" ]
}

main_js() {
    ls -t "$BUILD_DIR"/fruit-ninja*.js 2>/dev/null | head -1 || true
}
main_data() {
    ls -t "$BUILD_DIR"/fruit-ninja*.data 2>/dev/null | head -1 || true
}

# --- pre-build snapshot (the no-op detector) -----------------------------------
# The outputs are content-hash NAMED, so "did the hash move" is the reliable
# success signal; the exit code is not. Snapshot BEFORE clean_link_outputs.
#
# no-op vs broken, decided after the build:
#   hash moved                                  -> OK (real build)
#   hash same + no source newer than artifact   -> OK (legitimately up to date)
#   hash same + source newer + static lib MOVED -> FATAL (contradiction: the
#       object code changed yet the linked artifact did not -- the classic
#       "build claimed success but shipped the old wasm")
#   hash same + source newer + static lib same  -> WARNING (the recompile
#       produced byte-identical objects, e.g. a comment-only edit) -- failing
#       here would make every harmless touch a build failure.
PRE_WASM="$(main_wasm)"
PRE_WASM_HASH="$(fn_web_hash_file "$PRE_WASM")"
PRE_JS_HASH="$(fn_web_hash_file "$(main_js)")"
PRE_LIB_HASH="$(fn_web_hash_file "$BUILD_DIR/libfruit-ninja-game.a")"
SRC_NEWER=""
if [ -n "$PRE_WASM" ] && [ -f "$PRE_WASM" ]; then
    SRC_NEWER="$(find "$SRC_SH/src" "$SRC_SH/tools/web" "$SRC_SH/tools/assets" "$SRC_SH/CMakeLists.txt" \
        -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.c' -o -name '*.cc' -o -name '*.hpp' \
                   -o -name '*.py' -o -name '*.html' -o -name 'CMakeLists.txt' \) \
        -newer "$PRE_WASM" -print 2>/dev/null | head -1 || true)"
fi

# --- build (race-safe two-step) ------------------------------------------------
clean_link_outputs
echo "[build.sh] building static lib target first (parallel-link race workaround)"
cmake --build "$BUILD_DIR" --target fruit-ninja-game -j"$NPROC"

OK=0
for attempt in 1 2 3; do
    clean_link_outputs
    # Link + POST_BUILD content-hash SERIALLY (-j1): the exe link otherwise races
    # the libfruit-ninja-game.a rule under -j, and the retry loop that raced-link
    # triggered could pair a .js and .wasm from DIFFERENT link passes -> broken
    # bundle (wasm LinkError). The lib compile above stays parallel; only this
    # cheap link+hash step is serialized, so a single pass succeeds and the hash
    # runs exactly once on one matched link.
    if cmake --build "$BUILD_DIR" -j1; then
        if verify_outputs; then OK=1; break; fi
    fi
    echo "[build.sh] web build incomplete (attempt $attempt/3); retrying link" >&2
done

if [ "$OK" -ne 1 ]; then
    fn_web_fatal "web build incomplete after 3 attempts -- missing fruit-ninja.html or main wasm in $BUILD_DIR" \
        "Read the cmake/ninja output above for the first compile or link error."
fi

WASM="$(main_wasm)"

# --- entry-point rename: fruit-ninja.html -> index.html -------------------------
# emcc always emits fruit-ninja.html (that's what the verify loop checks); the
# SERVED entry is index.html. Rename after the loop succeeds so both release
# (hashed internal refs already rewritten) and debug (unhashed refs) get it.
if [ -f "$BUILD_DIR/fruit-ninja.html" ]; then
    mv -f "$BUILD_DIR/fruit-ninja.html" "$BUILD_DIR/index.html"
fi
if [ ! -f "$BUILD_DIR/index.html" ]; then
    fn_web_fatal "entry rename failed -- $BUILD_DIR/index.html missing" \
        "fruit-ninja.html was neither produced nor renamed."
fi

# --- post-build verification (MANDATORY; primary success criterion) ------------
# 1) the outputs exist and are non-trivial (a 0-byte/truncated link output is a
#    failure the tools happily leave behind after a partial link)
POST_WASM="$(main_wasm)"
POST_JS="$(main_js)"
POST_DATA="$(main_data)"
[ -n "$POST_WASM" ] || fn_web_fatal "no wasm in $BUILD_DIR after a 'successful' build"
[ -n "$POST_JS" ]   || fn_web_fatal "no fruit-ninja*.js in $BUILD_DIR after a 'successful' build"
[ -n "$POST_DATA" ] || fn_web_fatal "no fruit-ninja*.data in $BUILD_DIR after a 'successful' build" \
    "The --preload-file asset package is missing -- asset staging (fn_asset_staging) likely failed."
fn_web_require_size "$BUILD_DIR/index.html" 512    "entry html"
fn_web_require_size "$POST_WASM"            65536  "wasm"
fn_web_require_size "$POST_JS"              4096   "js loader"
fn_web_require_size "$POST_DATA"            65536  "asset package (.data)"

# 2) did the artifact actually MOVE? (see the pre-build snapshot for the
#    no-op-vs-broken decision table)
POST_WASM_HASH="$(fn_web_hash_file "$POST_WASM")"
POST_JS_HASH="$(fn_web_hash_file "$POST_JS")"
POST_LIB_HASH="$(fn_web_hash_file "$BUILD_DIR/libfruit-ninja-game.a")"

if [ -z "$PRE_WASM_HASH" ]; then
    echo "[build.sh] verify: first build in this tree -- produced $(basename "$POST_WASM")"
elif [ "$POST_WASM_HASH" != "$PRE_WASM_HASH" ] || [ "$POST_JS_HASH" != "$PRE_JS_HASH" ]; then
    echo "[build.sh] verify: artifact changed ($(basename "$PRE_WASM") -> $(basename "$POST_WASM"))"
elif [ -z "$SRC_NEWER" ]; then
    echo "[build.sh] verify: no-op rebuild -- nothing newer than $(basename "$PRE_WASM"), hash unchanged (this is fine)"
elif [ "$POST_LIB_HASH" != "$PRE_LIB_HASH" ]; then
    fn_web_fatal "build reported success but the wasm did NOT change" \
        "wasm: $(basename "$POST_WASM") -- content hash unchanged ($POST_WASM_HASH)" \
        "Newer than that artifact: $SRC_NEWER" \
        "libfruit-ninja-game.a DID change, so the object code moved but the linked" \
        "output did not -- the link/hash step did not use the fresh objects." \
        "Do NOT trust this build. Retry with: bash tools/web/build.sh --reconfigure" \
        "(or delete build/web and rebuild) and check the link output above."
else
    fn_web_warn "sources are newer than the wasm, yet neither libfruit-ninja-game.a nor the wasm changed" \
        "Newest changed source: $SRC_NEWER" \
        "The recompile produced byte-identical objects (comment/whitespace-only" \
        "edit, or the change is outside the web target). Treated as OK." \
        "If you expected a code change here, the edit did not reach the web build."
fi

echo "[build.sh] OK: $(basename "$WASM") ($(wc -c < "$WASM") bytes); entry: index.html"
