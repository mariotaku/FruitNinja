#!/usr/bin/env python3
"""Convert .tex files from FruitNinjaBada to PNG and generate index.html.

Usage:
    python tools/assets/convert_tex.py [INPUT_DIR] [OUTPUT_DIR]

Defaults:
    INPUT_DIR  = FruitNinjaBada  (relative to project root)
    OUTPUT_DIR = tmp/textures    (relative to project root)
"""

import argparse
import struct
import sys
from pathlib import Path
from PIL import Image

PROJECT_ROOT = Path(__file__).resolve().parent.parent

FMT_NAMES = {
    0x00: "RGB888",
    0x01: "RGBA8888",
    0x0F: "RGBA5551",
    0x10: "RGBA4444",
    0x11: "RGB565",
}


def convert_tex(tex_path: Path, bada_dir: Path, out_dir: Path) -> tuple[int, int, int]:
    """Convert a .tex file to PNG. Returns (width, height, format)."""
    data = tex_path.read_bytes()
    if len(data) < 12:
        raise ValueError(f"File too small: {len(data)} bytes")

    width_log2 = data[0]
    height_log2 = data[1]
    fmt = data[2]
    width = 1 << width_log2
    height = 1 << height_log2

    pixel_data = data[12:]

    if fmt == 0x10:  # RGBA4444
        img = Image.new("RGBA", (width, height))
        pixels = []
        for i in range(width * height):
            pixel = struct.unpack_from("<H", pixel_data, i * 2)[0]
            r = ((pixel >> 12) & 0xF) * 17
            g = ((pixel >> 8) & 0xF) * 17
            b = ((pixel >> 4) & 0xF) * 17
            a = ((pixel >> 0) & 0xF) * 17
            pixels.append((r, g, b, a))
        img.putdata(pixels)
    elif fmt == 0x11:  # RGB565
        img = Image.new("RGB", (width, height))
        pixels = []
        for i in range(width * height):
            pixel = struct.unpack_from("<H", pixel_data, i * 2)[0]
            r = ((pixel >> 11) & 0x1F) * 255 // 31
            g = ((pixel >> 5) & 0x3F) * 255 // 63
            b = ((pixel >> 0) & 0x1F) * 255 // 31
            pixels.append((r, g, b))
        img.putdata(pixels)
    elif fmt == 0x0F:  # RGBA5551
        img = Image.new("RGBA", (width, height))
        pixels = []
        for i in range(width * height):
            pixel = struct.unpack_from("<H", pixel_data, i * 2)[0]
            r = ((pixel >> 11) & 0x1F) * 255 // 31
            g = ((pixel >> 6) & 0x1F) * 255 // 31
            b = ((pixel >> 1) & 0x1F) * 255 // 31
            a = (pixel & 0x1) * 255
            pixels.append((r, g, b, a))
        img.putdata(pixels)
    elif fmt == 0x01:  # RGBA8888
        img = Image.new("RGBA", (width, height))
        pixels = []
        for i in range(width * height):
            off = i * 4
            r, g, b, a = pixel_data[off], pixel_data[off+1], pixel_data[off+2], pixel_data[off+3]
            pixels.append((r, g, b, a))
        img.putdata(pixels)
    elif fmt == 0x00:  # RGB888
        img = Image.new("RGB", (width, height))
        pixels = []
        for i in range(width * height):
            off = i * 3
            r, g, b = pixel_data[off], pixel_data[off+1], pixel_data[off+2]
            pixels.append((r, g, b))
        img.putdata(pixels)
    else:
        raise ValueError(f"Unsupported format: 0x{fmt:02X}")

    # Output path: keep hierarchy relative to input dir
    rel = tex_path.relative_to(bada_dir)
    out_path = out_dir / rel.with_suffix(".png")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    img.save(out_path, "PNG")

    return width, height, fmt


def generate_index(results: list[dict], out_dir: Path):
    """Generate index.html for previewing all converted textures."""
    results.sort(key=lambda r: r["rel"])

    # Group by directory
    groups = {}
    for r in results:
        d = str(Path(r["rel"]).parent)
        if d == ".":
            d = "(root)"
        groups.setdefault(d, []).append(r)

    html = """<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>FruitNinjaBada Textures</title>
<style>
body { font-family: sans-serif; background: #1a1a1a; color: #eee; margin: 20px; }
h1 { color: #ff6600; }
h2 { color: #cc5500; margin-top: 2em; border-bottom: 1px solid #444; padding-bottom: 4px; }
.grid { display: flex; flex-wrap: wrap; gap: 16px; }
.card { background: #2a2a2a; border-radius: 8px; padding: 8px; text-align: center; max-width: 280px; }
.card img { max-width: 256px; max-height: 256px; image-rendering: pixelated; background: repeating-conic-gradient(#333 0% 25%, #444 0% 50%) 50% / 16px 16px; }
.card .name { font-size: 12px; margin-top: 4px; word-break: break-all; }
.card .info { font-size: 11px; color: #888; }
.error { color: #f66; }
.summary { color: #aaa; margin-bottom: 1em; }
</style>
</head>
<body>
<h1>FruitNinjaBada Textures</h1>
"""
    total_ok = sum(1 for r in results if not r.get("error"))
    total_err = sum(1 for r in results if r.get("error"))
    html += f'<p class="summary">{total_ok} converted, {total_err} errors, {len(results)} total</p>\n'

    for group_name in sorted(groups.keys()):
        items = groups[group_name]
        html += f"<h2>{group_name}</h2>\n<div class='grid'>\n"
        for r in items:
            if r.get("error"):
                html += f'<div class="card"><p class="error">{r["rel"]}<br>{r["error"]}</p></div>\n'
            else:
                png_rel = str(Path(r["rel"]).with_suffix(".png")).replace("\\", "/")
                fname = Path(r["rel"]).stem
                fmt_str = FMT_NAMES.get(r["fmt"], f"0x{r['fmt']:02X}")
                html += f'<div class="card"><img src="{png_rel}" alt="{fname}"><div class="name">{fname}</div><div class="info">{r["w"]}x{r["h"]} {fmt_str}</div></div>\n'
        html += "</div>\n"

    html += "</body>\n</html>\n"

    (out_dir / "index.html").write_text(html, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description="Convert .tex textures to PNG")
    parser.add_argument("input_dir", nargs="?", default=str(PROJECT_ROOT / "FruitNinjaBada"),
                        help="Directory containing .tex files (default: FruitNinjaBada)")
    parser.add_argument("output_dir", nargs="?", default=str(PROJECT_ROOT / "tmp" / "textures"),
                        help="Output directory for PNGs (default: tmp/textures)")
    args = parser.parse_args()

    bada_dir = Path(args.input_dir).resolve()
    out_dir = Path(args.output_dir).resolve()

    tex_files = sorted(bada_dir.rglob("*.tex"))
    print(f"Found {len(tex_files)} .tex files in {bada_dir}")

    results = []
    errors = 0
    for tex_path in tex_files:
        rel = str(tex_path.relative_to(bada_dir))
        try:
            w, h, fmt = convert_tex(tex_path, bada_dir, out_dir)
            results.append({"rel": rel, "w": w, "h": h, "fmt": fmt})
            print(f"  OK: {rel} ({w}x{h}, {FMT_NAMES.get(fmt, f'0x{fmt:02X}')})")
        except Exception as e:
            results.append({"rel": rel, "error": str(e)})
            print(f"  ERR: {rel}: {e}")
            errors += 1

    generate_index(results, out_dir)
    print(f"\nDone: {len(results) - errors}/{len(results)} converted")
    print(f"Output: {out_dir}")
    print(f"Index: {out_dir / 'index.html'}")


if __name__ == "__main__":
    main()
