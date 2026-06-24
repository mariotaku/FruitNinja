#!/usr/bin/env python3
"""
tools/web/web-hash-assets.py -- Content-hash web output files to defeat browser caching.

Usage:
    python3 tools/web/web-hash-assets.py <build/web>

Pipeline (order matters -- nested references):
  1. Hash wasm + data + splash.webp.  Rename to name-<sha8>.ext.
  2. Rewrite fruit-ninja.js: replace references to hashed wasm + data filenames.
  3. Hash the now-rewritten fruit-ninja.js.  Rename to fruit-ninja-<sha8>.js.
  4. Rewrite fruit-ninja.html: replace script src + splash src with hashed names.
  5. Prune stale hashed files from prior builds (pattern name-<8hex>.ext).

fruit-ninja.html stays unhashed (stable entry point).

Idempotent: emcc always re-emits the canonical fruit-ninja.{html,js,wasm,data}
on each full build, so this script always operates on those fresh canonical names.
If any canonical file is missing, it is skipped (partial build scenario).
"""

import hashlib
import os
import re
import sys


# ---------------------------------------------------------------------------
# Reference literals emcc emits in fruit-ninja.js and fruit-ninja.html
# (verified against emscripten/emsdk:latest / emcc 6.x output).
# ---------------------------------------------------------------------------

# JS: the .data package loader uses these two identical string literals.
# Both must be replaced; order does not matter since they are independent.
_DATA_LITERALS = [
    'PACKAGE_NAME="fruit-ninja.data"',
    'REMOTE_PACKAGE_BASE="fruit-ninja.data"',
]
# JS: the wasm loader calls locateFile with this exact string literal.
_WASM_LITERAL = 'locateFile("fruit-ninja.wasm")'

# HTML: emcc minifies the <script> tag to this exact form (no quotes around src value).
_HTML_SCRIPT_LITERAL = '<script async src=fruit-ninja.js>'
# HTML: shell.html emits this for the splash img.
_HTML_SPLASH_LITERAL = 'src=splash.webp'


def sha8(path):
    """Return first 8 hex digits of the SHA-256 of the file at path."""
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()[:8]


def hashed_name(stem, digest, ext):
    """Return 'stem-digest.ext' (e.g. 'fruit-ninja-ab12cd34.wasm')."""
    return "{}-{}.{}".format(stem, digest, ext)


def rename_with_hash(src_path, stem, digest, ext):
    """Rename src_path to stem-digest.ext in the same directory. Returns new path."""
    dst_name = hashed_name(stem, digest, ext)
    dst_path = os.path.join(os.path.dirname(src_path), dst_name)
    os.rename(src_path, dst_path)
    return dst_path


def read_text(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def write_text(path, content):
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)


def prune_stale(out_dir, stem, ext, keep_digest):
    """
    Delete any file matching stem-<8hex>.ext in out_dir that is NOT keep_digest.
    Matches only exactly 8 lowercase hex characters.
    """
    pattern = re.compile(r"^{}-([0-9a-f]{{8}})\.{}$".format(re.escape(stem), re.escape(ext)))
    removed = []
    for name in os.listdir(out_dir):
        m = pattern.match(name)
        if m and m.group(1) != keep_digest:
            path = os.path.join(out_dir, name)
            os.remove(path)
            removed.append(name)
    return removed


def main():
    if len(sys.argv) < 2:
        print("Usage: web-hash-assets.py <build/web>", file=sys.stderr)
        sys.exit(1)

    out_dir = sys.argv[1]
    if not os.path.isdir(out_dir):
        print("ERROR: not a directory: {}".format(out_dir), file=sys.stderr)
        sys.exit(1)

    def p(path):
        return os.path.join(out_dir, path)

    print("[web-hash] starting in: {}".format(os.path.abspath(out_dir)))

    # -----------------------------------------------------------------------
    # Step 1: Hash wasm + data + splash.webp; rename each to name-<sha8>.ext
    # -----------------------------------------------------------------------

    wasm_digest = data_digest = splash_digest = None

    # -- wasm --
    wasm_src = p("fruit-ninja.wasm")
    if os.path.isfile(wasm_src):
        wasm_digest = sha8(wasm_src)
        wasm_hashed = rename_with_hash(wasm_src, "fruit-ninja", wasm_digest, "wasm")
        print("[web-hash] wasm  {} -> {}".format("fruit-ninja.wasm", os.path.basename(wasm_hashed)))
    else:
        print("[web-hash] WARNING: fruit-ninja.wasm not found -- skipping")

    # -- data --
    data_src = p("fruit-ninja.data")
    if os.path.isfile(data_src):
        data_digest = sha8(data_src)
        data_hashed = rename_with_hash(data_src, "fruit-ninja", data_digest, "data")
        print("[web-hash] data  {} -> {}".format("fruit-ninja.data", os.path.basename(data_hashed)))
    else:
        print("[web-hash] WARNING: fruit-ninja.data not found -- skipping")

    # -- splash --
    splash_src = p("splash.webp")
    if os.path.isfile(splash_src):
        splash_digest = sha8(splash_src)
        splash_hashed = rename_with_hash(splash_src, "splash", splash_digest, "webp")
        print("[web-hash] splash {} -> {}".format("splash.webp", os.path.basename(splash_hashed)))
    else:
        print("[web-hash] WARNING: splash.webp not found -- skipping")

    # -----------------------------------------------------------------------
    # Step 2: Rewrite fruit-ninja.js -- replace wasm + data references
    # -----------------------------------------------------------------------

    js_src = p("fruit-ninja.js")
    if not os.path.isfile(js_src):
        print("[web-hash] WARNING: fruit-ninja.js not found -- skipping js rewrite")
        js_digest = None
    else:
        js_content = read_text(js_src)

        # Replace data references (PACKAGE_NAME + REMOTE_PACKAGE_BASE)
        if data_digest is not None:
            new_data_name = hashed_name("fruit-ninja", data_digest, "data")
            for literal in _DATA_LITERALS:
                new_literal = literal.replace("fruit-ninja.data", new_data_name)
                count = js_content.count(literal)
                if count == 0:
                    print("[web-hash] WARNING: JS literal not found: {}".format(literal))
                js_content = js_content.replace(literal, new_literal)
                print("[web-hash] js: replaced {} occurrence(s) of {!r}".format(count, literal))

            # Also replace the datafile_ dependency key (two occurrences expected)
            old_dep = '"datafile_fruit-ninja.data"'
            new_dep = '"datafile_{}"'.format(new_data_name)
            count = js_content.count(old_dep)
            if count > 0:
                js_content = js_content.replace(old_dep, new_dep)
                print("[web-hash] js: replaced {} occurrence(s) of {!r}".format(count, old_dep))

        # Replace wasm reference
        if wasm_digest is not None:
            new_wasm_name = hashed_name("fruit-ninja", wasm_digest, "wasm")
            new_wasm_literal = _WASM_LITERAL.replace("fruit-ninja.wasm", new_wasm_name)
            count = js_content.count(_WASM_LITERAL)
            if count == 0:
                print("[web-hash] WARNING: wasm literal not found in JS: {}".format(_WASM_LITERAL))
            js_content = js_content.replace(_WASM_LITERAL, new_wasm_literal)
            print("[web-hash] js: replaced {} occurrence(s) of {!r}".format(count, _WASM_LITERAL))

        write_text(js_src, js_content)
        print("[web-hash] js: rewrites applied -> {}".format(os.path.basename(js_src)))

        # -----------------------------------------------------------------------
        # Step 3: Hash the now-rewritten js; rename to fruit-ninja-<sha8>.js
        # -----------------------------------------------------------------------
        js_digest = sha8(js_src)
        js_hashed = rename_with_hash(js_src, "fruit-ninja", js_digest, "js")
        print("[web-hash] js   fruit-ninja.js -> {}".format(os.path.basename(js_hashed)))

    # -----------------------------------------------------------------------
    # Step 4: Rewrite fruit-ninja.html -- replace script src + splash src
    # -----------------------------------------------------------------------

    html_path = p("fruit-ninja.html")
    if not os.path.isfile(html_path):
        print("[web-hash] WARNING: fruit-ninja.html not found -- skipping html rewrite")
    else:
        html_content = read_text(html_path)

        # Replace <script async src=fruit-ninja.js>
        if js_digest is not None:
            new_js_name = hashed_name("fruit-ninja", js_digest, "js")
            new_script = _HTML_SCRIPT_LITERAL.replace("fruit-ninja.js", new_js_name)
            count = html_content.count(_HTML_SCRIPT_LITERAL)
            if count == 0:
                print("[web-hash] WARNING: script literal not found in HTML: {}".format(_HTML_SCRIPT_LITERAL))
            html_content = html_content.replace(_HTML_SCRIPT_LITERAL, new_script)
            print("[web-hash] html: replaced {} occurrence(s) of script src".format(count))

        # Replace src=splash.webp
        if splash_digest is not None:
            new_splash_name = hashed_name("splash", splash_digest, "webp")
            new_splash = _HTML_SPLASH_LITERAL.replace("splash.webp", new_splash_name)
            count = html_content.count(_HTML_SPLASH_LITERAL)
            if count == 0:
                print("[web-hash] WARNING: splash literal not found in HTML: {}".format(_HTML_SPLASH_LITERAL))
            html_content = html_content.replace(_HTML_SPLASH_LITERAL, new_splash)
            print("[web-hash] html: replaced {} occurrence(s) of splash src".format(count))

        # Replace the build-identifier placeholder with the wasm sha8 so the
        # bottom-left "build <hash>" overlay (shown when ?fps is set) matches
        # the actual loaded wasm -- a quick cache/freshness check on device.
        if wasm_digest is not None:
            count = html_content.count("__FN_BUILD__")
            html_content = html_content.replace("__FN_BUILD__", wasm_digest)
            print("[web-hash] html: replaced {} occurrence(s) of __FN_BUILD__ -> {}".format(count, wasm_digest))

        write_text(html_path, html_content)
        print("[web-hash] html: rewrites applied -> fruit-ninja.html (unchanged filename)")

    # -----------------------------------------------------------------------
    # Step 5: Prune stale hashed files from prior builds
    # -----------------------------------------------------------------------

    print("[web-hash] pruning stale hashes...")

    if wasm_digest is not None:
        removed = prune_stale(out_dir, "fruit-ninja", "wasm", wasm_digest)
        for name in removed:
            print("[web-hash] pruned stale: {}".format(name))

    if data_digest is not None:
        removed = prune_stale(out_dir, "fruit-ninja", "data", data_digest)
        for name in removed:
            print("[web-hash] pruned stale: {}".format(name))

    if js_digest is not None:
        removed = prune_stale(out_dir, "fruit-ninja", "js", js_digest)
        for name in removed:
            print("[web-hash] pruned stale: {}".format(name))

    if splash_digest is not None:
        removed = prune_stale(out_dir, "splash", "webp", splash_digest)
        for name in removed:
            print("[web-hash] pruned stale: {}".format(name))

    print("[web-hash] done.")
    print("[web-hash] current set:")
    for name in sorted(os.listdir(out_dir)):
        if re.search(r"-[0-9a-f]{8}\.(js|wasm|data)$", name) or re.search(r"-[0-9a-f]{8}\.webp$", name) or name == "fruit-ninja.html":
            size = os.path.getsize(os.path.join(out_dir, name))
            print("[web-hash]   {} ({} bytes)".format(name, size))


if __name__ == "__main__":
    main()
