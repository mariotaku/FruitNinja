#!/bin/bash
# Empaqueta un DLC content-pack como .pkg para PS3.
#
# Usage:
#   bash tools/ps3/package-dlc.sh skittles
#   bash tools/ps3/package-dlc.sh puss
#   bash tools/ps3/package-dlc.sh skittles /ruta/custom/Data
#
# Lee packs/<id>/dlc.json y packs/<id>/Data/ (o el path opcional).
# Output: build/ps3/<pkg_name del manifest>

set -eo pipefail

PROJ="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="$PROJ/build/ps3"
PACK_ID="${1:-}"

if [ -z "$PACK_ID" ]; then
    echo "Usage: bash tools/ps3/package-dlc.sh <skittles|puss|id> [DataPath]" >&2
    echo "Packs disponibles:" >&2
    ls -1 "$PROJ/packs" 2>/dev/null || true
    exit 1
fi

PACK_DIR="$PROJ/packs/$PACK_ID"
MANIFEST="$PACK_DIR/dlc.json"

if [ ! -f "$MANIFEST" ]; then
    echo "ERROR: no existe $MANIFEST" >&2
    exit 1
fi

# Parseo minimal sin jq (id, title, appid, pkg_name)
json_get() {
    local key="$1"
    sed -n "s/.*\"$key\"[[:space:]]*:[[:space:]]*\"\([^\"]*\)\".*/\1/p" "$MANIFEST" | head -1
}

TITLE=$(json_get title)
APPID=$(json_get appid)
PKG_NAME=$(json_get pkg_name)
VERSION=$(json_get version)
VERSION=${VERSION:-1.0}

if [ -z "$APPID" ] || [ -z "$PKG_NAME" ]; then
    echo "ERROR: dlc.json incompleto (appid / pkg_name)" >&2
    exit 1
fi

CONTENTID="UP0001-${APPID}_00-0000000000000000"
OUT_PKG="$BUILD_DIR/$PKG_NAME"
PKG_STAGE="$BUILD_DIR/pkg-dlc-$PACK_ID"

if [ -n "${2:-}" ]; then
    DATA_SRC="$2"
elif [ -d "$PACK_DIR/Data" ] && [ "$(find "$PACK_DIR/Data" -type f ! -name '.gitkeep' | head -1)" ]; then
    DATA_SRC="$PACK_DIR/Data"
else
    echo "ERROR: packs/$PACK_ID/Data/ está vacío." >&2
    echo "Meté el dump (Android/Bada reskin) ahí, o pasá un path:" >&2
    echo "  bash tools/ps3/package-dlc.sh $PACK_ID /ruta/a/Data" >&2
    exit 1
fi

find_tool() {
    local name="$1"
    if command -v "$name" >/dev/null 2>&1; then command -v "$name"; return; fi
    for d in "$PS3DEV/bin" "$PSL1GHT/host/bin" "$PSL1GHT/tools"; do
        [ -x "$d/$name" ] && echo "$d/$name" && return
    done
    echo ""
}

SFO=$(find_tool sfo.py)
PKG=$(find_tool pkg.py)
if [ -z "$SFO" ] || [ -z "$PKG" ]; then
    echo "ERROR: faltan sfo.py / pkg.py (PS3DEV + PSL1GHT)" >&2
    exit 1
fi

echo "==> DLC '$PACK_ID'  title=$TITLE  appid=$APPID"
echo "    data: $DATA_SRC"
echo "    out:  $OUT_PKG"

rm -rf "$PKG_STAGE"
mkdir -p "$PKG_STAGE/USRDIR/Data"
cp -a "$DATA_SRC"/. "$PKG_STAGE/USRDIR/Data/"
cp "$MANIFEST" "$PKG_STAGE/USRDIR/dlc.json"

SFO_XML="$BUILD_DIR/sfo-dlc-$PACK_ID.xml"
mkdir -p "$BUILD_DIR"
cat > "$SFO_XML" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<sfo>
  <value name="APP_VER" type="string">${VERSION}</value>
  <value name="ATTRIBUTE" type="int">0</value>
  <value name="CATEGORY" type="string">HG</value>
  <value name="LICENSE" type="string">Fan content pack - supply your own assets</value>
  <value name="RESOLUTION" type="int">1</value>
  <value name="TITLE" type="string">${TITLE}</value>
  <value name="TITLE_ID" type="string">${APPID}</value>
  <value name="VERSION" type="string">${VERSION}</value>
</sfo>
EOF

"$SFO" --title "$TITLE" --appid "$APPID" -f "$SFO_XML" "$PKG_STAGE/PARAM.SFO"

ICON0="${PS3DEV}/bin/ICON0.PNG"
[ -f "$PACK_DIR/ICON0.PNG" ] && ICON0="$PACK_DIR/ICON0.PNG"
[ -f "$ICON0" ] && cp "$ICON0" "$PKG_STAGE/ICON0.PNG"

"$PKG" --contentid "$CONTENTID" "$PKG_STAGE/" "$OUT_PKG"

echo
echo "OK: $OUT_PKG"
echo "Instalá después de assets base + TestBuild. El juego leerá dlc.json + Data/."
