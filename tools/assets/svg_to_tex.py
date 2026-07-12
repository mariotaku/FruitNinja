#!/usr/bin/env python3
"""tools/assets/svg_to_tex.py -- Rasterize checked-in widget SVGs to native
Tex1 .tex textures under FruitNinjaBada/Data/textures/.

Usage:
    python3 tools/assets/svg_to_tex.py [repo_root]

Source: assets/ui-widgets/<name>.svg (checked in).
Output: FruitNinjaBada/Data/textures/<name>.tex (Tex1, RGBA8888).

Tex1 format (see src/engine/asset/TextureFileFormat.cpp ReadTex1Format
@0x0022b324, tools/lib/tex_decoder.py): 12-byte header then raw pixels.
    byte[0] = wLog2 (width = 1<<wLog2)
    byte[1] = hLog2 (height = 1<<hLog2)
    byte[2] = format; this script always emits 0x01 = RGBA8888
    byte[3..11] = 0 padding
Pixel data is row-major, top row first -- no orientation flip. The reader
derives apparentWidth/apparentHeight purely from wLog2/hLog2; there is no
flip logic to replicate.

Rasterizer autodetect (first found wins): rsvg-convert, resvg, inkscape,
then the cairosvg Python module (self-provisioned via pip if missing). If
none is available at all, this is NON-FATAL: prints one warning line and
exits 0 -- widgets fall back to placeholder art (see SettingsScreen.cpp
LoadOrPlaceholder). This is the expected state on a plain Windows host with
no rasterizer installed.

Idempotent: an output .tex newer than its source .svg is skipped.

Importable: `from svg_to_tex import generate; generate(repo_root)` is the
reusable entry point (used by tools/web/stage-web-assets.py before its
texture-transcode walk, so the web build picks up freshly generated
textures automatically).
"""

import os
import shutil
import subprocess
import sys

# name -> (width, height), all POT per the widget's on-screen footprint.
MANIFEST = {
    "checked":      (128, 64),
    "unchecked":    (128, 64),
    "combo_bar":    (128, 32),
    "_dialog_box":  (128, 16),
    "slider_will":  (32, 32),
    "expand_arrow": (32, 32),
    "vbar":         (32, 128),
    "vslider":      (32, 64),
    "arrow":        (32, 32),
}

TEX_FORMAT_RGBA8888 = 0x01


def _log2_exact(n):
    """n must be an exact power of two; returns log2(n)."""
    assert n > 0 and (n & (n - 1)) == 0, "size must be a power of two: {}".format(n)
    return n.bit_length() - 1


def _ensure_pillow():
    try:
        import PIL  # noqa: F401
        return
    except ImportError:
        pass
    print("[svg_to_tex] Pillow not found, installing via pip")
    proc = subprocess.run([sys.executable, "-m", "pip", "install", "--quiet", "pillow"])
    if proc.returncode != 0:
        print("[svg_to_tex] ERROR: pip install pillow failed", file=sys.stderr)
        sys.exit(1)
    try:
        import PIL  # noqa: F401
    except ImportError:
        print("[svg_to_tex] ERROR: pillow installed but still not importable", file=sys.stderr)
        sys.exit(1)


def _rasterize_with_cli(tool, svg_path, png_path, w, h):
    if tool == "rsvg-convert":
        cmd = ["rsvg-convert", "-w", str(w), "-h", str(h), svg_path, "-o", png_path]
    elif tool == "resvg":
        cmd = ["resvg", "-w", str(w), "-h", str(h), svg_path, png_path]
    elif tool == "inkscape":
        cmd = ["inkscape", "--export-type=png", "-w", str(w), "-h", str(h),
               "-o", png_path, svg_path]
    else:
        raise ValueError("unknown rasterizer tool: {}".format(tool))
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0 or not os.path.isfile(png_path):
        raise RuntimeError("{} failed ({}): {}".format(
            tool, proc.returncode, proc.stderr.decode("utf-8", "replace")))


def _detect_rasterizer():
    """Returns a callable(svg_path, png_path, w, h) or None if nothing is
    available (neither a CLI tool on PATH nor the cairosvg module)."""
    for tool in ("rsvg-convert", "resvg", "inkscape"):
        if shutil.which(tool) is not None:
            return lambda svg, png, w, h, _t=tool: _rasterize_with_cli(_t, svg, png, w, h)

    try:
        import cairosvg  # noqa: F401
    except ImportError:
        return None

    def _cairosvg_rasterize(svg_path, png_path, w, h):
        import cairosvg
        cairosvg.svg2png(url=svg_path, write_to=png_path,
                          output_width=w, output_height=h)
    return _cairosvg_rasterize


def _png_to_rgba_bytes(png_path, w, h):
    _ensure_pillow()
    from PIL import Image
    img = Image.open(png_path).convert("RGBA")
    if img.size != (w, h):
        img = img.resize((w, h), Image.LANCZOS)
    return img.tobytes()


def _write_tex1(out_path, w, h, rgba_bytes):
    w_log2 = _log2_exact(w)
    h_log2 = _log2_exact(h)
    header = bytes([w_log2, h_log2, TEX_FORMAT_RGBA8888, 0, 0, 0, 0, 0, 0, 0, 0, 0])
    tmp_path = out_path + ".tmp"
    with open(tmp_path, "wb") as f:
        f.write(header)
        f.write(rgba_bytes)
    os.replace(tmp_path, out_path)


def generate(repo_root):
    """Rasterize every SVG in the manifest to a Tex1 .tex under
    FruitNinjaBada/Data/textures/. Non-fatal (prints a warning, returns) if
    no rasterizer is available at all."""
    svg_dir = os.path.join(repo_root, "assets", "ui-widgets")
    out_dir = os.path.join(repo_root, "FruitNinjaBada", "Data", "textures")

    rasterize = _detect_rasterizer()
    if rasterize is None:
        print("[svg_to_tex] WARNING: no SVG rasterizer found (tried rsvg-convert, "
              "resvg, inkscape, cairosvg) -- skipping widget texture generation; "
              "widgets will fall back to placeholder art")
        return

    os.makedirs(out_dir, exist_ok=True)

    for name, (w, h) in MANIFEST.items():
        svg_path = os.path.join(svg_dir, name + ".svg")
        tex_path = os.path.join(out_dir, name + ".tex")

        if not os.path.isfile(svg_path):
            print("[svg_to_tex] WARNING: missing source SVG {} -- skipping".format(svg_path))
            continue

        if os.path.isfile(tex_path) and os.path.getmtime(tex_path) >= os.path.getmtime(svg_path):
            print("[svg_to_tex] skip {}.tex (up to date)".format(name))
            continue

        png_path = tex_path + ".tmp.png"
        try:
            rasterize(svg_path, png_path, w, h)
            rgba = _png_to_rgba_bytes(png_path, w, h)
            _write_tex1(tex_path, w, h, rgba)
        finally:
            if os.path.isfile(png_path):
                os.remove(png_path)

        print("[svg_to_tex] generated {}.tex ({}x{})".format(name, w, h))


if __name__ == "__main__":
    if len(sys.argv) > 1:
        _repo_root = sys.argv[1]
    else:
        _repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    generate(_repo_root)
