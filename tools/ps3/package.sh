#!/bin/bash
# Build a PS3 .pkg named TestBuildFruitNinja1.0.pkg from the PS3 ELF.
#
# Prerequisites:
#   export PS3DEV=/usr/local/ps3dev
#   export PSL1GHT=$PS3DEV/psl1ght
#   export PATH=$PS3DEV/bin:$PS3DEV/ppu/bin:$PATH
#
# Usage (from repo root, after a successful PS3 build):
#   bash tools/ps3/package.sh [path/to/fruit-ninja.elf]
#
# Default ELF path: build/ps3/fruit-ninja

set -eo pipefail

PROJ="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="$PROJ/build/ps3"
PKG_STAGE="$BUILD_DIR/pkg-stage"
OUT_PKG="$BUILD_DIR/TestBuildFruitNinja1.0.pkg"

TITLE="Fruit Ninja Test Build"
APPID="FNTEST100"          # 9 chars max for TITLE_ID style ids
CONTENTID="UP0001-${APPID}_00-0000000000000000"
VERSION="01.00"

ELF="${1:-$BUILD_DIR/fruit-ninja}"
if [ ! -f "$ELF" ]; then
    # CMake sometimes names it fruit-ninja.elf
    if [ -f "$BUILD_DIR/fruit-ninja.elf" ]; then
        ELF="$BUILD_DIR/fruit-ninja.elf"
    else
        echo "ERROR: ELF not found at $ELF" >&2
        echo "Build first: bash tools/ps3/build.sh" >&2
        exit 1
    fi
fi

# Resolve packaging tools (installed with PSL1GHT / ps3toolchain)
find_tool() {
    local name="$1"
    if command -v "$name" >/dev/null 2>&1; then
        command -v "$name"
        return
    fi
    for d in "$PS3DEV/bin" "$PSL1GHT/host/bin" "$PSL1GHT/tools"; do
        if [ -x "$d/$name" ]; then
            echo "$d/$name"
            return
        fi
    done
    echo ""
}

FSELF=$(find_tool fself)
SFO=$(find_tool sfo.py)
PKG=$(find_tool pkg.py)
STRIP=$(find_tool ppu-strip)
SPRX=$(find_tool sprxlinker)

if [ -z "$FSELF" ] || [ -z "$SFO" ] || [ -z "$PKG" ]; then
    echo "ERROR: missing packaging tools (fself / sfo.py / pkg.py)." >&2
    echo "Make sure PS3DEV and PSL1GHT are set and on PATH." >&2
    exit 1
fi

echo "==> Packaging $ELF"
echo "    TITLE=$TITLE  APPID=$APPID"
echo "    OUTPUT=$OUT_PKG"

rm -rf "$PKG_STAGE"
mkdir -p "$PKG_STAGE/USRDIR"

# 1) Strip + sprxlinker (optional but recommended)
WORK_ELF="$BUILD_DIR/fruit-ninja.pkg.elf"
cp "$ELF" "$WORK_ELF"
if [ -n "$STRIP" ]; then
    "$STRIP" "$WORK_ELF" || true
fi
if [ -n "$SPRX" ]; then
    "$SPRX" "$WORK_ELF" || true
fi

# 2) NPDRM SELF -> EBOOT.BIN  (required for .pkg)
#    fself -n = NPDRM mode for packaging
echo "==> fself -n -> EBOOT.BIN"
"$FSELF" -n "$WORK_ELF" "$PKG_STAGE/USRDIR/EBOOT.BIN"

# 3) PARAM.SFO
echo "==> sfo.py -> PARAM.SFO"
SFO_XML="${PS3DEV}/bin/sfo.xml"
if [ ! -f "$SFO_XML" ]; then
    # Minimal fallback if the SDK template is missing
    SFO_XML="$BUILD_DIR/sfo.xml"
    cat > "$SFO_XML" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<sfo>
  <value name="APP_VER" type="string">${VERSION}</value>
  <value name="ATTRIBUTE" type="int">0</value>
  <value name="CATEGORY" type="string">HG</value>
  <value name="LICENSE" type="string">Fruit Ninja fan port - test build</value>
  <value name="RESOLUTION" type="int">1</value>
  <value name="TITLE" type="string">${TITLE}</value>
  <value name="TITLE_ID" type="string">${APPID}</value>
  <value name="VERSION" type="string">${VERSION}</value>
</sfo>
EOF
fi

"$SFO" --title "$TITLE" --appid "$APPID" -f "$SFO_XML" "$PKG_STAGE/PARAM.SFO"

# 4) ICON0.PNG (320x176 recommended). Use SDK default if we have no custom icon yet.
ICON0="${PS3DEV}/bin/ICON0.PNG"
if [ -f "$PROJ/src/platform/ps3/ICON0.PNG" ]; then
    ICON0="$PROJ/src/platform/ps3/ICON0.PNG"
elif [ -f "$PROJ/docs/screenshots/main-menu.png" ]; then
    # Not the right size, but better than nothing for a test build
    ICON0="$PROJ/docs/screenshots/main-menu.png"
fi
if [ -f "$ICON0" ]; then
    cp "$ICON0" "$PKG_STAGE/ICON0.PNG"
    echo "==> ICON0 from $ICON0"
else
    echo "WARNING: no ICON0.PNG found — PKG may still install without icon"
fi

# 5) pkg.py
echo "==> pkg.py -> TestBuildFruitNinja1.0.pkg"
"$PKG" --contentid "$CONTENTID" "$PKG_STAGE/" "$OUT_PKG"

echo
echo "OK: $OUT_PKG"
echo
echo "Install on CFW PS3:"
echo "  1. Copy TestBuildFruitNinja1.0.pkg to USB root"
echo "  2. Package Manager -> Install Package Files"
echo
echo "RPCS3:"
echo "  File -> Install Packages/RAPS/EDAT -> select the .pkg"
