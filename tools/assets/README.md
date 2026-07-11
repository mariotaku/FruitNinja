# tools/assets/

`.tex` texture asset conversion (FruitNinjaBada → webp gallery).

- **`convert_tex.py`** — CLI: decode `.tex` files to `.webp` and emit an `index.html` gallery (PIL). `python tools/assets/convert_tex.py [IN] [OUT]`.

Both this and the web build's asset staging (`tools/web/stage-web-assets.py`) share the single `.tex` decode core in [`tools/lib/tex_decoder.py`](../lib/tex_decoder.py) — the only place the Tex1 format is parsed. This gallery emits **lossless** webp for a pixel-faithful reference; the web build emits **lossy-90** webp (via ffmpeg) for size.
