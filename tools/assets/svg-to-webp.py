#!/usr/bin/env python3
"""
tools/assets/svg-to-webp.py -- BUILD-TIME rasterizer: converts the
checked-in widget SVGs to WebP-encoded .tex textures under
assets/ui-widgets/generated/. Invoked by the fn_asset_staging CMake
target (see CMakeLists.txt), before tools/assets/stage-assets.py, on
every platform (Windows/CLion included) -- replaces the Node/sharp
svg-to-webp.mjs tool (dropped the Node dependency entirely). Also
replaces the older OFFLINE Python/cairosvg tool (svg_to_tex.py), which
couldn't rasterize on a plain Windows host; cairosvg was re-evaluated
for this and rejected too (no bundled libcairo on Windows, and it
mis-renders settings_button.svg's SVG filter chain). resvg-py is a
prebuilt statically-linked Rust wheel (same rendering engine class as
sharp/resvg-node) with correct SVG-filter support and no system-lib
dependency.

Usage:
    python3 svg-to-webp.py [repoRoot]

Source: assets/ui-widgets/<name>.svg (checked in).
Output: assets/ui-widgets/generated/<name>.tex (WebP bytes, .tex
extension -- NOT git-tracked, this is a build artifact regenerated
every configure/build; stage-assets.py copies these verbatim into the
staged textures dir for both host and web builds).

Lossless WebP: this is crisp vector UI art (flat colors, sharp edges),
so lossless keeps it pixel-exact. resvg renders directly at the target
pixel size (no separate supersample+downscale pass needed -- unlike
sharp's density-based rasterization, resvg's width/height params
render the vector geometry natively at that resolution with its own
anti-aliasing).

Wii sidecar: for each BASE (non-hd_) widget a raw-RGBA sidecar
<name>.rgba is also written next to the WebP ("RRAW" magic + u16le
width/height + width*height*4 RGBA8 bytes, little-endian -- consumed
only on the x86 build host). The Wii build has no WebP decoder, so
stage-assets.py --wii reads this sidecar with pure Python stdlib and
re-encodes it as a pre-tiled GXTX .tex. hd_ renders get no sidecar
(Wii HD assets are disabled, FN_ENABLE_HD_ASSETS=OFF).

Self-provisioning: if the `resvg_py` or `PIL` modules aren't installed
yet, this runs `pip install` for the missing package(s) once (same
trick as the other Python asset tools' _ensure_pillow) and retries the
import.

Fatal by design: the widget textures have no runtime fallback (SettingsScreen
/ the render tests load them directly via LoadLocalisedTexture, no
placeholder substitution), so ANY failure (resvg_py/Pillow missing and
pip install failing, no network, rasterize error) prints a clear ASCII
error and exits NON-ZERO to fail the build loudly instead of silently
shipping blank widgets.

Idempotent: an output .tex newer than its source .svg is skipped.
"""

import io
import os
import struct
import subprocess
import sys

# name -> (width, height), all POT per the widget's on-screen footprint.
# Mirrors svg-to-webp.mjs's MANIFEST exactly -- see that file's per-entry
# comments (list_fade / expand_arrow non-POT rationale, box.tex sharing,
# etc.) for the full derivation; not re-duplicated here.
MANIFEST = {
    "checked": (128, 64),
    "unchecked": (128, 64),
    "box": (64, 40),
    "list_fade": (64, 6),
    "list_item": (128, 40),
    "slider_will": (32, 32),
    "check": (32, 32),
    "caret": (32, 32),
    "expand_arrow": (32, 38),
    "vbar": (32, 128),
    "vslider": (32, 64),
    "arrow": (32, 32),
    "settings_button": (64, 64),
}


def _ensure_deps():
    global resvg_py, Image
    try:
        import resvg_py  # noqa: F401
        from PIL import Image  # noqa: F401
        return
    except ImportError:
        pass
    print("[svg-to-webp] resvg_py or Pillow not found, running pip install")
    subprocess.check_call([sys.executable, "-m", "pip", "install", "resvg-py", "Pillow"])
    import resvg_py  # noqa: F401
    from PIL import Image  # noqa: F401


_ensure_deps()
import resvg_py  # noqa: E402
from PIL import Image  # noqa: E402


def _rasterize(svg_path, w, h):
    """Render svg_path to an RGBA PIL Image at exactly (w x h) pixels."""
    png_bytes = resvg_py.svg_to_bytes(svg_path=svg_path, width=w, height=h)
    img = Image.open(io.BytesIO(png_bytes)).convert("RGBA")
    if img.size != (w, h):
        # resvg preserves aspect ratio within the given box by default; the
        # widget SVGs' viewBox already matches the target aspect (see the
        # MANIFEST/viewBox comment in each .svg), so this should not fire.
        # fit exactly the same way sharp's {fit:"fill"} did, as a safety net.
        img = img.resize((w, h), Image.LANCZOS)
    return img


def render(svg_path, out_path, w, h):
    """Render svg_path -> out_path (.tex, WebP-encoded), lossless, at (w x h).
    Returns True if rendered, False if skipped (output newer than source)."""
    if os.path.exists(out_path) and os.path.getmtime(out_path) >= os.path.getmtime(svg_path):
        return False
    img = _rasterize(svg_path, w, h)
    tmp_path = out_path + ".tmp.webp"
    img.save(tmp_path, "WEBP", lossless=True, quality=100)
    os.replace(tmp_path, out_path)
    print("[svg-to-webp] generated %s (%dx%d)" % (os.path.basename(out_path), w, h))
    return True


def render_raw(svg_path, out_path, w, h):
    """Write the raw-RGBA sidecar out_path (.rgba) for the BASE render only
    (the Wii staging path, stage-assets.py --wii, reads it with pure Python
    stdlib -- no WebP decoder on the Wii, no Pillow in its msys2 Python).
    Layout (little-endian; consumed only on the x86 build host):
      bytes[0..3]  magic "RRAW"
      bytes[4..5]  u16le width
      bytes[6..7]  u16le height
      bytes[8..]   width*height*4 raw RGBA8 bytes
    Same idempotency as render(): skipped when newer than the source .svg."""
    if os.path.exists(out_path) and os.path.getmtime(out_path) >= os.path.getmtime(svg_path):
        return False
    img = _rasterize(svg_path, w, h)
    data = img.tobytes("raw", "RGBA")
    if len(data) != w * h * 4:
        raise ValueError("raw render of %s is %d bytes (want %d)" %
                          (os.path.basename(out_path), len(data), w * h * 4))
    header = struct.pack("<4sHH", b"RRAW", w, h)
    tmp_path = out_path + ".tmp.rgba"
    with open(tmp_path, "wb") as f:
        f.write(header)
        f.write(data)
    os.replace(tmp_path, out_path)
    print("[svg-to-webp] generated %s (%dx%d raw RGBA sidecar)" % (os.path.basename(out_path), w, h))
    return True


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = sys.argv[1] if len(sys.argv) > 1 else os.path.abspath(os.path.join(script_dir, "..", ".."))

    svg_dir = os.path.join(repo_root, "assets", "ui-widgets")
    out_dir = os.path.join(svg_dir, "generated")
    os.makedirs(out_dir, exist_ok=True)

    generated = 0
    skipped = 0

    for name, (w, h) in MANIFEST.items():
        svg_path = os.path.join(svg_dir, name + ".svg")
        if not os.path.exists(svg_path):
            print("[svg-to-webp] WARNING: missing source SVG %s -- skipping" % svg_path)
            continue

        # Nominal-res .tex (baseline / fallback) plus an HD "hd_" sibling at
        # 2x the pixel dimensions. The texture loader (TextureManager::
        # BuildHdPath/Load) silently prefers the hd_ file and halves its
        # reported apparent size, so widgets draw at the SAME on-screen
        # footprint but sample 2x the detail -- crisper vector UI.
        if render(svg_path, os.path.join(out_dir, name + ".tex"), w, h):
            generated += 1
        else:
            skipped += 1
        if render_raw(svg_path, os.path.join(out_dir, name + ".rgba"), w, h):  # Wii sidecar: base only, no hd_
            generated += 1
        else:
            skipped += 1
        if render(svg_path, os.path.join(out_dir, "hd_" + name + ".tex"), w * 2, h * 2):
            generated += 1
        else:
            skipped += 1

    print("[svg-to-webp] %d generated, %d up to date" % (generated, skipped))


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print("[svg-to-webp] ERROR: rasterization failed (%s)" % e)
        print("[svg-to-webp] widget textures are required (no runtime fallback) -- "
              "ensure pip can install 'resvg-py' and 'Pillow' (tools/assets/), then rebuild")
        sys.exit(1)
