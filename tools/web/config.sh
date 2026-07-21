# Shared web-build config -- single source of truth for values duplicated across
# tools/web/rebuild-web.sh (local dev) and .github/workflows/pages.yml (CI).
# POSIX-sourceable (`. tools/web/config.sh`); CI reads it into $GITHUB_ENV.
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
