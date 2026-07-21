#!/bin/bash
# Deploy a BUILT Wii homebrew package into Dolphin's SD card image (WiiSD.raw)
# via mtools. This is the ONLY step that touches Dolphin/AppData -- it is NOT
# part of the build (see tools/wii/build.sh, which stops at cmake --install /
# cpack). Run this separately, after a build+install, whenever you want to
# test the current package in Dolphin.
#
# Usage (from the devkitPro MSYS2 shell, so mtools/mingw64 DLLs resolve):
#   bash tools/wii/deploy-dolphin.sh [package_dir]
#
#   package_dir   defaults to build/wii/dist/apps/fruitninja (the output of
#                 `cmake --install build/wii --prefix build/wii/dist`).
#                 Override to point at any assembled apps/fruitninja dir
#                 (e.g. an extracted fruit-ninja-wii.zip).
#
# Env overrides:
#   FN_DOLPHIN_SD    Path to Dolphin's WiiSD.raw image. Defaults to
#                    "/c/Users/$USER/AppData/Roaming/Dolphin Emulator/Load/WiiSD.raw"
#                    ($USER = the Windows login; msys2 $HOME is /home/<user>, so
#                    it is NOT used here). Deploy is a no-op if absent.
#   WII_FONT_LANGS   Space-separated list of prebaked font languages to
#                    deploy to the SD, or "all". Defaults to
#                    "japanese english_us" -- see the SD-size note below.
set -eo pipefail

PROJ="$(cd "$(dirname "$0")/../.." && pwd)"
PKG_DIR="${1:-$PROJ/build/wii/dist/apps/fruitninja}"

if [ ! -f "$PKG_DIR/boot.dol" ]; then
    echo "ERROR: $PKG_DIR/boot.dol not found." >&2
    echo "Build + install first:" >&2
    echo "  cmake --build build/wii -j8" >&2
    echo "  cmake --install build/wii --prefix build/wii/dist" >&2
    exit 1
fi

RAW="${FN_DOLPHIN_SD:-/c/Users/$USER/AppData/Roaming/Dolphin Emulator/Load/WiiSD.raw}"
MT=/mingw64/bin
if [ ! -f "$RAW" ] || [ ! -x "$MT/mcopy" ]; then
    echo "Deploy skipped (no $RAW or mtools at $MT)"
    exit 0
fi
export PATH="$MT:$PATH"

# The prebaked fonts are ~102MB for all 16 languages, which OVERFLOWS Dolphin's
# 128MB WiiSD.raw test image (Data is ~50MB besides fonts). The game only ever
# loads ONE language (CONF_GetLanguage), so for Dolphin testing we deploy just
# the active test language(s) + english fallback. On REAL hardware (GBs of SD)
# deploy everything: run with WII_FONT_LANGS=all (needs a bigger WiiSD.raw).
WII_FONT_LANGS="${WII_FONT_LANGS:-japanese english_us}"

# mtools needs /mingw64/bin on PATH for its DLLs. Each op is `timeout`-wrapped
# so a stuck mtools process (image lock contention) can't wedge the deploy.
timeout 20  mmd      -i "$RAW" ::/apps ::/apps/fruitninja 2>/dev/null || true
timeout 120 mdeltree -i "$RAW" ::/apps/fruitninja/Data 2>/dev/null || true
timeout 20  mmd      -i "$RAW" ::/apps/fruitninja/Data 2>/dev/null || true

# Non-font Data FIRST (textures/sfx/xml/strings) so the game always boots even
# if fonts don't all fit -- unbaked glyphs just fall back to stb at runtime.
for item in "$PKG_DIR"/Data/*; do
    [ "$(basename "$item")" = "fonts" ] && continue
    timeout 300 mcopy -i "$RAW" -s -Q -o -n "$item" ::/apps/fruitninja/Data/ 2>/dev/null || true
done

# Bitmap .fnt fonts (fruit_ninja_numbers.fnt etc.) live at fonts/ top level and
# are NOT prebaked -- the score counter + classic number displays load these, so
# they MUST ship even though the loop above skips the whole fonts/ dir to handle
# prebaked per-language. Copy every top-level fonts/ entry EXCEPT the prebaked dir.
timeout 20 mmd -i "$RAW" ::/apps/fruitninja/Data/fonts 2>/dev/null || true
for f in "$PKG_DIR"/Data/fonts/*; do
    [ "$(basename "$f")" = "prebaked" ] && continue
    timeout 120 mcopy -i "$RAW" -s -Q -o -n "$f" ::/apps/fruitninja/Data/fonts/ 2>/dev/null \
        || echo "WARN: fonts/$(basename "$f") deploy failed"
done

# Prebaked fonts: full set, or just the selected langs (default) to fit the SD.
timeout 20 mmd -i "$RAW" ::/apps/fruitninja/Data/fonts/prebaked 2>/dev/null || true
if [ "$WII_FONT_LANGS" = "all" ]; then
    timeout 900 mcopy -i "$RAW" -s -Q -o -n "$PKG_DIR/Data/fonts/prebaked" ::/apps/fruitninja/Data/fonts/ 2>/dev/null \
        || echo "WARN: full font set overflowed WiiSD.raw -- grow the image or drop WII_FONT_LANGS=all"
else
    for lang in $WII_FONT_LANGS; do
        [ -d "$PKG_DIR/Data/fonts/prebaked/$lang" ] || continue
        timeout 120 mcopy -i "$RAW" -s -Q -o -n "$PKG_DIR/Data/fonts/prebaked/$lang" ::/apps/fruitninja/Data/fonts/prebaked/ 2>/dev/null \
            || echo "WARN: fonts/$lang deploy failed"
    done
fi

timeout 30 mcopy -i "$RAW" -Q -o -n "$PKG_DIR/boot.dol" ::/apps/fruitninja/boot.dol 2>/dev/null || true
timeout 20 mcopy -i "$RAW" -Q -o -n "$PKG_DIR/icon.png" ::/apps/fruitninja/icon.png 2>/dev/null || echo "WARN: icon.png deploy failed"
timeout 20 mcopy -i "$RAW" -Q -o -n "$PKG_DIR/meta.xml" ::/apps/fruitninja/meta.xml 2>/dev/null || echo "WARN: meta.xml deploy failed"
echo "Deployed Data + boot.dol + icon.png + meta.xml + fonts[$WII_FONT_LANGS] -> WiiSD.raw (mtools)"
