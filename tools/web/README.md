# tools/web/

Emscripten web-build packaging + LAN dev serving, and the GitHub Pages deploy runbook.

## Scripts

- **`build_pages.py`** — assemble the GitHub Pages output dir (`pages/`) from `web/` + `build/web/`. `python tools/web/build_pages.py [--out <dir>]`. Idempotent (removes+recreates `pages/`); errors if `build/web/fruit-ninja.html` is missing.
- **`web-hash-assets.py`** — content-hash wasm/data/js/splash to defeat browser caching, rewriting all nested references (js data/wasm refs, html script/splash/build-id). `python3 web-hash-assets.py <build/web>`. Robust to incremental builds: emcc re-emits `fruit-ninja.{js,wasm,html}` every build but only re-emits `fruit-ninja.data` when packaged assets change, so each asset's hash is resolved from the fresh canonical file *or* (when unchanged/absent) from the already-hashed `fruit-ninja-<sha8>.ext` — the js/html refs are always rewritten to a valid hashed name, and the 49MB `.data` is never re-copied when unchanged. Ends with a self-check that the served js references no un-hashed asset (exits non-zero otherwise).
- **`web-serve.py`** — LAN dev server with content-hash-aware caching (no-store HTML, immutable hashed assets, `application/wasm`). `web-serve.py [--dir build/web] [--port 8000]`.
- **`rebuild-web.sh`** — incremental Emscripten rebuild when `src/` changed; run detached by the Claude Code Stop hook so the dev server stays current. The worker clears the executable's link outputs (`fruit-ninja.{wasm,js,html}`, NOT the `.data`) and builds the static lib target **before** the executable, to avoid two recurring failures: a parallel-`-j` race where `fruit-ninja.html` links before `libfruit-ninja-game.a`'s rule registers, and a corrupted/truncated wasm intermediate surviving across builds.

## Build & preview locally (requires Emscripten)

```sh
source /path/to/emsdk/emsdk_env.sh          # one-time: install emsdk
emcmake cmake -S . -B build/web -DCMAKE_BUILD_TYPE=Release
cmake --build build/web -j$(nproc)          # assets must be at FruitNinjaBada/Data first
python tools/web/build_pages.py
cd pages && python -m http.server 8000      # http://localhost:8000
```

The three sub-pages (landing / `game/` wasm / `models/` + `textures/` galleries) work independently; no server-side logic.

## GitHub Pages deploy

Static three-section site via the Actions artifact mechanism (no `gh-pages` branch).

- **One-time repo setup** (manual; the workflow can't do this itself): **Settings -> Pages -> Build and deployment -> Source -> GitHub Actions**.
- **Trigger**: **Actions -> Pages -> Run workflow**. `workflow_dispatch` only — never automatic. ~10-15 min (Emscripten build dominates). Publishes to `https://<org>.github.io/<repo>/`.
- **Assets**: `.github/workflows/pages.yml` fetches the Bada game data from the Internet Archive at build time and asserts the binary MD5 so assets can't silently drift. The asset identifiers + hashes live in the `pages.yml` header (single source of truth); update them there if the archive.org item moves.

## Copyright

Publishing the site exposes Halfbrick's original Fruit Ninja assets (textures, meshes, audio). This is a research/RE project; Halfbrick Studios owns the IP. Do not redistribute the assets beyond what the Internet Archive already makes public.
