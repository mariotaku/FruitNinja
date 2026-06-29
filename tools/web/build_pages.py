#!/usr/bin/env python3
"""
tools/web/build_pages.py -- assemble the GitHub Pages output directory.

Usage:
    python tools/web/build_pages.py [--out <dir>]

Output layout (default: pages/):
    pages/
      .nojekyll
      index.html          <- web/index.html
      game/
        index.html        <- build/web/fruit-ninja.html (renamed)
        fruit-ninja.js
        fruit-ninja.wasm
        fruit-ninja.data
        splash.webp
        play_button.webp
        sound.webp
        sound_cross.webp
      models/
        index.html        <- docs/gallery/models/index.html
        models.json
        fruit_atlas.png
      textures/
        index.html        <- docs/gallery/textures/index.html
        Data/             <- docs/gallery/textures/Data/ (recursive)

Run from the repository root.  Idempotent: clears pages/ before each run.
Errors if build/web/ is missing or fruit-ninja.html is absent.
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

SRC_LANDING     = os.path.join(REPO_ROOT, "web", "index.html")
SRC_BUILD_WEB   = os.path.join(REPO_ROOT, "build", "web")
SRC_MODELS_DIR  = os.path.join(REPO_ROOT, "docs", "gallery", "models")
SRC_TEX_DIR     = os.path.join(REPO_ROOT, "docs", "gallery", "textures")

# Non-hashed extra assets copied from build/web/ into pages/game/ as-is.
# These are NOT referenced by name in HTML/JS so they don't need hashing.
GAME_EXTRA_FILES = [
    "play_button.webp",
    "sound.webp",
    "sound_cross.webp",
]

# Hashed file patterns: stem -> (pattern, ext)
# web-hash-assets.py renames canonical files to stem-<8hex>.ext.
# We discover whichever hash is present.
_HASHED_STEMS = [
    ("fruit-ninja", "js"),
    ("fruit-ninja", "wasm"),
    ("fruit-ninja", "data"),
    ("splash",      "webp"),
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

# Files we copy from docs/gallery/models/ into pages/models/
MODELS_FILES = [
    "index.html",
    "models.json",
    "fruit_atlas.png",
]


def die(msg):
    print("ERROR:", msg, file=sys.stderr)
    sys.exit(1)


def ensure_dir(path):
    os.makedirs(path, exist_ok=True)


def copy_file(src, dst):
    ensure_dir(os.path.dirname(dst))
    shutil.copy2(src, dst)
    return os.path.getsize(dst)


def copy_tree(src, dst):
    """Recursively copy src directory into dst (dst must not exist yet)."""
    shutil.copytree(src, dst)
    total = 0
    for dirpath, _, filenames in os.walk(dst):
        for fname in filenames:
            total += os.path.getsize(os.path.join(dirpath, fname))
    return total


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
    if not os.path.isfile(SRC_LANDING):
        die("web/index.html not found -- expected at: " + SRC_LANDING)

    game_html_src = os.path.join(SRC_BUILD_WEB, "fruit-ninja.html")
    if not os.path.isdir(SRC_BUILD_WEB):
        die("build/web/ directory not found.\n"
            "       Run the Emscripten build first:\n"
            "         emcmake cmake -S . -B build/web -DCMAKE_BUILD_TYPE=Release\n"
            "         cmake --build build/web -j")
    if not os.path.isfile(game_html_src):
        die("build/web/fruit-ninja.html not found -- build/web/ exists but "
            "build may be incomplete.")

    # Discover hashed game files (fruit-ninja-<hash>.{js,wasm,data}, splash-<hash>.webp).
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

    if not os.path.isdir(SRC_MODELS_DIR):
        die("docs/gallery/models/ not found at: " + SRC_MODELS_DIR)
    if not os.path.isdir(SRC_TEX_DIR):
        die("docs/gallery/textures/ not found at: " + SRC_TEX_DIR)

    # ------------------------------------------------------------------
    # Clean output -- non-destructive to top-level files.
    # Only clean the regenerated SUBDIRS (game/models/textures) so the copytree
    # targets get a clean dest; the top-level files (index.html, .nojekyll) are
    # overwritten in place by the copy steps below and any extra files are kept.
    # (Previously this rmtree'd the whole output dir, deleting index.html + any
    # extra/deployed files and breaking a live server mid-publish.)
    # ------------------------------------------------------------------
    ensure_dir(out)
    for _sub in ("game", "models", "textures"):
        _subpath = os.path.join(out, _sub)
        if os.path.isdir(_subpath):
            shutil.rmtree(_subpath)
            print("Cleaned subdir:", _subpath)

    summary = []

    # ------------------------------------------------------------------
    # .nojekyll
    # ------------------------------------------------------------------
    nojekyll = os.path.join(out, ".nojekyll")
    open(nojekyll, "w").close()
    summary.append(("pages/.nojekyll", 0))

    # ------------------------------------------------------------------
    # pages/index.html
    # ------------------------------------------------------------------
    sz = copy_file(SRC_LANDING, os.path.join(out, "index.html"))
    summary.append(("pages/index.html", sz))

    # ------------------------------------------------------------------
    # pages/game/
    # ------------------------------------------------------------------
    game_dir = os.path.join(out, "game")
    ensure_dir(game_dir)

    # fruit-ninja.html -> game/index.html
    sz = copy_file(game_html_src, os.path.join(game_dir, "index.html"))
    summary.append(("pages/game/index.html (from fruit-ninja.html)", sz))

    # Hashed files (fruit-ninja-<hash>.{js,wasm,data}, splash-<hash>.webp).
    for gf in hashed_game_files:
        src_path = os.path.join(SRC_BUILD_WEB, gf)
        if os.path.isfile(src_path):
            sz = copy_file(src_path, os.path.join(game_dir, gf))
            summary.append(("pages/game/" + gf, sz))

    # Non-hashed extra assets (not referenced by name in HTML/JS).
    for gf in GAME_EXTRA_FILES:
        src_path = os.path.join(SRC_BUILD_WEB, gf)
        if os.path.isfile(src_path):
            sz = copy_file(src_path, os.path.join(game_dir, gf))
            summary.append(("pages/game/" + gf, sz))

    # ------------------------------------------------------------------
    # pages/models/
    # ------------------------------------------------------------------
    models_dir = os.path.join(out, "models")
    ensure_dir(models_dir)

    for mf in MODELS_FILES:
        src_path = os.path.join(SRC_MODELS_DIR, mf)
        if os.path.isfile(src_path):
            sz = copy_file(src_path, os.path.join(models_dir, mf))
            summary.append(("pages/models/" + mf, sz))
        else:
            print("WARNING: models file not found:", mf)

    # ------------------------------------------------------------------
    # pages/textures/
    # ------------------------------------------------------------------
    tex_dst = os.path.join(out, "textures")

    # Copy textures/index.html separately, then the Data/ tree
    tex_index_src = os.path.join(SRC_TEX_DIR, "index.html")
    if not os.path.isfile(tex_index_src):
        die("docs/gallery/textures/index.html not found")

    ensure_dir(tex_dst)
    sz = copy_file(tex_index_src, os.path.join(tex_dst, "index.html"))
    summary.append(("pages/textures/index.html", sz))

    tex_data_src = os.path.join(SRC_TEX_DIR, "Data")
    if os.path.isdir(tex_data_src):
        copy_tree(tex_data_src, os.path.join(tex_dst, "Data"))
        n_files, n_bytes = count_tree(os.path.join(tex_dst, "Data"))
        summary.append(("pages/textures/Data/ ({} files)".format(n_files), n_bytes))
    else:
        print("WARNING: docs/gallery/textures/Data/ not found -- textures gallery will be incomplete")

    # ------------------------------------------------------------------
    # Summary
    # ------------------------------------------------------------------
    total_files, total_bytes = count_tree(out)
    print()
    print("=" * 60)
    print("  pages/ assembled successfully")
    print("=" * 60)
    for label, sz in summary:
        print("  {:<52} {}".format(label, fmt_bytes(sz)))
    print()
    print("  Total: {} files, {}".format(total_files, fmt_bytes(total_bytes)))
    print("  Output: " + os.path.abspath(out))
    print()
    print("  Preview locally:")
    print("    cd pages && python -m http.server 8000")
    print("    open http://localhost:8000")
    print()


if __name__ == "__main__":
    main()
