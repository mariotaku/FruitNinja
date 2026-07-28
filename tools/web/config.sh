# Shared web-build config + failure-reporting helpers -- single source of truth
# for values duplicated across tools/web/rebuild-web.sh (local dev),
# tools/web/build.sh (local + CI) and .github/workflows/pages.yml (CI).
# POSIX-sourceable (`. tools/web/config.sh`); CI reads it into $GITHUB_ENV.
#
# SOURCE-SAFE: this file only assigns variables and defines functions. It never
# runs `set -e`, never sets a trap and never exits, so the CI step that does
# `. tools/web/config.sh && echo "EMSDK_VERSION=..." >> $GITHUB_ENV` is
# unaffected. Callers opt into strict mode explicitly via fn_web_strict.
#
# Pinned (not "latest"): a floating version silently changed the default wasm
# STACK_SIZE (5MB -> 64KB at emscripten 3.1.27), which overflowed the HUD-text
# render path into static globals. Pin to a concrete version so the toolchain
# can't drift underneath us and CI/local builds stay reproducible.
#
# Both local dev and CI now build with NATIVE emsdk (emcc on PATH), not Docker.
# Local: rebuild-web.sh sources $FN_EMSDK/emsdk_env.sh (default /c/tools/emsdk).
# CI: .github/workflows/pages.yml installs this exact version via
#   mymindstorm/setup-emsdk@v14.
EMSDK_VERSION="6.0.0"

# =============================================================================
# Failure-reporting helpers (shared by build.sh / rebuild-web.sh)
#
# Rationale: both scripts have historically SUCCEEDED SILENTLY -- a missing
# ninja made rebuild-web.sh no-op with exit 0, and an unsupported flag was
# dropped without a word, so a "release" build shipped whatever was already
# there. Everything below exists to turn those into loud, specific failures.
#
# All printed output is ASCII only (the Windows console codepage mangles
# anything else) -- use "->" and "--", never Unicode arrows or dashes.
# =============================================================================

# fn_web_fatal <headline> [hint ...] -- print "FATAL: <headline>" plus indented
# hint lines to stderr and exit. Exit code defaults to 1; set FN_WEB_FATAL_CODE
# before calling to use another (build.sh uses 2 for usage errors).
fn_web_fatal() {
    printf 'FATAL: %s\n' "$1" >&2
    shift
    while [ "$#" -gt 0 ]; do printf '       %s\n' "$1" >&2; shift; done
    exit "${FN_WEB_FATAL_CODE:-1}"
}

# fn_web_warn <headline> [hint ...] -- non-fatal, but greppable ("WARNING:").
fn_web_warn() {
    printf 'WARNING: %s\n' "$1" >&2
    shift
    while [ "$#" -gt 0 ]; do printf '         %s\n' "$1" >&2; shift; done
}

fn_web_info() { printf '%s\n' "$*"; }

# fn_web_on_err <status> <line> <command> <source> -- ERR-trap body. Names the
# failing LINE, the command and the exit status, so an unexpected failure is
# never just a bare non-zero exit.
fn_web_on_err() {
    printf 'FATAL: %s: line %s: command failed (exit %s)\n' "${4:-$0}" "$2" "$1" >&2
    printf '       failing command: %s\n' "$3" >&2
    exit "$1"
}

# fn_web_strict -- strict mode + the ERR trap. -E (errtrace) makes the trap fire
# inside functions and command substitutions too, which is where the silent
# failures used to hide.
fn_web_strict() {
    set -Eeuo pipefail
    trap 'fn_web_on_err "$?" "$LINENO" "$BASH_COMMAND" "${BASH_SOURCE[0]##*/}"' ERR
}

# fn_web_is_ci -- true inside GitHub Actions (or any CI exporting $CI). Used to
# gate checks that are a local-workstation concern only (FN_NINJA_DIR, the
# emsdk_env.sh fallback): CI provisions its toolchain in the workflow, so those
# hints would be misleading there.
fn_web_is_ci() { [ -n "${CI:-}" ] || [ -n "${GITHUB_ACTIONS:-}" ]; }

fn_web_have() { command -v "$1" >/dev/null 2>&1; }

# fn_web_hash_file <path> -- content hash of a file, or "" when it is absent.
# The web outputs are content-hash NAMED, so the hash is the reliable
# "did the build actually produce something new" signal -- the exit code is not.
fn_web_hash_file() {
    [ -f "$1" ] || { printf ''; return 0; }
    if fn_web_have sha256sum; then
        sha256sum "$1" | cut -d' ' -f1
    elif fn_web_have shasum; then
        shasum -a 256 "$1" | cut -d' ' -f1
    elif fn_web_have openssl; then
        openssl dgst -sha256 "$1" | awk '{print $NF}'
    else
        # Last resort: size+cksum. Weaker, but still detects "artifact did not move".
        printf '%s-%s' "$(wc -c <"$1" | tr -d ' ')" "$(cksum <"$1" | cut -d' ' -f1)"
    fi
}

# fn_web_require_size <path> <min_bytes> <label> -- fail unless the file exists
# and is at least min_bytes (catches 0-byte / truncated link outputs, which the
# build tools happily leave behind after a partial link).
fn_web_require_size() {
    [ -f "$1" ] || fn_web_fatal "$3 missing: $1" \
        "The build reported success but did not produce this file."
    _sz="$(wc -c <"$1" | tr -d ' ')"
    [ "$_sz" -ge "$2" ] || fn_web_fatal "$3 is implausibly small: $1 ($_sz bytes, expected >= $2)" \
        "A truncated or 0-byte artifact means the link or the post-build hash step failed."
    unset _sz
}

# fn_web_emcc_version -- "x.y.z" parsed from emcc -v, or "" when unavailable.
fn_web_emcc_version() {
    emcc -v 2>&1 | head -1 | grep -Eo '[0-9]+\.[0-9]+\.[0-9]+' | head -1 || printf ''
}
