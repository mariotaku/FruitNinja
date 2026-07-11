# Shared web-build config -- single source of truth for values duplicated across
# tools/web/rebuild-web.sh (local dev) and .github/workflows/pages.yml (CI).
# POSIX-sourceable (`. tools/web/config.sh`); CI reads it into $GITHUB_ENV.
#
# Pinned (not :latest): a floating tag silently changed the default wasm
# STACK_SIZE (5MB -> 64KB at emscripten 3.1.27), which overflowed the HUD-text
# render path into static globals. Pin to a concrete version so the toolchain
# can't drift underneath us and CI/local builds stay reproducible.
EMSDK_IMAGE="emscripten/emsdk:6.0.0"
