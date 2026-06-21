#!/usr/bin/env python3
"""Drift check for tools/asm-verify/verify-sources.cmake.

verify-sources.cmake is a hand-maintained list of the .cpp files that the
asm-verify cross-build compiles. It is a CURATED subset (only files known to
cross-compile), so not every src/ file belongs -- but two failure modes drift
in silently:

  STALE   : a listed file no longer exists on disk (renamed/deleted/moved).
            This HARD-BREAKS the cross-build, so it is an ERROR (exit 1).
  UNLISTED: a portable src/ .cpp that is not in the list. This is informational
            (a candidate to add once it cross-compiles), NOT an error.

Run standalone, or as a non-fatal preflight before run.sh:

    python3 tools/asm-verify/checks/check-sources-drift.py

Exit status: 1 if any STALE entry is found, else 0.
"""
import os
import pathlib
import re
import sys

ASM_VERIFY_DIR = pathlib.Path(__file__).resolve().parent.parent
PROJECT_ROOT   = ASM_VERIFY_DIR.parent.parent
CMAKE = ASM_VERIFY_DIR / "verify-sources.cmake"

# CMake var -> directory, for expanding ${_VAR}/... entries.
VARS = {
    "_PROJECT_ROOT": PROJECT_ROOT,
    "_ASM_VERIFY":   ASM_VERIFY_DIR,
}

# Files NOT expected in the list (platform glue + entry points + port-only).
# Mirrors the symbol-diff exclusion convention documented in CLAUDE.md.
EXCLUDE_SUFFIXES = ("SDL.cpp", "Posix.cpp", "Win32.cpp")
EXCLUDE_DIRS = (
    PROJECT_ROOT / "src" / "platform" / "sdl",
    PROJECT_ROOT / "src" / "platform" / "posix",
    PROJECT_ROOT / "src" / "platform" / "win32",
)
EXCLUDE_NAMES = ("main.cpp", "mainEmscripten.cpp", "DebugFlags.cpp")
# Port-only font path (FreeType/TTF) has no binary counterpart.
EXCLUDE_NAME_SUBSTR = ("TTF", "FreeType")


def listed_paths():
    """Resolve every "${_VAR}/..." entry in the set(VERIFY_SOURCES ...) block."""
    text = CMAKE.read_text(encoding="utf-8", errors="replace")
    out = []
    for raw in re.findall(r'"(\$\{[^"]+)"', text):
        m = re.match(r"\$\{(\w+)\}/(.*)", raw)
        if not m:
            continue
        var, rel = m.group(1), m.group(2)
        base = VARS.get(var)
        if base is None:
            sys.stderr.write("warn: unknown cmake var ${%s} in %s\n" % (var, raw))
            continue
        out.append((raw, (base / rel)))
    return out


def is_excluded(p: pathlib.Path) -> bool:
    name = p.name
    if name in EXCLUDE_NAMES:
        return True
    if name.endswith(EXCLUDE_SUFFIXES):
        return True
    if any(s in name for s in EXCLUDE_NAME_SUBSTR):
        return True
    for d in EXCLUDE_DIRS:
        try:
            p.relative_to(d)
            return True
        except ValueError:
            pass
    return False


def main():
    if not CMAKE.exists():
        sys.exit("error: %s not found" % CMAKE)

    listed = listed_paths()
    listed_resolved = {p.resolve() for _, p in listed}

    # STALE: listed but missing on disk.
    stale = [(raw, p) for raw, p in listed if not p.exists()]

    # UNLISTED: portable src/ .cpp not in the list.
    src_root = PROJECT_ROOT / "src"
    unlisted = []
    for p in sorted(src_root.rglob("*.cpp")):
        if is_excluded(p):
            continue
        if p.resolve() not in listed_resolved:
            unlisted.append(p)

    print("verify-sources.cmake: %d listed, %d stale, %d unlisted portable src .cpp"
          % (len(listed), len(stale), len(unlisted)))

    if stale:
        print("\nSTALE (listed but missing on disk -- breaks cross-build):")
        for raw, p in stale:
            print("  %s" % raw)

    if unlisted:
        print("\nUNLISTED (portable src .cpp not yet in the list -- candidates):")
        for p in unlisted:
            print("  %s" % p.relative_to(PROJECT_ROOT).as_posix())

    # Only STALE entries are an error; UNLISTED is informational.
    sys.exit(1 if stale else 0)


if __name__ == "__main__":
    main()
