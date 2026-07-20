#!/usr/bin/env python3
"""Generate the Homebrew Channel icon (apps/fruitninja/icon.png), 128x48.

Composes the game's FRUIT + NINJA logo lettering (from the decoded gallery
webps -- no network, no .tex decode needed) in a diagonal cascade over a dark
maroon vertical gradient. Reproducible: run with a Python that has Pillow.

    python tools/wii/hbc/make-icon.py

Output: tools/wii/hbc/icon.png (deployed to sd:/apps/fruitninja/icon.png by the
Wii build/deploy so the Homebrew Channel shows a proper entry).
"""
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
GAL = os.path.join(ROOT, "docs", "gallery", "textures", "Data", "textures")

W, H = 1024, 384          # compose at 8x, downscale to 128x48 for crisp edges
TOP, BOT = (60, 12, 12), (12, 4, 4)   # maroon gradient
FRUIT_H = 210             # FRUIT lettering height (of 384)
NINJA_SCALE = 0.70        # NINJA 30% smaller than FRUIT
SPREAD = 150              # extra horizontal offset (widens the cascade)
OVERLAP = 58              # vertical overlap between FRUIT and NINJA


def load(name):
    im = Image.open(os.path.join(GAL, name + ".webp")).convert("RGBA")
    return im.crop(im.getbbox())


def scale_h(im, h):
    w = max(1, round(im.width * h / im.height))
    return im.resize((w, h), Image.LANCZOS)


def main():
    fruit, ninja = load("hd_fruit_text"), load("hd_ninja_text")
    # vertical gradient background
    col = Image.new("RGB", (1, H))
    for y in range(H):
        t = y / (H - 1)
        col.putpixel((0, y), tuple(int(TOP[i] + (BOT[i] - TOP[i]) * t) for i in range(3)))
    icon = col.resize((W, H)).convert("RGBA")
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
    dst = os.path.join(HERE, "icon.png")
    out.save(dst)
    print("wrote", dst, out.size)


if __name__ == "__main__":
    main()
