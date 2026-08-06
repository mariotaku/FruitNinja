#!/usr/bin/env python3
"""Generate the Homebrew Channel icon (apps/fruitninja/icon.png), 128x48.

Composes the game's FRUIT + NINJA logo lettering in a diagonal cascade over the
in-game dojo wood board (gb_game). Decodes the source textures directly from the
LOCAL FruitNinjaBada game dump (FruitNinjaBada/Data/textures/gb_game.tex,
hd_fruit_text.tex, hd_ninja_text.tex) via tools/lib/tex_decoder.py -- the dump is
gitignored and not distributed (Halfbrick copyright; see docs/gallery/README.md),
and this script's own output (icon.png) is likewise gitignored, so nothing
copyrighted is ever committed. Bring your own dump to regenerate the icon.

    python tools/wii/hbc/make-icon.py                 # -> tools/wii/hbc/icon.png
    python tools/wii/hbc/make-icon.py --out <path>    # -> anywhere (CMake uses
                                                      #    ${CMAKE_BINARY_DIR}/hbc)

Output: a 128x48 PNG, deployed to sd:/apps/fruitninja/icon.png by the Wii
build/deploy so the Homebrew Channel shows a proper entry. The default path
(tools/wii/hbc/icon.png) is gitignored; the Wii CMake build passes --out so the
icon lands in the build tree and a fresh checkout needs no committed .png.
"""
import argparse
import os
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
BADA_TEXTURES = os.path.join(ROOT, "FruitNinjaBada", "Data", "textures")

sys.path.insert(0, os.path.join(ROOT, "tools", "lib"))
import tex_decoder

W, H = 1024, 384          # compose at 8x, downscale to 128x48 for crisp edges
WOOD_TEX = "gb_game"      # in-game dojo wood board (vertical planks + slashes)
FRUIT_H = 231             # FRUIT lettering height (of 384)
NINJA_SCALE = 0.70        # NINJA 30% smaller than FRUIT
SPREAD = 165              # extra horizontal offset (widens the cascade)
OVERLAP = 64              # vertical overlap between FRUIT and NINJA


def load(name, crop_bbox=True):
    tex_path = os.path.join(BADA_TEXTURES, name + ".tex")
    if not os.path.exists(tex_path):
        sys.exit(
            "Wii icon requires the local FruitNinjaBada game dump at "
            "FruitNinjaBada/Data/textures/ (missing: " + tex_path + "); "
            "not distributed -- see docs/gallery/README.md"
        )
    decoded = tex_decoder.decode_tex(tex_path)
    if decoded is None:
        sys.exit("Wii icon: " + tex_path + " did not decode as a known Tex1 format")
    width, height, rgba = decoded
    im = Image.frombytes("RGBA", (width, height), bytes(rgba))
    return im.crop(im.getbbox()) if crop_bbox else im


def scale_h(im, h):
    w = max(1, round(im.width * h / im.height))
    return im.resize((w, h), Image.LANCZOS)


def wood_bg():
    # Center horizontal band of the dojo wood board at the icon aspect,
    # scaled to fill. Native brightness -- no gamma/level adjustment.
    bg = load(WOOD_TEX, crop_bbox=False).convert("RGB")
    bw, bh = bg.size
    band_h = int(bw / (W / H))
    y0 = (bh - band_h) // 2
    band = bg.crop((0, y0, bw, y0 + band_h)).resize((W, H), Image.LANCZOS)
    return band.convert("RGBA")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-o", "--out", default=os.path.join(HERE, "icon.png"),
                    help="output PNG path (default: tools/wii/hbc/icon.png)")
    args = ap.parse_args()

    fruit, ninja = load("hd_fruit_text"), load("hd_ninja_text")
    icon = wood_bg()
    # diagonal cascade: FRUIT upper-left, NINJA lower-right
    f = scale_h(fruit, FRUIT_H)
    n = scale_h(ninja, int(FRUIT_H * NINJA_SCALE))
    gw = max(f.width, n.width) + SPREAD
    gh = f.height + n.height - OVERLAP
    gx = (W - gw) // 2
    gy = (H - gh) // 2 - 4
    icon.alpha_composite(f, (gx, gy))
    icon.alpha_composite(n, (gx + gw - n.width, gy + f.height - OVERLAP))
    out = icon.resize((128, 48), Image.LANCZOS).convert("RGB")
    dst = os.path.abspath(args.out)
    out_dir = os.path.dirname(dst)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    out.save(dst)
    print("wrote", dst, out.size)


if __name__ == "__main__":
    main()
