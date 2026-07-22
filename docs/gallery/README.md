# Asset Gallery

Viewers for the original game's textures and models, used during RE to
cross-check the port's rendering.

**The textures and models themselves are NOT distributed** (Halfbrick
copyright) -- only the viewer tooling is committed. Regenerate them locally
from your own `FruitNinjaBada/Data` game dump:

- `docs/gallery/models/dump_meshes.py` -- walks `FruitNinjaBada/Data/models/Fruit/*.mmd`,
  writes `models.json` for `docs/gallery/models/index.html`.
- Textures under `docs/gallery/textures/Data/` are decoded `.tex` -> `.webp`
  copies of your dump's texture tree, browsed via `docs/gallery/textures/index.html`.

See `docs/gallery/models/README.md` for the model viewer's usage details.
