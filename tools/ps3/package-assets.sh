#!/bin/bash
# Build assets-only PS3 package: FruitNinjaNECESARRYAssets.PKG
#
# Sources game data from:
#   1) argument path, or
#   2) $FN_ANDROID_DATA, or
#   3) FruitNinjaBada/Data (Bada dump — same Tex1/.wav layout the port expects)
#
# Android: dump/extract the game assets yourself (APK/OBB) into a folder that
# looks like Data/ (textures, sfx, etc.). Point this script at that folder.
# The port still expects Bada-style Tex1 names; if Android files differ you
# may need a convert pass first (tools/assets/).
#
# Usage:
#   bash tools/ps3/package-assets.sh
#   bash tools/ps3/package-assets.sh /path/to/AndroidExtract/Data
#   FN_ANDROID_DATA=~/fn-android/Data bash tools/ps3/package-assets.sh
#
# Output:
#   build/ps3/FruitNinjaNECESARRYAssets.PKG

set -eo pipefail

PROJ="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="$PROJ/build/ps3"
PKG_STAGE="$BUILD_DIR/pkg-assets-stage"
OUT_PKG="$BUILD_DIR/FruitNinjaNECESARRYAssets.PKG"

TITLE="Fruit Ninja Necessary Assets"
APPID="FNASSET01"
CONTENTID="UP0001-${APPID}_00-0000000000000000"
VERSION="01.00"

# Resolve data root
if [ -n "${1:-}" ]; then
    DATA_SRC="$1"
elif [ -n "${FN_ANDROID_DATA:-}" ]; then
    DATA_SRC="$FN_ANDROID_DATA"
elif [ -d "$PROJ/FruitNinjaBada/Data" ]; then
    DATA_SRC="$PROJ/FruitNinjaBada/Data"
elif [ -d "$PROJ/Data" ]; then
    DATA_SRC="$PROJ/Data"
else
    echo "ERROR: no game data found." >&2
    echo "Provide a path to Data/ (Bada dump or Android extract):" >&2
    echo "  bash tools/ps3/package-assets.sh /path/to/Data" >&2
    echo "  or: export FN_ANDROID_DATA=/path/to/Data" >&2
    echo "Expected layout example:" >&2
    echo "  Data/textures/  Data/sfx/  Data/fonts/  ..." >&2
    exit 1
fi

if [ ! -d "$DATA_SRC" ]; then
    echo "ERROR: data path is not a directory: $DATA_SRC" >&2
    exit 1
fi

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

SFO=$(find_tool sfo.py)
PKG=$(find_tool pkg.py)

if [ -z "$SFO" ] || [ -z "$PKG" ]; then
    echo "ERROR: need sfo.py and pkg.py on PATH (PS3DEV/PSL1GHT)." >&2
    exit 1
fi

echo "==> Assets PKG from: $DATA_SRC"
echo "    OUTPUT: $OUT_PKG"

rm -rf "$PKG_STAGE"
mkdir -p "$PKG_STAGE/USRDIR/Data"

# Copy data tree into USRDIR/Data (game will look under /dev_hdd0/game/FNASSET01/USRDIR/Data
# or you can merge into the main app's USRDIR after install).
echo "==> Copying data..."
cp -a "$DATA_SRC"/. "$PKG_STAGE/USRDIR/Data/"

# PARAM.SFO (content package, no EBOOT required for pure data — some CFW
# installers still want a minimal structure; we only ship Data + SFO + icon).
SFO_XML="${PS3DEV}/bin/sfo.xml"
if [ ! -f "$SFO_XML" ]; then
    SFO_XML="$BUILD_DIR/sfo-assets.xml"
    mkdir -p "$BUILD_DIR"
    cat > "$SFO_XML" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<sfo>
  <value name="APP_VER" type="string">${VERSION}</value>
  <value name="ATTRIBUTE" type="int">0</value>
  <value name="CATEGORY" type="string">HG</value>
  <value name="LICENSE" type="string">Game data package - you must own the original game</value>
  <value name="RESOLUTION" type="int">1</value>
  <value name="TITLE" type="string">${TITLE}</value>
  <value name="TITLE_ID" type="string">${APPID}</value>
  <value name="VERSION" type="string">${VERSION}</value>
</sfo>
EOF
fi

echo "==> sfo.py -> PARAM.SFO"
"$SFO" --title "$TITLE" --appid "$APPID" -f "$SFO_XML" "$PKG_STAGE/PARAM.SFO"

ICON0="${PS3DEV}/bin/ICON0.PNG"
if [ -f "$PROJ/src/platform/ps3/ICON0.PNG" ]; then
    ICON0="$PROJ/src/platform/ps3/ICON0.PNG"
fi
if [ -f "$ICON0" ]; then
    cp "$ICON0" "$PKG_STAGE/ICON0.PNG"
fi

echo "==> pkg.py -> FruitNinjaNECESARRYAssets.PKG"
"$PKG" --contentid "$CONTENTID" "$PKG_STAGE/" "$OUT_PKG"

echo
echo "OK: $OUT_PKG"
echo
echo "Install order on PS3/RPCS3:"
echo "  1) FruitNinjaNECESARRYAssets.PKG   (data)"
echo "  2) TestBuildFruitNinja1.0.pkg      (executable)"
echo
echo "Data lands under game title $APPID / USRDIR/Data"
echo "Point FN_DATA_DIR at that path, or copy Data into the main app USRDIR."
