# tools/web/

Emscripten web-build packaging + LAN dev serving (feeds the GitHub Pages deploy).

- **`build_pages.py`** — assemble the GitHub Pages output dir (`pages/`) from `web/` + `build-web/`. `python tools/web/build_pages.py [--out <dir>]`.
- **`web-hash-assets.py`** — content-hash the wasm/data/js/splash to defeat browser caching, rewriting nested references. `python3 web-hash-assets.py <build-web-dir>`.
- **`web-serve.py`** — LAN dev server with content-hash-aware caching (no-store HTML, immutable hashed assets, `application/wasm`). `web-serve.py [--dir build-web] [--port 8000]`.
- **`rebuild-web.sh`** — incremental Emscripten rebuild when `src/` changed; run detached by the Claude Code Stop hook so the dev server stays current.
