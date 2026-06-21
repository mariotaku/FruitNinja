# GitHub Pages Deploy

The project publishes a three-section static site via GitHub Pages using the
**Actions artifact mechanism** (no `gh-pages` branch).

## One-time repository setup

1. Go to **Settings -> Pages**.
2. Under **Build and deployment -> Source**, choose **GitHub Actions**.
3. Save. No branch or folder selection is needed.

That is the only repository setting required. All content is uploaded by the
workflow as an artifact; GitHub deploys it automatically.

## Triggering a deploy

Navigate to **Actions -> Pages -> Run workflow** and click the green button.
The workflow is `workflow_dispatch` only -- it never runs automatically.
A deploy takes roughly 10-15 minutes (Emscripten build dominates).

The published URL will be:

    https://<org>.github.io/<repo>/

## Site layout

```
/               Landing page (web/index.html)
/game/          WebAssembly build (fruit-ninja.html renamed to index.html)
/models/        WebGL mesh viewer (docs/gallery/models/)
/textures/      Texture gallery  (docs/gallery/textures/)
```

## Asset source (archive.org)

The Bada game Data directory is NOT committed to this repository (it is
gitignored under `FruitNinjaBada/`). The workflow fetches it from the
Internet Archive at build time:

- **Collection**: `badaappsgamescollection`
  https://archive.org/details/badaappsgamescollection
- **File**: `FruitNinja.zip` (approx. 20 MB)
  https://archive.org/download/badaappsgamescollection/FruitNinja.zip
- **MD5**: `76844370f4c50b5f2bdc113b0664c4ce`
- **Game version**: v1.6.1. The package's `Bin/FruitNinja.exe` is MD5
  `ab60ff54a272abed1cbd403c5dcb7c55` (SHA1 `a4669367a91c0e115b7dddfc127cebc00fee3089`).
  The workflow **asserts this binary hash** (`EXPECTED_BIN_MD5`) so the assets
  can't drift to a different build without the build failing loudly.

This `FruitNinja.zip` (no space) has the package dirs (`Bin/ Data/ Res/ ...`) at
the zip root -- no `.bar` wrapper. The workflow unzips it and moves `Data/` to
`FruitNinjaBada/Data`, which is where CMake expects it. (Other copies in the
collection wrap the package in a `.bar`, itself a zip; the extract step handles
both.)

If the archive.org item changes or moves, update `ASSET_IDENTIFIER` and
`ASSET_FILENAME` near the top of `.github/workflows/pages.yml`.

## Copyright note

Publishing this site makes the original Halfbrick game assets (textures,
meshes, audio) publicly accessible. The reverse-engineering port is a research
project. Halfbrick Studios owns the original Fruit Ninja IP and assets.
Do not redistribute the assets beyond what the Internet Archive already makes
public.

## Building and previewing locally

### Full web build (requires Emscripten)

```sh
# One-time: install emsdk (https://emscripten.org/docs/getting_started/)
source /path/to/emsdk/emsdk_env.sh

emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web -j$(nproc)
```

Assets must be present at `FruitNinjaBada/Data` before the build (the
preload step embeds them into `fruit-ninja.data`).

### Assembling pages/

```sh
python tools/web/build_pages.py
```

The script is idempotent: it removes and recreates `pages/` each run.
It errors if `build-web/` or `build-web/fruit-ninja.html` is missing.

### Serving locally

```sh
cd pages
python -m http.server 8000
# open http://localhost:8000
```

The three sub-pages work independently; no server-side logic is required.
