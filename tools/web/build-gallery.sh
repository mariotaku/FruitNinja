#!/usr/bin/env bash
# tools/web/build-gallery.sh -- generate the asset gallery (textures + models)
# from the FruitNinjaBada dump into OUT_DIR (default: pages/gallery).
#
# Not committed: the gallery is regenerated at build time from the game dump
# (Pages workflow downloads it to FruitNinjaBada/Data; locally, provide your
# own dump). Reuses the existing committed generators:
#   - tools/assets/convert_tex.py  (textures -> WebP + its own index.html)
#   - docs/gallery/models/dump_meshes.py + index.html (mesh viewer)
#
# Usage:
#   tools/web/build-gallery.sh [OUT_DIR]
#     OUT_DIR   default: pages/gallery
#   Env:
#     FN_DUMP            override the FruitNinjaBada dump dir (default: $PROJ/FruitNinjaBada)
#     FN_GALLERY_PYTHON   override the python interpreter to use (must have Pillow)
set -euo pipefail

PROJ="$(cd "$(dirname "$0")/../.." && pwd)"

OUT_DIR="${1:-$PROJ/pages/gallery}"
FN_DUMP="${FN_DUMP:-$PROJ/FruitNinjaBada}"

# ---------------------------------------------------------------------------
# Pick a python with Pillow available.
# ---------------------------------------------------------------------------
PYTHON=""
if [ -n "${FN_GALLERY_PYTHON:-}" ]; then
    if "$FN_GALLERY_PYTHON" -c "import PIL" >/dev/null 2>&1; then
        PYTHON="$FN_GALLERY_PYTHON"
    else
        echo "ERROR: FN_GALLERY_PYTHON=$FN_GALLERY_PYTHON does not have Pillow (PIL) importable." >&2
        exit 1
    fi
else
    for cand in python3 python; do
        if command -v "$cand" >/dev/null 2>&1 && "$cand" -c "import PIL" >/dev/null 2>&1; then
            PYTHON="$cand"
            break
        fi
    done
fi

if [ -z "$PYTHON" ]; then
    echo "ERROR: no python with Pillow (PIL) found." >&2
    echo "       gallery needs the FruitNinjaBada dump + Pillow; the dump is fetched" >&2
    echo "       at build time / provide your own locally. Install Pillow, e.g.:" >&2
    echo "         pip install Pillow   (or: apt-get install -y python3-pil)" >&2
    exit 1
fi

if [ ! -d "$FN_DUMP/Data" ]; then
    echo "ERROR: game dump not found at $FN_DUMP/Data" >&2
    echo "       gallery needs the FruitNinjaBada dump + Pillow; the dump is fetched" >&2
    echo "       at build time / provide your own locally (set FN_DUMP)." >&2
    exit 1
fi

echo "Using python: $PYTHON ($("$PYTHON" --version 2>&1))"
echo "Dump dir:     $FN_DUMP"
echo "Output dir:   $OUT_DIR"

# ---------------------------------------------------------------------------
# a. Output dirs
# ---------------------------------------------------------------------------
mkdir -p "$OUT_DIR/textures" "$OUT_DIR/models"

# ---------------------------------------------------------------------------
# b. Textures -> WebP + index.html (tools/assets/convert_tex.py's own index)
# ---------------------------------------------------------------------------
echo "=== Converting textures ==="
"$PYTHON" "$PROJ/tools/assets/convert_tex.py" "$FN_DUMP" "$OUT_DIR/textures"

# ---------------------------------------------------------------------------
# c. Models -> models.json + viewer index.html
# ---------------------------------------------------------------------------
echo "=== Dumping meshes ==="
"$PYTHON" "$PROJ/docs/gallery/models/dump_meshes.py"
cp "$PROJ/docs/gallery/models/index.html" "$OUT_DIR/models/index.html"
cp "$PROJ/docs/gallery/models/models.json" "$OUT_DIR/models/models.json"

# ---------------------------------------------------------------------------
# d. fruit_atlas.png -- decoded from Data/models/fruit/textures/fruit_atlas.tex
#    (not produced by any existing generator; the model viewer expects it
#    next to models.json).
# ---------------------------------------------------------------------------
echo "=== Decoding fruit_atlas.tex ==="
"$PYTHON" - "$PROJ" "$FN_DUMP" "$OUT_DIR/models/fruit_atlas.png" <<'PYEOF'
import sys
from pathlib import Path
from PIL import Image

repo_root, dump_dir, out_path = (Path(a) for a in sys.argv[1:4])

sys.path.insert(0, str(repo_root / "tools" / "lib"))
import tex_decoder

tex_path = dump_dir / "Data" / "models" / "fruit" / "textures" / "fruit_atlas.tex"
if not tex_path.is_file():
    print(f"ERROR: {tex_path} not found", file=sys.stderr)
    sys.exit(1)

decoded = tex_decoder.decode_tex(tex_path)
if decoded is None:
    print(f"ERROR: could not decode {tex_path}", file=sys.stderr)
    sys.exit(1)

width, height, rgba = decoded
img = Image.frombytes("RGBA", (width, height), bytes(rgba))
out_path.parent.mkdir(parents=True, exist_ok=True)
img.save(out_path, "PNG")
print(f"  OK: {tex_path.name} ({width}x{height}) -> {out_path}")
PYEOF

# ---------------------------------------------------------------------------
# e. Landing page
# ---------------------------------------------------------------------------
echo "=== Writing landing page ==="
cat > "$OUT_DIR/index.html" <<'HTMLEOF'
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>Fruit Ninja -- Asset Gallery</title>
<style>
body { font-family: sans-serif; background: #1a1a1a; color: #eee; margin: 0; padding: 40px 20px; }
h1 { color: #ff6600; margin-bottom: 4px; }
p.sub { color: #999; margin-top: 0; }
.cards { display: flex; flex-wrap: wrap; gap: 20px; margin-top: 30px; }
.card { background: #2a2a2a; border-radius: 10px; padding: 24px; width: 240px; text-decoration: none; color: #eee; transition: background 0.15s; }
.card:hover { background: #3a3a3a; }
.card h2 { margin: 0 0 8px; color: #ff8800; }
.card p { margin: 0; color: #aaa; font-size: 14px; }
</style>
</head>
<body>
<h1>Fruit Ninja -- Asset Gallery</h1>
<p class="sub">Assets decoded from the original game data, for reverse-engineering reference.</p>
<div class="cards">
<a class="card" href="./textures/"><h2>Textures</h2><p>Every .tex converted to lossless WebP.</p></a>
<a class="card" href="./models/"><h2>Models</h2><p>Interactive WebGL viewer for every .mmd mesh.</p></a>
</div>
</body>
</html>
HTMLEOF

echo
echo "Gallery written to: $OUT_DIR"
