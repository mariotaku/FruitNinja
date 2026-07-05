#!/usr/bin/env bash
# Single in-container web-build entrypoint. Local dev (tools/web/rebuild-web.sh)
# and CI (.github/workflows/pages.yml) both run THIS script inside the pinned
# emscripten/emsdk:6.0.0 image, so both paths share one deterministic pipeline:
#   configure-if-needed -> race-safe two-step build -> verify outputs (+retry link).
# CMake remains the packaging source of truth (shell.html, --preload-file .data,
# splash/UI webp copies, web-hash-assets.py POST_BUILD); this script only
# sequences the build and validates the link outputs.
#
# Usage (inside the container, repo mounted at /src, cwd /src):
#   bash /src/tools/web/build.sh [--debug|--release] [--reconfigure]
#     (no flags)     reuse the existing build/web configure when present -- this
#                    preserves a locally-configured FN_WEB_DEBUG=ON tree for the
#                    auto-rebuild hook; configures Release when the cache is absent
#     --debug        (re)configure Release + FN_WEB_DEBUG=ON (separate-DWARF /
#                    overflow-check debug variant; outputs stay unhashed)
#     --release      (re)configure Release + FN_WEB_DEBUG=OFF (hashed outputs)
#     --reconfigure  force a re-configure with the default (Release) settings
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
set -euo pipefail

if [ -f /src/CMakeLists.txt ]; then SRC=/src; else SRC="$(pwd)"; fi
BUILD_DIR="$SRC/build/web"
NPROC="$(nproc 2>/dev/null || echo 4)"

MODE=""          # "" = auto (respect existing configure), or debug/release
RECONFIGURE=0
for arg in "$@"; do
    case "$arg" in
        --debug)       MODE=debug ;;
        --release)     MODE=release ;;
        --reconfigure) RECONFIGURE=1 ;;
        *) echo "[build.sh] unknown flag: $arg (expected --debug|--release|--reconfigure)" >&2; exit 2 ;;
    esac
done

# --- configure (only when the cache is absent or a force flag is given) ------
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ] || [ -n "$MODE" ] || [ "$RECONFIGURE" -eq 1 ]; then
    CFG_ARGS=(-S "$SRC" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release)
    case "$MODE" in
        debug)   CFG_ARGS+=(-DFN_WEB_DEBUG=ON) ;;
        release) CFG_ARGS+=(-DFN_WEB_DEBUG=OFF) ;;
    esac
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
    [ -f "$BUILD_DIR/fruit-ninja.html" ] && [ -n "$(main_wasm)" ]
}

# --- build (race-safe two-step) ------------------------------------------------
clean_link_outputs
echo "[build.sh] building static lib target first (parallel-link race workaround)"
cmake --build "$BUILD_DIR" --target fruit-ninja-game -j"$NPROC"

OK=0
for attempt in 1 2 3; do
    clean_link_outputs
    if cmake --build "$BUILD_DIR" -j"$NPROC"; then
        if verify_outputs; then OK=1; break; fi
    fi
    echo "[build.sh] web build incomplete (attempt $attempt/3); retrying link" >&2
done

if [ "$OK" -ne 1 ]; then
    echo "[build.sh] FATAL: web build incomplete after 3 attempts -- missing fruit-ninja.html or main wasm in $BUILD_DIR" >&2
    exit 1
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
    echo "[build.sh] FATAL: entry rename failed -- $BUILD_DIR/index.html missing" >&2
    exit 1
fi

echo "[build.sh] OK: $(basename "$WASM") ($(wc -c < "$WASM") bytes); entry: index.html"
