#!/usr/bin/env bash
# tools/web/build-gallery.sh -- generate the asset gallery (textures + models)
# into OUT_DIR (default: pages/gallery).
#
# Not committed: the gallery is regenerated at build time.
#   - Textures: TRUE .data reuse, zero duplicated files. The deployed game's
#     Data (including the webp-in-.tex textures) is packed into emscripten's
#     content-hashed fruit-ninja-<hash>.data via `emcc --preload-file`
#     (CMakeLists.txt: "SHELL:--preload-file ${FN_STAGING_DATA_DIR}@/..."; see
#     tools/web/build.sh). file_packager inlines the {filename,start,end}
#     offset table as the object literal argument to the `loadPackage(...)`
#     call in the generated fruit-ninja-<hash>.js -- there is no separate
#     `.metadata` JSON (that's only emitted with --separate-metadata, which
#     this project's packaging does not pass). web-hash-assets.py only
#     renames files; it doesn't touch the JS body, so the loadPackage object
#     literal is intact and safe to regex-extract.
#
#     This script parses that table out of the ALREADY-BUILT hashed .js
#     (no game-build change needed -- see option (b) in the task) and emits
#     a filtered `textures/manifest.json` = {data: "<hashed .data filename>",
#     textures: [{name, start, end}, ...]} for entries whose path contains
#     "/textures/". No .tex/.webp file is ever copied into pages/ -- the
#     browser-side viewer (textures/index.html) range-fetches straight out
#     of the deployed .data at runtime.
#   - Models: unchanged -- still sourced from the raw FruitNinjaBada dump via
#     the existing generators (tools/lib/tex_decoder.py +
#     docs/gallery/models/dump_meshes.py + index.html), since the .data
#     doesn't carry gallery-ready mesh geometry or the fruit_atlas decode.
#
# Requires the web build to have already run AND been assembled into pages/
# (tools/web/build.sh then tools/web/build_pages.py), since the Pages
# workflow runs build_pages.py before this script and this script reads the
# final hashed fruit-ninja-<hash>.{js,data} names from there. Falls back to
# build/web/ directly for local testing without a pages/ assembly step.
#
# Usage:
#   tools/web/build-gallery.sh [OUT_DIR]
#     OUT_DIR   default: pages/gallery
#   Env:
#     FN_DUMP           override the FruitNinjaBada dump dir (default: $PROJ/FruitNinjaBada)
#                       (models step only)
#     FN_DEPLOYED_DIR   override the dir holding the deployed hashed
#                       fruit-ninja-<hash>.{js,data} (default: $PROJ/pages if
#                       it contains a hashed .data, else $PROJ/build/web)
#     FN_GALLERY_PYTHON  override the python interpreter to use (must have Pillow;
#                       models step only)
set -euo pipefail

PROJ="$(cd "$(dirname "$0")/../.." && pwd)"
# Shared failure helpers: strict mode + an ERR trap that names the failing line,
# command and exit status (this script's own preflight failures were already
# specific; the trap covers the UNEXPECTED ones). See tools/web/config.sh.
. "$(dirname "$0")/config.sh"
fn_web_strict

# This script takes ONE positional arg (OUT_DIR) and no flags. Anything
# flag-shaped or extra is a hard error -- silently treating "--release" as an
# output directory would create a directory of that name and "succeed".
if [ "$#" -gt 1 ]; then
    FN_WEB_FATAL_CODE=2 fn_web_fatal "build-gallery.sh takes at most one argument (OUT_DIR), got $#: $*" \
        "Usage: tools/web/build-gallery.sh [OUT_DIR]   (default: pages/gallery)"
fi
case "${1:-}" in
    -*) FN_WEB_FATAL_CODE=2 fn_web_fatal "build-gallery.sh: unknown flag: $1" \
            "This script accepts no flags, only an optional OUT_DIR path." \
            "Usage: tools/web/build-gallery.sh [OUT_DIR]   (default: pages/gallery)" ;;
esac

OUT_DIR="${1:-$PROJ/pages/gallery}"
FN_DUMP="${FN_DUMP:-$PROJ/FruitNinjaBada}"

# ---------------------------------------------------------------------------
# Locate the deployed hashed fruit-ninja-<hash>.{js,data} pair. Prefer
# pages/ (the actual deploy artifact assembled by build_pages.py, which the
# Pages workflow runs immediately before this script) so the manifest's
# data-file reference matches what actually ships; fall back to build/web/
# for standalone local runs.
# ---------------------------------------------------------------------------
if [ -n "${FN_DEPLOYED_DIR:-}" ]; then
    DEPLOYED_DIR="$FN_DEPLOYED_DIR"
elif ls "$PROJ/pages"/fruit-ninja-*.data >/dev/null 2>&1; then
    DEPLOYED_DIR="$PROJ/pages"
else
    DEPLOYED_DIR="$PROJ/build/web"
fi

DATA_FILE="$(ls "$DEPLOYED_DIR"/fruit-ninja-*.data 2>/dev/null | grep -v '\.debug\.data$' | head -1 || true)"
JS_FILE="$(ls "$DEPLOYED_DIR"/fruit-ninja-*.js 2>/dev/null | head -1 || true)"

if [ -z "$DATA_FILE" ] || [ -z "$JS_FILE" ]; then
    echo "ERROR: could not find a hashed fruit-ninja-<hash>.{js,data} pair in $DEPLOYED_DIR" >&2
    echo "       the gallery textures manifest is generated from the web build's" >&2
    echo "       generated loadPackage() offset table (in fruit-ninja-<hash>.js), and" >&2
    echo "       needs the paired fruit-ninja-<hash>.data filename to reference." >&2
    echo "       Run the web build first (tools/web/build.sh) and/or" >&2
    echo "       tools/web/build_pages.py, or set FN_DEPLOYED_DIR." >&2
    exit 1
fi
DATA_FILENAME="$(basename "$DATA_FILE")"
JS_FILENAME="$(basename "$JS_FILE")"

# ---------------------------------------------------------------------------
# Pick a python with Pillow available (needed for the models step only: mesh
# dump + fruit_atlas.tex decode).
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
    echo "       the models step needs the FruitNinjaBada dump + Pillow; the dump is" >&2
    echo "       fetched at build time / provide your own locally. Install Pillow, e.g.:" >&2
    echo "         pip install Pillow   (or: apt-get install -y python3-pil)" >&2
    exit 1
fi

if [ ! -d "$FN_DUMP/Data" ]; then
    echo "ERROR: game dump not found at $FN_DUMP/Data" >&2
    echo "       the models step needs the FruitNinjaBada dump + Pillow; the dump is" >&2
    echo "       fetched at build time / provide your own locally (set FN_DUMP)." >&2
    exit 1
fi

echo "Using python:  $PYTHON ($("$PYTHON" --version 2>&1))"
echo "Dump dir:      $FN_DUMP"
echo "Deployed dir:  $DEPLOYED_DIR  ($JS_FILENAME / $DATA_FILENAME)"
echo "Output dir:    $OUT_DIR"

# ---------------------------------------------------------------------------
# a. Output dirs
# ---------------------------------------------------------------------------
mkdir -p "$OUT_DIR/textures" "$OUT_DIR/models"

# ---------------------------------------------------------------------------
# b. Textures manifest.json -- TRUE .data reuse, no file copies. Parse the
#    loadPackage({files:[{filename,start,end},...]}) table out of the built
#    JS and filter to texture entries (any path containing "/textures/").
#    The viewer (textures/index.html) range-fetches these byte spans
#    straight out of the deployed .data at runtime.
# ---------------------------------------------------------------------------
echo "=== Extracting texture manifest from $JS_FILENAME ==="
"$PYTHON" - "$JS_FILE" "$DATA_FILENAME" "$OUT_DIR/textures/manifest.json" <<'PYEOF'
import json
import re
import sys

js_path, data_filename, out_path = sys.argv[1:4]

with open(js_path, encoding="utf-8") as f:
    js = f.read()

marker = "loadPackage({files:["
start = js.find(marker)
if start == -1:
    print("ERROR: loadPackage({files:[...]}) table not found in " + js_path, file=sys.stderr)
    print("       (emscripten's file_packager metadata format may have changed --", file=sys.stderr)
    print("       see the option (a)/--separate-metadata note in this script's header)", file=sys.stderr)
    sys.exit(1)

# Bracket-count from the '[' to find the matching ']' (the array can contain
# nested structures in principle; plain counting is robust and dependency-free).
arr_start = js.index("[", start)
depth = 0
i = arr_start
while True:
    c = js[i]
    if c == "[":
        depth += 1
    elif c == "]":
        depth -= 1
        if depth == 0:
            break
    i += 1
arr_text = js[arr_start:i + 1]

entry_re = re.compile(r'\{filename:"(.*?)",start:(\d+),end:(\d+)\}')
entries = entry_re.findall(arr_text)
if not entries:
    print("ERROR: parsed loadPackage array but found zero {filename,start,end} entries", file=sys.stderr)
    sys.exit(1)

textures = []
for filename, start_off, end_off in entries:
    if "/textures/" not in filename:
        continue
    # filename is like "/FruitNinjaBada/Data/models/fruit/textures/bomb_explod.tex"
    # -- keep the path relative to Data/ as the display name.
    marker2 = "/Data/"
    idx = filename.find(marker2)
    name = filename[idx + len(marker2):] if idx != -1 else filename
    textures.append({"name": name, "start": int(start_off), "end": int(end_off)})

textures.sort(key=lambda t: t["name"])

manifest = {"data": data_filename, "textures": textures}
with open(out_path, "w", encoding="utf-8") as f:
    json.dump(manifest, f)

print(f"  {len(entries)} total packaged files, {len(textures)} texture entries -> {out_path}")
PYEOF

# ---------------------------------------------------------------------------
# c. Textures viewer -- self-contained JS, lazy range-fetch straight out of
#    the deployed .data (see the header comment; no texture files written).
# ---------------------------------------------------------------------------
echo "=== Writing textures viewer ==="
cat > "$OUT_DIR/textures/index.html" <<'HTMLEOF'
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>Fruit Ninja -- Textures</title>
<style>
body { font-family: sans-serif; background: #1a1a1a; color: #eee; margin: 20px; }
h1 { color: #ff6600; }
.summary { color: #aaa; margin-bottom: 1em; }
.grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(160px, 1fr)); gap: 16px; }
figure { background: #2a2a2a; border-radius: 8px; padding: 8px; margin: 0; text-align: center; min-height: 140px; }
figure img { max-width: 100%; max-height: 200px; image-rendering: pixelated; background: repeating-conic-gradient(#333 0% 25%, #444 0% 50%) 50% / 16px 16px; }
figcaption { font-size: 12px; margin-top: 4px; word-break: break-all; color: #ccc; }
.placeholder { color: #666; font-size: 11px; }
.error { color: #f66; font-size: 11px; }
</style>
</head>
<body>
<h1>Fruit Ninja -- Textures</h1>
<p class="summary" id="summary">Loading manifest...</p>
<div class="grid" id="grid"></div>
<script>
(function () {
  "use strict";

  var grid = document.getElementById("grid");
  var summaryEl = document.getElementById("summary");

  var MAX_CONCURRENT_RANGE_FETCHES = 8;

  // Range support is probed ONCE up front (bytes=0-0) before any tile loads,
  // so every tile agrees on the same strategy -- this is what prevents the
  // fallback race: without a shared up-front probe, many tiles could each
  // notice "Range unsupported" independently and each kick off their own
  // full .data download concurrently.
  var rangeSupported = null; // null = not yet probed
  var rangeProbePromise = null;

  // No-Range fallback: a SINGLE SHARED promise for the whole .data. Every
  // tile awaits the same promise (one download total) and slices its own
  // [start,end) out of the shared ArrayBuffer.
  var wholeDataPromise = null;
  function getWholeData(dataUrl) {
    if (!wholeDataPromise) {
      wholeDataPromise = fetch(dataUrl).then(function (res) {
        if (!res.ok) throw new Error("full .data fetch failed: " + res.status);
        return res.arrayBuffer();
      });
    }
    return wholeDataPromise;
  }

  function probeRangeSupport(dataUrl) {
    if (!rangeProbePromise) {
      rangeProbePromise = fetch(dataUrl, { headers: { Range: "bytes=0-0" } })
        .then(function (res) {
          rangeSupported = (res.status === 206);
          if (!rangeSupported) {
            // Server ignored Range and sent the full body right here (200)
            // -- reuse THIS response as the whole-.data cache instead of
            // firing a second full fetch from getWholeData(). Exactly one
            // full download on the no-Range path.
            wholeDataPromise = res.arrayBuffer();
            return wholeDataPromise;
          }
          // 206: tiny 1-byte probe body, just drain/ignore it.
          return res.arrayBuffer().catch(function () {});
        })
        .catch(function () {
          rangeSupported = false;
        });
    }
    return rangeProbePromise;
  }

  // Small concurrency-capped queue for the 206 Range path, so a fast scroll
  // can't open hundreds of simultaneous connections.
  var rangeQueue = [];
  var rangeInFlight = 0;
  function runRangeQueue() {
    while (rangeInFlight < MAX_CONCURRENT_RANGE_FETCHES && rangeQueue.length > 0) {
      var job = rangeQueue.shift();
      rangeInFlight++;
      job().finally(function () {
        rangeInFlight--;
        runRangeQueue();
      });
    }
  }
  function enqueueRangeFetch(fn) {
    return new Promise(function (resolve, reject) {
      rangeQueue.push(function () {
        return fn().then(resolve, reject);
      });
      runRangeQueue();
    });
  }

  function fetchRangeSlice(dataUrl, start, end) {
    // end is exclusive (matches the manifest / emscripten convention);
    // HTTP Range end-byte is inclusive.
    return fetch(dataUrl, { headers: { Range: "bytes=" + start + "-" + (end - 1) } })
      .then(function (res) {
        if (res.status === 206) return res.arrayBuffer();
        if (res.status === 200) {
          // Server ignored Range after all (probe said otherwise) -- slice
          // locally rather than failing the tile.
          return res.arrayBuffer().then(function (buf) { return buf.slice(start, end); });
        }
        throw new Error("range fetch failed: " + res.status);
      });
  }

  function fetchSlice(dataUrl, start, end) {
    return probeRangeSupport(dataUrl).then(function () {
      if (rangeSupported) {
        return enqueueRangeFetch(function () { return fetchRangeSlice(dataUrl, start, end); });
      }
      return getWholeData(dataUrl).then(function (buf) {
        return buf.slice(start, end);
      });
    });
  }

  function makeTile(tex) {
    var figure = document.createElement("figure");
    var placeholder = document.createElement("div");
    placeholder.className = "placeholder";
    placeholder.textContent = "...";
    figure.appendChild(placeholder);

    var caption = document.createElement("figcaption");
    caption.textContent = tex.name;
    figure.appendChild(caption);

    figure._tex = tex;
    figure._placeholder = placeholder;
    figure._state = "pending"; // pending -> loading -> loaded | failed
    return figure;
  }

  function loadTile(figure, dataUrl, isRetry) {
    if (figure._state === "loading" || figure._state === "loaded") return;
    figure._state = "loading";
    var tex = figure._tex;
    fetchSlice(dataUrl, tex.start, tex.end)
      .then(function (buf) {
        var blob = new Blob([buf], { type: "image/webp" });
        var url = URL.createObjectURL(blob);
        var img = document.createElement("img");
        img.loading = "lazy";
        img.alt = tex.name;
        img.src = url;
        figure.replaceChild(img, figure._placeholder);
        figure._objectUrl = url;
        figure._state = "loaded";
      })
      .catch(function (err) {
        console.error("texture load failed:", tex.name, err);
        if (!isRetry) {
          // Retry once before giving up.
          figure._state = "pending";
          loadTile(figure, dataUrl, true);
          return;
        }
        figure._state = "failed";
        figure._placeholder.className = "error";
        figure._placeholder.textContent = "error";
      });
  }

  fetch("manifest.json")
    .then(function (res) {
      if (!res.ok) throw new Error("manifest.json fetch failed: " + res.status);
      return res.json();
    })
    .then(function (manifest) {
      // manifest.data is the bare hashed filename (e.g.
      // "fruit-ninja-<hash>.data"); it lives at the site root, one level up
      // from gallery/textures/.
      var dataUrl = "../../" + manifest.data;
      summaryEl.textContent = manifest.textures.length + " textures (range-fetched live from " + manifest.data + ")";

      var allFigures = [];

      var observer = new IntersectionObserver(function (entries) {
        entries.forEach(function (entry) {
          if (entry.isIntersecting) {
            observer.unobserve(entry.target);
            loadTile(entry.target, dataUrl);
          }
        });
      }, { rootMargin: "400px" });

      // Revoke object URLs once a tile scrolls far away, to bound memory on
      // a long scroll session (optional, best-effort).
      var revokeObserver = new IntersectionObserver(function (entries) {
        entries.forEach(function (entry) {
          if (!entry.isIntersecting && entry.target._objectUrl) {
            var img = entry.target.querySelector("img");
            if (img) {
              URL.revokeObjectURL(entry.target._objectUrl);
              entry.target._objectUrl = null;
            }
          }
        });
      }, { rootMargin: "1000px" });

      manifest.textures.forEach(function (tex) {
        var figure = makeTile(tex);
        grid.appendChild(figure);
        observer.observe(figure);
        revokeObserver.observe(figure);
        allFigures.push(figure);
      });

      // Safety-net sweep: IntersectionObserver can miss tiles in some
      // browsers/layouts (observer created before final layout, a very
      // short last row, rapid/one-way programmatic scroll leaving mid-page
      // tiles behind). On scroll/resize-idle (and once shortly after
      // initial load), force-load EVERY still-pending tile, not just ones
      // near the current viewport -- a fast one-way scroll would otherwise
      // strand mid-page tiles until the user happens to scroll back over
      // them. This is cheap: on the no-Range path the whole .data is
      // already cached in memory (getWholeData/wholeDataPromise), so each
      // straggler is just a local slice; on the 206/Range path each goes
      // through the same MAX_CONCURRENT_RANGE_FETCHES=8 queue as normal
      // tile loads, so it can't open hundreds of connections at once.
      function sweepStragglers() {
        allFigures.forEach(function (figure) {
          if (figure._state !== "pending") return;
          observer.unobserve(figure);
          loadTile(figure, dataUrl);
        });
      }
      var sweepTimer = null;
      function scheduleSweep() {
        clearTimeout(sweepTimer);
        sweepTimer = setTimeout(sweepStragglers, 200);
      }
      window.addEventListener("scroll", scheduleSweep, { passive: true });
      window.addEventListener("resize", scheduleSweep);
      scheduleSweep();
    })
    .catch(function (err) {
      summaryEl.textContent = "Failed to load manifest.json: " + err;
      console.error(err);
    });
})();
</script>
</body>
</html>
HTMLEOF

# ---------------------------------------------------------------------------
# d. Models -> models.json + viewer index.html (unchanged: dump from the raw
#    FruitNinjaBada dump -- staged Data has no gallery-ready mesh geometry).
# ---------------------------------------------------------------------------
echo "=== Dumping meshes ==="
"$PYTHON" "$PROJ/docs/gallery/models/dump_meshes.py"
cp "$PROJ/docs/gallery/models/index.html" "$OUT_DIR/models/index.html"
cp "$PROJ/docs/gallery/models/models.json" "$OUT_DIR/models/models.json"

# ---------------------------------------------------------------------------
# e. fruit_atlas.png -- decoded from Data/models/fruit/textures/fruit_atlas.tex
#    (not produced by any existing generator; the model viewer expects it
#    next to models.json). Unchanged: still needs the raw dump + tex_decoder.
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
# f. Landing page
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
<a class="card" href="./textures/"><h2>Textures</h2><p>Every texture, streamed live from the game's own .data bundle.</p></a>
<a class="card" href="./models/"><h2>Models</h2><p>Interactive WebGL viewer for every .mmd mesh.</p></a>
</div>
</body>
</html>
HTMLEOF

echo
echo "Gallery written to: $OUT_DIR"
