# tools/web/

Emscripten web-build packaging + LAN dev serving, and the GitHub Pages deploy runbook.

## Scripts

- **`build.sh`** — the single native build entrypoint; local dev (`rebuild-web.sh`) and CI (`.github/workflows/pages.yml`) both run it against a **native emsdk 6.0.0** toolchain (no Docker) — `emcc`/`ninja`/`ffmpeg` on PATH. On Windows/MSYS it `cygpath -m`-normalizes paths for the native cmake/ninja, and must run WITHOUT `MSYS_NO_PATHCONV` (it breaks CMake's internal `try_compile`). Configures `build/web` if needed, pre-clears stale link outputs (never the `.data`), builds the static lib **before** the executable (parallel-`-j` link-race workaround), then verifies the emcc-emitted `fruit-ninja.html` + the main wasm with up to 3 link retries, and finally renames the entry HTML to `index.html` (served at the directory root; internal `fruit-ninja[-<sha8>].js/.wasm/.data` names are untouched). Flags: no flags = reuse the existing configure (preserves a local `FN_WEB_DEBUG` tree); `--release` = force Release + `FN_WEB_DEBUG=OFF`; `--debug` = Release + `FN_WEB_DEBUG=ON` (separate-DWARF debug, unhashed outputs); `--reconfigure` = force re-configure; `--profiling` = force reconfigure + `FN_WEB_PROFILING=ON` (keeps wasm function names for flame-graph profiling; see "Profiling the web build" below). CMake stays the packaging source of truth (shell.html, `--preload-file`, webp copies, `web-hash-assets.py`).
- **`build_pages.py`** — assemble the GitHub Pages output dir (`pages/`) from `build/web/`: the game at the site root (`index.html` + hashed `fruit-ninja-<sha8>.{js,wasm,data}` + `splash-<sha8>.webp` + the three unhashed webp buttons + `.nojekyll`) — no landing page, no galleries. The build also emits PWA files (`manifest.webmanifest`, `sw.js`, `favicon.ico`, `icons/` — static, unhashed by design), so the deploy is installable and offline-capable after one full online load. `python tools/web/build_pages.py [--out <dir>]`. Idempotent (removes+recreates `pages/`); errors if `build/web/index.html` is missing.
- **`web-hash-assets.py`** — content-hash wasm/data/js/splash to defeat browser caching, rewriting all nested references (js data/wasm refs, html script/splash/build-id). `python3 web-hash-assets.py <build/web>`. Robust to incremental builds: emcc re-emits `fruit-ninja.{js,wasm,html}` every build but only re-emits `fruit-ninja.data` when packaged assets change, so each asset's hash is resolved from the fresh canonical file *or* (when unchanged/absent) from the already-hashed `fruit-ninja-<sha8>.ext` — the js/html refs are always rewritten to a valid hashed name, and the 49MB `.data` is never re-copied when unchanged. Ends with a self-check that the served js references no un-hashed asset (exits non-zero otherwise).
- **`web-serve.py`** — LAN dev server with content-hash-aware caching (no-store HTML, immutable hashed assets, `application/wasm`). `web-serve.py [--dir build/web] [--port 8000]`.
- **`../assets/stage-assets.py`** — unified build-phase asset staging for BOTH host and web (run by the `fn_asset_staging` CMake target before `fruit-ninja` links): mirrors `FruitNinjaBada/Data` into the staging dir CMake passes it (`FN_STAGING_DATA_DIR`, `${CMAKE_BINARY_DIR}/staging/Data`), re-encoding Tex1 textures -> WebP-in-`.tex` via Pillow (host + web alike) and merging in pre-generated widget art from `assets/ui-widgets/generated/`. Under `--web` it additionally transcodes sfx `.wav.pcm` -> Ogg/Vorbis and subsets `gangofchinese.ttf` to the used glyph set (loop points ship as a build-generated C++ table linked into the wasm — `--gen-loop-table`, see `src/engine/audio/SfxLoopTable.h` — not as a runtime file); everything else is copied verbatim. Self-provisions its Python tools (Pillow + fontTools): `apt` inside a PEP 668 container, `pip` on a native host. `ffmpeg` (used under `--web`) must be on PATH — `apt` provides it in the container; install it natively (e.g. winget/choco) for a native build. `python3 stage-assets.py <repo_root> <out_staging_data_dir> [--web]`. See `tools/assets/README.md`.
- **`config.sh`** — single source of truth for values shared by `build.sh`/`rebuild-web.sh`/`build-gallery.sh` (source it) and `.github/workflows/pages.yml` (reads it into `$GITHUB_ENV`): the pinned `EMSDK_VERSION` (`6.0.0`) plus the shared failure helpers (`fn_web_strict` / `fn_web_fatal` / `fn_web_warn` / `fn_web_hash_file` / `fn_web_is_ci`). Source-safe: it only defines things, never `set -e`s or exits.
- **`rebuild-web.sh`** — auto-rebuild dispatcher for the Claude Code Stop hook: gates on `src/` changes vs the newest wasm, preflights the toolchain **synchronously**, then runs `build.sh` **natively**, detached (lock + `tmp/web-rebuild.log`), so the dev server stays current. Sources emsdk from `${FN_EMSDK:-/c/tools/emsdk}/emsdk_env.sh` if `emcc` isn't already on PATH, and requires `ninja` + `ffmpeg` on PATH (or `FN_NINJA_DIR`/`FN_FFMPEG_DIR`, or — for ninja only — a still-valid `CMAKE_MAKE_PROGRAM` in `build/web/CMakeCache.txt`). Never sets `MSYS_NO_PATHCONV`.

### Failure reporting (why a build can no longer "succeed" silently)

- Every script runs strict mode + an ERR trap: unexpected failures print `FATAL: <script>: line N: command failed (exit S)` with the command.
- **Unknown flags are fatal** (they list the accepted set) — a dropped `--release` used to be ignored silently.
- Preflight failures name one missing thing each (ninja / `FN_NINJA_DIR`, emcc / `FN_EMSDK`, ffmpeg, cmake, python, the asset dump), plus what to do about it.
- `build.sh` treats the **wasm content hash**, not the exit code, as the success signal: unchanged hash + a source newer than the artifact + a changed `libfruit-ninja-game.a` = `FATAL` (the build shipped the old wasm). Unchanged hash with nothing newer is a normal no-op; unchanged hash with byte-identical objects is a warning.
- The detached worker records its outcome in `tmp/web-rebuild.status` (`status=`/`exit=`/`wasm_before=`/`wasm_after=`) and drops `tmp/web-rebuild.failed` / `tmp/web-rebuild.warned`; the **next** dispatcher run prints them and exits non-zero, so a background failure surfaces in the session instead of dying in the log.
- **`regen-shell.py`** — fast shell-only regen of `index.html` from the current `shell.html`, reusing the already-built hashed js/wasm/data on disk (no emcc, <1s). Only valid when no `.cpp`/`.h` changed since the last real build — see the script's own header for the `{{{ SCRIPT }}}` substitution it replicates. `python3 tools/web/regen-shell.py [build/web]`.

## Build & preview locally (native emsdk, same as CI)

Prereqs: **emsdk 6.0.0** (`git clone emsdk` + `emsdk install/activate 6.0.0`; the docs default to `C:\tools\emsdk` here, overridable via `$FN_EMSDK`), plus **ninja** and **ffmpeg** on PATH. Assets must be at `FruitNinjaBada/Data` first.

```sh
source /c/tools/emsdk/emsdk_env.sh          # emcc on PATH
# ninja + ffmpeg on PATH too (or set FN_NINJA_DIR / FN_FFMPEG_DIR)
bash tools/web/build.sh --release           # do NOT set MSYS_NO_PATHCONV
python tools/web/build_pages.py
cd pages && python -m http.server 8000       # http://localhost:8000
```

Or the usual dev wrapper: `bash tools/web/rebuild-web.sh --worker` (auto-sources emsdk, honors `FN_NINJA_DIR`/`FN_FFMPEG_DIR`, logs to `tmp/web-rebuild.log`).

The site is just the wasm game at the root; no server-side logic.

## GitHub Pages deploy

Static site (the wasm game at the root) via the Actions artifact mechanism (no `gh-pages` branch).

- **One-time repo setup** (manual; the workflow can't do this itself): **Settings -> Pages -> Build and deployment -> Source -> GitHub Actions**.
- **Trigger**: **Actions -> Pages -> Run workflow**. `workflow_dispatch` only — never automatic. ~10-15 min (Emscripten build dominates). Publishes to `https://<org>.github.io/<repo>/`.
- **Assets**: `.github/workflows/pages.yml` fetches the Bada game data from the Internet Archive at build time and asserts the binary MD5 so assets can't silently drift. The asset identifiers + hashes live in the `pages.yml` header (single source of truth); update them there if the archive.org item moves.

## Profiling the web build

Opt-in build that keeps C++ function names in the wasm name section (`--profiling-funcs` / `FN_WEB_PROFILING`, OFF by default) so Chrome/Firefox DevTools show real symbols in a flame graph instead of `wasm-function[N]`:

```sh
bash tools/web/rebuild-web.sh --worker --profiling
```

(or directly: `bash tools/web/build.sh --profiling`, with the native toolchain on PATH). Then open the served page in Chrome, DevTools -> Performance -> record ~5s of gameplay -> Stop; the flame chart now shows real C++ function names. `--profiling-funcs` is pure name-section metadata (no codegen/timing change), so the profile is representative. The setting is sticky in the `build/web` CMake cache until the next plain/`--release`/`--debug`/`--reconfigure` build, which reverts to stripped names.

## Copyright

Publishing the site exposes Halfbrick's original Fruit Ninja assets (textures, meshes, audio). This is a research/RE project; Halfbrick Studios owns the IP. Do not redistribute the assets beyond what the Internet Archive already makes public.
