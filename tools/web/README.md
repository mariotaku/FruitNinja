# tools/web/

Emscripten web-build packaging + LAN dev serving, and the GitHub Pages deploy runbook.

## Scripts

- **`build.sh`** — the single in-container build entrypoint; local dev (`rebuild-web.sh`) and CI (`.github/workflows/pages.yml`) both run it inside the pinned `emscripten/emsdk:6.0.0` image (repo mounted at `/src`). Configures `build/web` if needed, pre-clears stale link outputs (never the `.data`), builds the static lib **before** the executable (parallel-`-j` link-race workaround), then verifies the emcc-emitted `fruit-ninja.html` + the main wasm with up to 3 link retries, and finally renames the entry HTML to `index.html` (served at the directory root; internal `fruit-ninja[-<sha8>].js/.wasm/.data` names are untouched). Flags: no flags = reuse the existing configure (preserves a local `FN_WEB_DEBUG` tree); `--release` = force Release + `FN_WEB_DEBUG=OFF`; `--debug` = Release + `FN_WEB_DEBUG=ON` (separate-DWARF debug, unhashed outputs); `--reconfigure` = force re-configure. CMake stays the packaging source of truth (shell.html, `--preload-file`, webp copies, `web-hash-assets.py`).
- **`build_pages.py`** — assemble the GitHub Pages output dir (`pages/`) from `web/` + `build/web/`. `python tools/web/build_pages.py [--out <dir>]`. Idempotent (removes+recreates `pages/`); errors if `build/web/index.html` is missing.
- **`web-hash-assets.py`** — content-hash wasm/data/js/splash to defeat browser caching, rewriting all nested references (js data/wasm refs, html script/splash/build-id). `python3 web-hash-assets.py <build/web>`. Robust to incremental builds: emcc re-emits `fruit-ninja.{js,wasm,html}` every build but only re-emits `fruit-ninja.data` when packaged assets change, so each asset's hash is resolved from the fresh canonical file *or* (when unchanged/absent) from the already-hashed `fruit-ninja-<sha8>.ext` — the js/html refs are always rewritten to a valid hashed name, and the 49MB `.data` is never re-copied when unchanged. Ends with a self-check that the served js references no un-hashed asset (exits non-zero otherwise).
- **`web-serve.py`** — LAN dev server with content-hash-aware caching (no-store HTML, immutable hashed assets, `application/wasm`). `web-serve.py [--dir build/web] [--port 8000]`.
- **`rebuild-web.sh`** — auto-rebuild dispatcher for the Claude Code Stop hook: gates on `src/` changes vs the newest wasm, then runs `build.sh` in Docker, detached (lock + `tmp/web-rebuild.log`), so the dev server stays current.

## Build & preview locally (Docker, same as CI)

```sh
# assets must be at FruitNinjaBada/Data first
docker run --rm -v "$(pwd):/src" -w /src emscripten/emsdk:6.0.0 \
    bash /src/tools/web/build.sh --release
python tools/web/build_pages.py
cd pages && python -m http.server 8000      # http://localhost:8000
```

(On MSYS2/Windows pass a native path for the mount and set `MSYS_NO_PATHCONV=1` — `rebuild-web.sh` does both automatically.)

The three sub-pages (landing / `game/` wasm / `models/` + `textures/` galleries) work independently; no server-side logic.

## GitHub Pages deploy

Static three-section site via the Actions artifact mechanism (no `gh-pages` branch).

- **One-time repo setup** (manual; the workflow can't do this itself): **Settings -> Pages -> Build and deployment -> Source -> GitHub Actions**.
- **Trigger**: **Actions -> Pages -> Run workflow**. `workflow_dispatch` only — never automatic. ~10-15 min (Emscripten build dominates). Publishes to `https://<org>.github.io/<repo>/`.
- **Assets**: `.github/workflows/pages.yml` fetches the Bada game data from the Internet Archive at build time and asserts the binary MD5 so assets can't silently drift. The asset identifiers + hashes live in the `pages.yml` header (single source of truth); update them there if the archive.org item moves.

## Copyright

Publishing the site exposes Halfbrick's original Fruit Ninja assets (textures, meshes, audio). This is a research/RE project; Halfbrick Studios owns the IP. Do not redistribute the assets beyond what the Internet Archive already makes public.
