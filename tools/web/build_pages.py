#!/usr/bin/env python3
"""
tools/web/build_pages.py -- assemble the GitHub Pages output directory.

Usage:
    python tools/web/build_pages.py [--out <dir>]

The deployed site is just the game at the root -- no landing page, no
galleries.  Output layout (default: pages/):

    pages/
      .nojekyll
      index.html                  <- build/web/index.html (entry, renamed from
                                     fruit-ninja.html by tools/web/build.sh)
      fruit-ninja-<sha8>.js
      fruit-ninja-<sha8>.wasm
      fruit-ninja-<sha8>.data
      manifest.webmanifest        <- PWA manifest (static, unhashed by design)
      sw.js                       <- service worker (root scope; runtime caching)
      favicon.ico
      icons/                      <- PWA icon PNGs (16/32/180/192/512/maskable)

The game's index.html references its assets by bare relative name (same
directory), so placing them next to index.html at the root needs no
reference rewriting.

Run from the repository root.  Idempotent: removes and recreates the output
directory each run so stale files from previous layouts don't linger.
Errors if build/web/index.html is missing.
"""

import argparse
import os
import re
import shutil
import sys

# ---------------------------------------------------------------------------
# Paths (relative to repo root, which is the cwd at run-time)
# ---------------------------------------------------------------------------
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

SRC_BUILD_WEB = os.path.join(REPO_ROOT, "build", "web")

# Non-hashed extra assets copied from build/web/ into pages/ as-is.
# These are NOT referenced by name in HTML/JS so they don't need hashing.
GAME_EXTRA_FILES = []

# PWA files copied from build/web/ into pages/ root as-is.  Static and
# UNHASHED by design: the manifest / service worker / favicon must keep
# stable names across builds (web-hash-assets.py leaves them alone).
PWA_FILES = [
    "manifest.webmanifest",
    "sw.js",
    "favicon.ico",
]

# PWA icon PNG directory, copied wholesale to pages/icons/.
ICONS_DIRNAME = "icons"

# Hashed file patterns: stem -> (pattern, ext)
# web-hash-assets.py renames canonical files to stem-<8hex>.ext.
# We discover whichever hash is present.
_HASHED_STEMS = [
    ("fruit-ninja", "js"),
    ("fruit-ninja", "wasm"),
    ("fruit-ninja", "data"),
]


def find_hashed_file(build_web_dir, stem, ext):
    """
    Return the filename of the single hashed file matching stem-<8hex>.ext
    in build_web_dir, or None if not found.  Warns if multiple matches exist.
    """
    pattern = re.compile(r"^{}-([0-9a-f]{{8}})\.{}$".format(re.escape(stem), re.escape(ext)))
    matches = [name for name in os.listdir(build_web_dir) if pattern.match(name)]
    if not matches:
        # Fall back to unhashed canonical name (build without hash step).
        canonical = "{}.{}".format(stem, ext)
        if os.path.isfile(os.path.join(build_web_dir, canonical)):
            return canonical
        return None
    if len(matches) > 1:
        print("WARNING: multiple hashed files for {}.{}: {}".format(stem, ext, matches))
    return matches[0]


def die(msg):
    print("ERROR:", msg, file=sys.stderr)
    sys.exit(1)


def copy_file(src, dst):
    shutil.copy2(src, dst)
    return os.path.getsize(dst)


def fmt_bytes(n):
    if n < 1024:
        return "{} B".format(n)
    if n < 1024 * 1024:
        return "{:.1f} KB".format(n / 1024)
    return "{:.1f} MB".format(n / (1024 * 1024))


def count_tree(path):
    total_files = 0
    total_bytes = 0
    for dirpath, _, filenames in os.walk(path):
        for fname in filenames:
            total_files += 1
            total_bytes += os.path.getsize(os.path.join(dirpath, fname))
    return total_files, total_bytes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", default=os.path.join(REPO_ROOT, "pages"),
                        help="Output directory (default: pages/)")
    args = parser.parse_args()
    out = args.out

    # ------------------------------------------------------------------
    # Pre-flight checks
    # ------------------------------------------------------------------
    game_html_src = os.path.join(SRC_BUILD_WEB, "index.html")
    if not os.path.isdir(SRC_BUILD_WEB):
        die("build/web/ directory not found.\n"
            "       Run the Emscripten build first:\n"
            "         bash tools/web/build.sh --release   (inside emscripten/emsdk)")
    if not os.path.isfile(game_html_src):
        die("build/web/index.html not found -- build/web/ exists but the build "
            "may be incomplete (tools/web/build.sh renames fruit-ninja.html to "
            "index.html after a verified build).")

    # Discover hashed game files (fruit-ninja-<hash>.{js,wasm,data}).
    hashed_game_files = []
    for stem, ext in _HASHED_STEMS:
        name = find_hashed_file(SRC_BUILD_WEB, stem, ext)
        if name:
            hashed_game_files.append(name)
        else:
            print("WARNING: hashed game file not found for {}.{}".format(stem, ext))

    for gf in GAME_EXTRA_FILES:
        p_path = os.path.join(SRC_BUILD_WEB, gf)
        if not os.path.isfile(p_path):
            print("WARNING: expected extra game file missing:", gf)

    # ------------------------------------------------------------------
    # Clean output -- remove and recreate so stale files from previous
    # layouts (old landing page, game/, models/, textures/) don't linger.
    # ------------------------------------------------------------------
    if os.path.isdir(out):
        shutil.rmtree(out)
        print("Cleaned:", out)
    os.makedirs(out)

    summary = []

    # ------------------------------------------------------------------
    # .nojekyll
    # ------------------------------------------------------------------
    nojekyll = os.path.join(out, ".nojekyll")
    open(nojekyll, "w").close()
    summary.append(("pages/.nojekyll", 0))

    # ------------------------------------------------------------------
    # pages/index.html  <- build/web/index.html (the game entry, at root)
    # ------------------------------------------------------------------
    sz = copy_file(game_html_src, os.path.join(out, "index.html"))
    summary.append(("pages/index.html (from build/web/index.html)", sz))

    # Hashed files (fruit-ninja-<hash>.{js,wasm,data}).
    # The game HTML/JS references these by bare same-dir name -- no rewriting.
    for gf in hashed_game_files:
        src_path = os.path.join(SRC_BUILD_WEB, gf)
        if os.path.isfile(src_path):
            sz = copy_file(src_path, os.path.join(out, gf))
            summary.append(("pages/" + gf, sz))

    # Non-hashed extra assets (not referenced by name in HTML/JS).
    for gf in GAME_EXTRA_FILES:
        src_path = os.path.join(SRC_BUILD_WEB, gf)
        if os.path.isfile(src_path):
            sz = copy_file(src_path, os.path.join(out, gf))
            summary.append(("pages/" + gf, sz))

    # PWA files (manifest.webmanifest, sw.js, favicon.ico) -- static, unhashed.
    for pf in PWA_FILES:
        src_path = os.path.join(SRC_BUILD_WEB, pf)
        if os.path.isfile(src_path):
            sz = copy_file(src_path, os.path.join(out, pf))
            summary.append(("pages/" + pf, sz))
        else:
            print("WARNING: expected PWA file missing:", pf)

    # PWA icons directory -> pages/icons/.
    icons_src = os.path.join(SRC_BUILD_WEB, ICONS_DIRNAME)
    if os.path.isdir(icons_src):
        icons_dst = os.path.join(out, ICONS_DIRNAME)
        shutil.copytree(icons_src, icons_dst)
        icon_files, icon_bytes = count_tree(icons_dst)
        summary.append(("pages/icons/ ({} files)".format(icon_files), icon_bytes))
    else:
        print("WARNING: expected PWA icons directory missing: build/web/icons/")

    # ------------------------------------------------------------------
    # Summary
    # ------------------------------------------------------------------
    total_files, total_bytes = count_tree(out)
    print()
    print("=" * 60)
    print("  pages/ assembled successfully (game at root)")
    print("=" * 60)
    for label, sz in summary:
        print("  {:<52} {}".format(label, fmt_bytes(sz)))
    print()
    print("  Total: {} files, {}".format(total_files, fmt_bytes(total_bytes)))
    print("  Output: " + os.path.abspath(out))
    print()
    print("  Preview locally:")
    print("    cd pages && python -m http.server 8000")
    print("    open http://localhost:8000/")
    print()


if __name__ == "__main__":
    main()
