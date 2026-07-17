#!/usr/bin/env python3
"""
tools/web/regen-shell.py -- Fast shell-only regen of build/web/index.html.

Usage:
    python3 tools/web/regen-shell.py [<build/web>]     (default: build/web)

Rewrites build/web/index.html straight from the CURRENT
src/platform/emscripten/shell.html, reusing the EXISTING hashed
fruit-ninja-<sha8>.{js,wasm,data} already on disk. Pure Python string
substitution -- no emcc, no Docker, no CMake. Runs in well under a second.

Why this exists
----------------
A shell.html-only edit (CSS/JS in the HTML shell -- overlays, progress bar,
fullscreen button, etc.) does not touch a single C++ file, so nothing about
the compiled wasm/js/data changes. But the normal pipeline only produces
index.html as a side effect of the full emcc link (`--shell-file shell.html`
substitutes emscripten's `{{{ ... }}}` placeholders at link time), which
means a 1-line HTML tweak still costs a ~1-2 minute Docker rebuild if you go
through tools/web/build.sh. This script skips straight to the substitution
step using the js/wasm/data that are ALREADY built and unchanged.

What `--shell-file` substitutes (emscripten 6.x, verified against a real
build/web/index.html): the literal text "{{{ SCRIPT }}}" appears TWICE in
shell.html, but only the SECOND occurrence is a real token --
    line ~24:   prose inside the top HTML comment, describing the
                placeholder ("Required Emscripten placeholder: {{{ SCRIPT }}}")
                -- NOT substituted by emcc (comments aren't parsed specially,
                but this text is never touched because emcc's shell-file
                substitution is a dumb literal replace and there's only ever
                been one `{{{ SCRIPT }}}` for it to expand in a real build;
                this script instead deliberately replaces only the LAST
                occurrence so the comment's mention is left untouched too).
    line ~1425: the real directive, right before </body>
      -> becomes  <script async src="fruit-ninja.js"></script>
      -> then web-hash-assets.py (CMake POST_BUILD) rewrites that literal to
         the content-hashed name, e.g. <script async src=fruit-ninja-75a3c88a.js></script>
No other `{{{...}}}` token exists anywhere in shell.html.

emcc also runs its HTML through a minifier (html-minifier) as part of the
normal link, so the real build/web/index.html is minified (attributes
unquoted where possible, whitespace collapsed, etc.). This script does NOT
reproduce that minification -- it emits shell.html verbatim (readable) with
just the one substitution applied. That is a harmless cosmetic difference:
browsers parse unminified HTML identically. If byte-for-byte parity with a
real emcc build matters, run the full rebuild-web.sh instead.

CAVEAT -- shell-only regen is valid ONLY when the existing js/wasm/data in
<build/web> already reflect the current C++ source (i.e. no .cpp/.h changed
since the last real build). This script does not check that for you beyond
requiring the hashed files to exist; it does not compare timestamps against
src/. A C++ change still needs the full tools/web/rebuild-web.sh (or
build.sh) pipeline -- this script only re-lays-out the HTML shell around
whatever wasm/js/data are already sitting in <build/web>.

Exit codes: 0 on success, 1 if <build/web> or shell.html is missing, 2 if no
fruit-ninja-*.js is found in <build/web> (nothing to wire the script tag to).
"""

import glob
import os
import sys

STEM = "fruit-ninja"
PLACEHOLDER = "{{{ SCRIPT }}}"


def find_hashed_js(build_dir):
    """Return the newest fruit-ninja-<sha8>.js in build_dir, or None."""
    candidates = glob.glob(os.path.join(build_dir, "{}-*.js".format(STEM)))
    if not candidates:
        # Fall back to an unhashed debug-build fruit-ninja.js if present.
        unhashed = os.path.join(build_dir, "{}.js".format(STEM))
        return unhashed if os.path.isfile(unhashed) else None
    candidates.sort(key=os.path.getmtime, reverse=True)
    return candidates[0]


def main():
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    build_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(repo_root, "build", "web")
    shell_path = os.path.join(repo_root, "src", "platform", "emscripten", "shell.html")

    if not os.path.isdir(build_dir):
        print("ERROR: not a directory: {}".format(build_dir), file=sys.stderr)
        sys.exit(1)
    if not os.path.isfile(shell_path):
        print("ERROR: shell.html not found: {}".format(shell_path), file=sys.stderr)
        sys.exit(1)

    js_path = find_hashed_js(build_dir)
    if js_path is None:
        print("ERROR: no {0}-<hash>.js (or unhashed {0}.js) found in {1} -- "
              "run a full build first (tools/web/rebuild-web.sh).".format(STEM, build_dir),
              file=sys.stderr)
        sys.exit(2)
    js_name = os.path.basename(js_path)

    with open(shell_path, "r", encoding="utf-8") as f:
        shell = f.read()

    # shell.html mentions the literal text "{{{ SCRIPT }}}" TWICE: once in the
    # top HTML comment (prose describing the placeholder) and once as the
    # real directive right before </body>. Only the LAST occurrence is the
    # real token emcc substitutes -- rsplit(..., 1) replaces only that one,
    # leaving the comment's mention untouched (harmless either way, but this
    # keeps the substitution exact rather than "coincidentally worked").
    count = shell.count(PLACEHOLDER)
    if count != 2:
        print("ERROR: expected exactly 2 occurrences of {!r} in shell.html "
              "(1 comment mention + 1 real token), found {}. shell.html's "
              "placeholder shape may have changed -- update this script."
              .format(PLACEHOLDER, count), file=sys.stderr)
        sys.exit(1)

    script_tag = '<script async src="{}"></script>'.format(js_name)
    head, _, tail = shell.rpartition(PLACEHOLDER)
    out = head + script_tag + tail

    out_path = os.path.join(build_dir, "index.html")
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(out)

    print("[regen-shell] shell.html -> {}".format(out_path))
    print("[regen-shell] wired script tag: {}".format(script_tag))
    print("[regen-shell] no emcc/Docker invoked -- existing wasm/data/js on disk are untouched.")
    print("[regen-shell] CAVEAT: only valid if the existing js/wasm/data are already "
          "current for this C++ source -- a C++ change still needs a full rebuild-web.sh.")


if __name__ == "__main__":
    main()
