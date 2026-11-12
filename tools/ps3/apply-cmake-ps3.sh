#!/bin/bash
# Aplica el cableado FRUIT_PLATFORM_PS3 al CMakeLists.txt raíz.
# Corré esto en Ubuntu/WSL dentro del repo (branch feature/ps3-scaffold).
#
#   bash tools/ps3/apply-cmake-ps3.sh
#
# Si CMakeLists.txt quedó roto ("PLACEHOLDER"), primero restaura desde main:
#   git show main:CMakeLists.txt > CMakeLists.txt
#   # o desde el commit previo al placeholder:
#   git checkout af04c274 -- CMakeLists.txt

set -eo pipefail
PROJ="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$PROJ"

if grep -q 'PLACEHOLDER' CMakeLists.txt 2>/dev/null; then
    echo "CMakeLists.txt está roto (PLACEHOLDER). Restaurando desde main..."
    git show main:CMakeLists.txt > CMakeLists.txt || \
      git show origin/main:CMakeLists.txt > CMakeLists.txt
fi

if grep -q 'FRUIT_PLATFORM_PS3' CMakeLists.txt; then
    echo "FRUIT_PLATFORM_PS3 ya está en CMakeLists.txt — nada que hacer."
    exit 0
fi

echo "Aplicando cmake/ps3-cmake.patch ..."
patch -p1 < cmake/ps3-cmake.patch
echo "OK. Ahora:"
echo "  export PS3DEV=... PSL1GHT=..."
echo "  bash tools/ps3/build.sh"
