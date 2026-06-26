#!/usr/bin/env python3
"""
tools/web/web-hash-assets.py -- Content-hash web output files to defeat browser caching.

Usage:
    python3 tools/web/web-hash-assets.py <build/web>

Every build, ALL of wasm + data + js (+ splash) end up as content-hashed
fruit-ninja-<sha8>.ext files, and fruit-ninja.js / fruit-ninja.html are rewritten
to reference those hashed names. fruit-ninja.html keeps its stable (unhashed) name
as the entry point.

Why this is careful about incremental builds
--------------------------------------------
emcc re-emits the canonical fruit-ninja.{js,wasm,html} on every build, but it does
NOT re-emit fruit-ninja.DATA when the packaged assets are unchanged. The previous
version of this script `rename`d the canonical files to hashed names; on the next
incremental build fruit-ninja.data was therefore ABSENT (already renamed), the data
hash was skipped, and the freshly-emitted fruit-ninja.js kept its un-hashed
'fruit-ninja.data' reference -> 404 at runtime (only fruit-ninja-<datahash>.data
exists on disk).

Fix: resolve each asset's digest from the fresh canonical file when present, OR
from the already-existing fruit-ninja-<sha8>.ext when the canonical is absent
(unchanged this build). The JS/HTML reference rewrite then ALWAYS runs against a
valid digest, hashed or not. No 49MB data re-copy when the data is unchanged.

Idempotent and safe to re-run on an already-hashed tree.
"""

import hashlib
import os
import re
import sys

STEM = "fruit-ninja"

# Reference literals emcc emits in fruit-ninja.js / fruit-ninja.html
# (emscripten/emsdk:latest, emcc 6.x).
_DATA_LITERALS = [
    'PACKAGE_NAME="fruit-ninja.data"',
    'REMOTE_PACKAGE_BASE="fruit-ninja.data"',
]
_DATA_DEP_LITERAL = '"datafile_fruit-ninja.data"'
_WASM_LITERAL = 'locateFile("fruit-ninja.wasm")'
_HTML_SCRIPT_LITERAL = '<script async src=fruit-ninja.js>'
_HTML_SPLASH_LITERAL = 'src=splash.webp'


def sha8(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()[:8]


def hashed_name(stem, digest, ext):
    return "{}-{}.{}".format(stem, digest, ext)


def read_text(path):
    with open(path, "r", encoding="utf-8", errors="surrogateescape") as f:
        return f.read()


def write_text(path, content):
    with open(path, "w", encoding="utf-8", errors="surrogateescape") as f:
        f.write(content)


def find_existing_hashed(out_dir, stem, ext):
    """Return [(digest, name, mtime), ...] for files stem-<8hex>.ext in out_dir."""
    pat = re.compile(r"^{}-([0-9a-f]{{8}})\.{}$".format(re.escape(stem), re.escape(ext)))
    out = []
    for name in os.listdir(out_dir):
        m = pat.match(name)
        if m:
            full = os.path.join(out_dir, name)
            out.append((m.group(1), name, os.path.getmtime(full)))
    return out


def resolve_asset(out_dir, stem, ext):
    """
    Ensure a content-hashed stem-<sha8>.ext exists in out_dir and return its 8-hex
    digest (or None if the asset is entirely absent).

    - If the freshly-emitted canonical stem.ext exists: hash it and turn it into the
      hashed name (rename; or, if an identical-hash file already exists, just drop
      the duplicate canonical). This is the "changed / full build" path.
    - Else (canonical absent -> unchanged on this incremental build): reuse the
      existing stem-<sha8>.ext. No copy. This is the fix for the .data 404.
    """
    canonical = os.path.join(out_dir, "{}.{}".format(stem, ext))
    if os.path.isfile(canonical):
        digest = sha8(canonical)
        hashed = os.path.join(out_dir, hashed_name(stem, digest, ext))
        if os.path.abspath(hashed) != os.path.abspath(canonical):
            if os.path.isfile(hashed):
                os.remove(canonical)          # identical content already hashed
            else:
                os.rename(canonical, hashed)
        return digest, "fresh"

    existing = find_existing_hashed(out_dir, stem, ext)
    if not existing:
        return None, "missing"
    if len(existing) == 1:
        return existing[0][0], "reused"
    # ambiguous (multiple hashed copies left over) -> keep the newest; prune later
    newest = max(existing, key=lambda t: t[2])
    return newest[0], "reused-newest"


def prune_stale(out_dir, stem, ext, keep_digest):
    pat = re.compile(r"^{}-([0-9a-f]{{8}})\.{}$".format(re.escape(stem), re.escape(ext)))
    removed = []
    for name in sorted(os.listdir(out_dir)):
        m = pat.match(name)
        if m and m.group(1) != keep_digest:
            os.remove(os.path.join(out_dir, name))
            removed.append(name)
    return removed


def replace_report(content, old, new, label):
    count = content.count(old)
    if count == 0:
        print("[web-hash] WARNING: literal not found ({}): {!r}".format(label, old))
        return content
    print("[web-hash] {}: replaced {} occurrence(s) of {!r}".format(label, count, old))
    return content.replace(old, new)


def main():
    if len(sys.argv) < 2:
        print("Usage: web-hash-assets.py <build/web>", file=sys.stderr)
        sys.exit(1)
    out_dir = sys.argv[1]
    if not os.path.isdir(out_dir):
        print("ERROR: not a directory: {}".format(out_dir), file=sys.stderr)
        sys.exit(1)

    def p(name):
        return os.path.join(out_dir, name)

    print("[web-hash] starting in: {}".format(os.path.abspath(out_dir)))

    # --- Step 1: resolve content hashes for wasm + data + splash --------------
    # (each becomes / stays fruit-ninja-<sha8>.ext; digest reused if unchanged)
    wasm_digest, wasm_how = resolve_asset(out_dir, STEM, "wasm")
    print("[web-hash] wasm   digest={} ({})".format(wasm_digest, wasm_how))
    data_digest, data_how = resolve_asset(out_dir, STEM, "data")
    print("[web-hash] data   digest={} ({})".format(data_digest, data_how))
    splash_digest, splash_how = resolve_asset(out_dir, "splash", "webp")
    print("[web-hash] splash digest={} ({})".format(splash_digest, splash_how))

    if data_digest is None:
        print("[web-hash] ERROR: no fruit-ninja.data (canonical or hashed) found -- "
              "the runtime will 404. Run a clean web build.", file=sys.stderr)

    # --- Step 2: rewrite fruit-ninja.js references, then hash it ---------------
    # The JS is re-emitted (with un-hashed refs) every build, so rewrite always
    # runs against the fresh canonical file using the digests resolved above.
    js_digest = None
    js_src = p("{}.js".format(STEM))
    if os.path.isfile(js_src):
        js = read_text(js_src)
        if data_digest is not None:
            new_data = hashed_name(STEM, data_digest, "data")
            for lit in _DATA_LITERALS:
                js = replace_report(js, lit, lit.replace("fruit-ninja.data", new_data), "js")
            js = replace_report(js, _DATA_DEP_LITERAL,
                                _DATA_DEP_LITERAL.replace("fruit-ninja.data", new_data), "js")
        if wasm_digest is not None:
            new_wasm = hashed_name(STEM, wasm_digest, "wasm")
            js = replace_report(js, _WASM_LITERAL,
                                _WASM_LITERAL.replace("fruit-ninja.wasm", new_wasm), "js")
        # Belt-and-suspenders: catch any remaining bare references emcc may emit
        # in future versions (idempotent -- already-hashed names won't match).
        if data_digest is not None:
            stray = re.findall(r'"fruit-ninja\.data"', js)
            if stray:
                js = js.replace('"fruit-ninja.data"', '"{}"'.format(hashed_name(STEM, data_digest, "data")))
                print("[web-hash] js: rewrote {} stray bare fruit-ninja.data ref(s)".format(len(stray)))
        write_text(js_src, js)
        js_digest = sha8(js_src)
        os.rename(js_src, p(hashed_name(STEM, js_digest, "js")))
        print("[web-hash] js   fruit-ninja.js -> {}".format(hashed_name(STEM, js_digest, "js")))
    else:
        existing_js = find_existing_hashed(out_dir, STEM, "js")
        if existing_js:
            js_digest = max(existing_js, key=lambda t: t[2])[0]
            print("[web-hash] js   canonical absent -> reusing {}".format(
                hashed_name(STEM, js_digest, "js")))
        else:
            print("[web-hash] WARNING: no fruit-ninja.js (canonical or hashed) found")

    # --- Step 3: rewrite fruit-ninja.html (stays unhashed) --------------------
    html = p("{}.html".format(STEM))
    if os.path.isfile(html):
        h = read_text(html)
        if js_digest is not None:
            h = replace_report(h, _HTML_SCRIPT_LITERAL,
                               _HTML_SCRIPT_LITERAL.replace("fruit-ninja.js",
                                                            hashed_name(STEM, js_digest, "js")), "html")
        if splash_digest is not None:
            h = replace_report(h, _HTML_SPLASH_LITERAL,
                               _HTML_SPLASH_LITERAL.replace("splash.webp",
                                                            hashed_name("splash", splash_digest, "webp")), "html")
        if wasm_digest is not None and "__FN_BUILD__" in h:
            h = h.replace("__FN_BUILD__", wasm_digest)
            print("[web-hash] html: __FN_BUILD__ -> {}".format(wasm_digest))
        write_text(html, h)
        print("[web-hash] html: rewrites applied -> fruit-ninja.html")
    else:
        print("[web-hash] WARNING: fruit-ninja.html not found")

    # --- Step 4: prune stale hashes (keep current) ----------------------------
    for stem, ext, dig in [(STEM, "wasm", wasm_digest), (STEM, "data", data_digest),
                           (STEM, "js", js_digest), ("splash", "webp", splash_digest)]:
        if dig is not None:
            for name in prune_stale(out_dir, stem, ext, dig):
                print("[web-hash] pruned stale: {}".format(name))

    # --- Verify: the served JS must not reference any un-hashed asset ----------
    ok = True
    if js_digest is not None:
        served_js = p(hashed_name(STEM, js_digest, "js"))
        if os.path.isfile(served_js):
            content = read_text(served_js)
            for bad in ('"fruit-ninja.data"', "locateFile(\"fruit-ninja.wasm\")"):
                if bad in content:
                    print("[web-hash] ERROR: served JS still references un-hashed asset: {}".format(bad),
                          file=sys.stderr)
                    ok = False

    print("[web-hash] done.")
    print("[web-hash] current set:")
    for name in sorted(os.listdir(out_dir)):
        if re.search(r"-[0-9a-f]{8}\.(js|wasm|data|webp)$", name) or name == "fruit-ninja.html":
            print("[web-hash]   {} ({} bytes)".format(name, os.path.getsize(p(name))))

    sys.exit(0 if ok else 2)


if __name__ == "__main__":
    main()
