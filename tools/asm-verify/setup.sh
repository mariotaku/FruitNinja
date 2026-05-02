#!/usr/bin/env bash
# One-time bootstrap for the asm-verify Docker image.
#
# Builds tools/asm-verify/Dockerfile, which fetches the Sourcery G++ Lite
# 2010q1-188 (GCC 4.4.1) toolchain from the Khadas mirror and bakes it
# into the image alongside cmake/python3/rsync/i386 multilib.
#
# Run from the project root (or anywhere -- script discovers its own dir):
#   bash tools/asm-verify/setup.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
IMAGE="${ASM_VERIFY_IMAGE:-fnverify}"

if ! command -v docker > /dev/null; then
    echo "ERROR: docker not on PATH. Install Docker Desktop (or rancher-desktop)." >&2
    exit 1
fi

echo "Building $IMAGE from $SCRIPT_DIR/Dockerfile ..."
docker build -t "$IMAGE" "$SCRIPT_DIR"

echo
echo "Image ready. Test with:"
echo "  docker run --rm $IMAGE -c 'arm-none-eabi-g++ --version | head -1'"
echo
echo "Then drive verification via:"
echo "  bash tools/asm-verify/run.sh"
