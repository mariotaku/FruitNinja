# Image for the asm-verify pipeline (build tag: fnverify-bada).
#
# Base: ghcr.io/mariotaku/bada-sdk:1.1.0 -- Debian bookworm carrying Samsung
# Sourcery G++ 4.4.1 (arm-samsung-nucleuseabi -- the binary's actual compiler
# family) at /opt/codesourcery, already on PATH. This image only layers the
# build tooling (cmake/make/python3/rsync/git) on top.
#
# Build:
#   docker build -t fnverify-bada tools/asm-verify
#
# Run (driven by tools/asm-verify/run.sh and bindiff-pipeline.sh):
#   docker run --rm -v "$(pwd):/work" fnverify-bada /work/tools/asm-verify/verify.sh

FROM ghcr.io/mariotaku/bada-sdk:1.1.0

ENV DEBIAN_FRONTEND=noninteractive

# Build tooling on top of the SDK toolchain base.
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        cmake make rsync git ca-certificates \
        python3 python3-pip \
 && rm -rf /var/lib/apt/lists/*

# Toolchain comes from the base image at /opt/codesourcery (already on PATH).
# Note: asm-verify.py runs its homegrown mnemonic-level LCS scorer; asm-differ is
# intentionally NOT installed here (USE_ASM_DIFFER stays false). See the TODO in
# tools/asm-verify/asm-verify.py for why operand-level diffing is blocked.

# Sanity-check at build time so a broken image fails fast.
RUN arm-samsung-nucleuseabi-g++ --version | head -1

WORKDIR /work
ENTRYPOINT ["/bin/bash"]
