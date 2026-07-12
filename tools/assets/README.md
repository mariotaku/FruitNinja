# tools/assets/

`.tex` texture asset conversion (FruitNinjaBada → webp gallery) and build-time asset staging.

- **`convert_tex.py`** — CLI: decode `.tex` files to `.webp` and emit an `index.html` gallery (PIL). `python tools/assets/convert_tex.py [IN] [OUT]`.
- **`stage-assets.py`** — unified build-phase asset staging for BOTH host and web builds (run by the `fn_asset_staging` CMake target, after `svg-to-webp.mjs`). Transcodes Tex1 textures -> WebP via Pillow, merges in pre-generated widget art; `--web` additionally transcodes sfx audio -> Ogg/Vorbis and subsets the CJK font. See `tools/web/README.md` for the full flag/behaviour breakdown.
- **`svg-to-webp.mjs`** — BUILD-TIME step (run by the `fn_asset_staging` CMake target, before `stage-assets.py`, whenever `node` is on PATH): rasterizes `assets/ui-widgets/*.svg` to WebP-encoded `.tex` files under `assets/ui-widgets/generated/` (a build artifact, gitignored — not tracked) via the `sharp` npm package (self-provisioned: runs `npm install` in this dir if missing). Non-fatal — any failure warns and exits 0, widgets fall back to placeholder art. `node tools/assets/svg-to-webp.mjs [repoRoot]`.

`convert_tex.py` and `stage-assets.py` share the single `.tex` decode core in [`tools/lib/tex_decoder.py`](../lib/tex_decoder.py) — the only place the Tex1 format is parsed. The gallery emits **lossless** webp for a pixel-faithful reference; `stage-assets.py` emits **lossy-90** webp (via Pillow) for size, except pre-generated widget art which is lossless (crisp vector UI art, not photographic).
