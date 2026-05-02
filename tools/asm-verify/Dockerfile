# Image for the asm-verify pipeline.
#
# Contains:
#   - Sourcery G++ Lite 2010q1-188 (= GCC 4.4.1, the upstream of Samsung's
#     Sourcery G++ 4.4-157 that built FruitNinja.exe)
#   - cmake, python3, rsync, git, build-essential
#   - i386 multilib runtime for the toolchain's 32-bit ELF binaries
#
# Build:
#   docker build -t fnverify tools/asm-verify
#
# Run (driven by tools/asm-verify/run.sh):
#   docker run --rm -v "$(pwd):/work" fnverify /work/tools/asm-verify/verify.sh

FROM --platform=linux/amd64 debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive

# i386 multilib for the Sourcery toolchain (i386 ELF binaries).
RUN dpkg --add-architecture i386 \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
        cmake make rsync git ca-certificates \
        python3 python3-pip \
        libc6:i386 libstdc++6:i386 zlib1g:i386 \
 && rm -rf /var/lib/apt/lists/*

# Toolchain. Pulled at build time from the Khadas mirror (the original
# Mentor URL is dead). Strip .git to keep the image small.
ARG TC_REPO=https://github.com/khadas/buildroot_toolchain_gcc_linux-x86_arm_Sourcery_Gpp_Lite-2010q1.git
RUN git clone --depth 1 "$TC_REPO" /opt/sourcery-2010q1 \
 && rm -rf /opt/sourcery-2010q1/.git

ENV FN_TOOLCHAIN_DIR=/opt/sourcery-2010q1
ENV PATH=/opt/sourcery-2010q1/bin:$PATH

# asm-differ: Simon Lindholm's tool for normalised asm comparison with
# register-rename heuristics. Python-only, no native deps. Pin to a
# specific commit for reproducibility.
ARG ASM_DIFFER_REV=dd7f9f0c021063eadfbdff7f31097f7039c3a5a7
RUN git clone https://github.com/simonlindholm/asm-differ /opt/asm-differ \
 && cd /opt/asm-differ \
 && git checkout "$ASM_DIFFER_REV" \
 && rm -rf .git \
 && pip3 install --break-system-packages --no-cache-dir colorama dataclasses watchdog
ENV PATH=/opt/asm-differ:$PATH
ENV PYTHONPATH=/opt/asm-differ:${PYTHONPATH:-}

# Sanity-check at build time so a broken image fails fast.
RUN /opt/sourcery-2010q1/bin/arm-none-eabi-g++ --version | head -1

WORKDIR /work
ENTRYPOINT ["/bin/bash"]
