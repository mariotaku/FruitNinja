#!/usr/bin/env python3
"""Convert .tex files from FruitNinjaBada to WebP and generate index.html.

Usage:
    python tools/assets/convert_tex.py [INPUT_DIR] [OUTPUT_DIR]

Defaults:
    INPUT_DIR  = FruitNinjaBada  (relative to project root)
    OUTPUT_DIR = tmp/textures    (relative to project root)
"""

import argparse
import sys
from pathlib import Path
from PIL import Image

PROJECT_ROOT = Path(__file__).resolve().parent.parent

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "lib"))
import tex_decoder


def convert_tex(tex_path: Path, bada_dir: Path, out_dir: Path) -> tuple[int, int, int]:
    """Convert a .tex file to WebP. Returns (width, height, format)."""
    decoded = tex_decoder.decode_tex(tex_path)
    if decoded is None:
        data = tex_path.read_bytes()
        size = len(data)
        raise ValueError(f"Not a decodable Tex1 (size={size} bytes)")
    width, height, rgba = decoded
    fmt = tex_path.read_bytes()[2]

    img = Image.frombytes("RGBA", (width, height), bytes(rgba))

    # Output path: keep hierarchy relative to input dir
    rel = tex_path.relative_to(bada_dir)
    out_path = out_dir / rel.with_suffix(".webp")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    # Gallery uses lossless webp for pixel-faithful reference; the web build
    # (stage-web-assets.py) uses lossy-90 via ffmpeg for size; both share
    # tex_decoder now.
    img.save(out_path, "webp", lossless=True, quality=100, method=6)

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
                webp_rel = str(Path(r["rel"]).with_suffix(".webp")).replace("\\", "/")
                fname = Path(r["rel"]).stem
                fmt_str = tex_decoder.TEX_FORMAT_NAMES.get(r["fmt"], f"0x{r['fmt']:02X}")
                html += f'<div class="card"><img src="{webp_rel}" alt="{fname}"><div class="name">{fname}</div><div class="info">{r["w"]}x{r["h"]} {fmt_str}</div></div>\n'
        html += "</div>\n"

    html += "</body>\n</html>\n"

    (out_dir / "index.html").write_text(html, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description="Convert .tex textures to WebP")
    parser.add_argument("input_dir", nargs="?", default=str(PROJECT_ROOT / "FruitNinjaBada"),
                        help="Directory containing .tex files (default: FruitNinjaBada)")
    parser.add_argument("output_dir", nargs="?", default=str(PROJECT_ROOT / "tmp" / "textures"),
                        help="Output directory for WebP images (default: tmp/textures)")
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
            print(f"  OK: {rel} ({w}x{h}, {tex_decoder.TEX_FORMAT_NAMES.get(fmt, f'0x{fmt:02X}')})")
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
