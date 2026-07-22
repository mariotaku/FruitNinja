# Asset Gallery

Interactive viewers for the original game's models and extracted textures. Regenerated during the web build pipeline.

## Build Flow

The gallery is built by the web build step (`tools/web/build-gallery.sh`) and output to `pages/gallery/` for deployment. The gallery consists of:

- **Model viewer** — `tools/web/dump_meshes.py` walks `FruitNinjaBada/Data/models/Fruit/*.mmd` and emits `models.json`. The HTML viewer (`index.html`) renders every mesh in an interactive WebGL canvas.
- **Texture gallery** — Textures are NOT statically pre-extracted; instead, the deployed game streams texture byte-ranges at runtime from the game's `.data` file (emscripten asset bundling), and the gallery displays extracted PNGs on demand during browsing.

See `docs/gallery/models/README.md` for detailed model viewer documentation.
