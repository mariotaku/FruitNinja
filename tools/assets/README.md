# tools/assets/

`.tex` texture asset conversion (FruitNinjaBada → webp gallery) and build-time asset staging.

- **`convert_tex.py`** — CLI: decode `.tex` files to `.webp` and emit an `index.html` gallery (PIL). `python tools/assets/convert_tex.py [IN] [OUT]`.
- **`stage-assets.py`** — unified build-phase asset staging for BOTH host and web builds (run by the `fn_asset_staging` CMake target, after `svg-to-webp.py`). Transcodes Tex1 textures -> WebP via Pillow, merges in pre-generated widget art; `--web` additionally transcodes sfx audio -> Ogg/Vorbis and subsets the CJK font. See `tools/web/README.md` for the full flag/behaviour breakdown.
- **`svg-to-webp.py`** — BUILD-TIME step (run by the `fn_asset_staging` CMake target, before `stage-assets.py`, on every platform): rasterizes `assets/ui-widgets/*.svg` to WebP-encoded `.tex` files under `assets/ui-widgets/generated/` (a build artifact, gitignored — not tracked) via `resvg-py` + Pillow (self-provisioned: `pip install`s either package if missing). Fatal by design — widget textures have no runtime placeholder fallback, so any failure exits non-zero and fails the build. Also emits a raw-RGBA `.rgba` sidecar per base widget for the Wii build (no WebP decoder there). `python3 tools/assets/svg-to-webp.py [repoRoot]`.

**Requirements**: `pip install resvg-py pillow` (resvg-py ships prebuilt wheels for common platforms incl. win_amd64; a platform without a prebuilt wheel needs a Rust toolchain to build from sdist — check before relying on this in CI).

`convert_tex.py` and `stage-assets.py` share the single `.tex` decode core in [`tools/lib/tex_decoder.py`](../lib/tex_decoder.py) — the only place the Tex1 format is parsed. The gallery emits **lossless** webp for a pixel-faithful reference; `stage-assets.py` emits **lossy-90** webp (via Pillow) for size, except pre-generated widget art which is lossless (crisp vector UI art, not photographic).
