#!/usr/bin/env python3
"""Case-mismatch check for asset-path string constants.

Catches the poweruplist.xml class of bug: a port source literal that only
differs from the binary's real string constant by LETTER CASE. On the
case-insensitive Windows dev box these load fine, but on case-sensitive
filesystems (web/emscripten, webOS, HLE) they silently fail -> missing
textures / unparsed XML / no sounds.

Method:
  1. Extract every NUL-terminated printable-ASCII run from the binary's
     data/rodata sections (LIEF; readelf is unreliable on this ELF).
  2. Extract double-quoted literals from src/ that look like asset paths
     (have a known asset extension, or a '/' + basename shape).
  3. For each port literal:
       - exact byte match in binary  -> OK (silent)
       - case-insensitive match only -> CASE MISMATCH (report binary's casing)
       - no match at all             -> UNMATCHED (info; port-specific or new)

Durable output: tmp/string-case-check/report.json (source of truth).
Stdout: ranked human/LLM summary (mismatches first).

Usage: python tools/asm-verify/string-case-check.py [--all]
  --all   also list UNMATCHED literals (noisy; default hides them)
"""
import json
import os
import pathlib
import re
import sys

import lief

ASM_VERIFY_DIR = pathlib.Path(__file__).resolve().parent
PROJECT_ROOT = ASM_VERIFY_DIR.parent.parent
BINARY = pathlib.Path(os.environ.get(
    "ASM_VERIFY_BINARY",
    PROJECT_ROOT / "FruitNinjaBada" / "Bin" / "FruitNinja.exe"))
SRC_DIR = PROJECT_ROOT / "src"
OUT_DIR = PROJECT_ROOT / "tmp" / "string-case-check"

# Asset-ish extensions the game actually loads. A literal ending in one of
# these (case-insensitive) is treated as a resource path worth checking.
ASSET_EXTS = (
    ".xml", ".tex", ".mad", ".mmd", ".png", ".jpg", ".jpeg",
    ".fnt", ".ttf", ".wav", ".ogg", ".raw", ".obj", ".gltf", ".bin",
)

# Directory prefixes that mark a resource path even without an extension
# (e.g. sound names loaded as "sfx/whoosh"), plus bare-name loaders.
PATH_HINT = re.compile(r"(^|/)(xml|tex|snd|sfx|music|font|fonts|gfx|data|res|shaders?)/", re.I)

# A C/C++ double-quoted literal (no escaped-quote handling needed for paths).
LITERAL = re.compile(r'"([^"\\\n]{2,120})"')


def extract_binary_strings(path):
    """Return dict: lower(str) -> set of real-case strings present in binary."""
    bin_ = lief.parse(str(path))
    by_lower = {}
    exact = set()
    for sec in bin_.sections:
        content = bytes(sec.content)
        if not content:
            continue
        # split on any non-printable byte; keep printable ASCII runs
        run = bytearray()
        for b in content:
            if 0x20 <= b < 0x7F:
                run.append(b)
            else:
                if len(run) >= 2:
                    s = run.decode("ascii")
                    exact.add(s)
                    by_lower.setdefault(s.lower(), set()).add(s)
                run = bytearray()
        if len(run) >= 2:
            s = run.decode("ascii")
            exact.add(s)
            by_lower.setdefault(s.lower(), set()).add(s)
    return exact, by_lower


def looks_like_asset(lit):
    low = lit.lower()
    if low.endswith(ASSET_EXTS):
        return True
    if PATH_HINT.search(lit):
        return True
    return False


def iter_port_literals():
    for p in SRC_DIR.rglob("*"):
        if p.suffix not in (".cpp", ".h", ".hpp", ".cc"):
            continue
        try:
            text = p.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for i, line in enumerate(text.splitlines(), 1):
            stripped = line.lstrip()
            if stripped.startswith("//") or stripped.startswith("*"):
                continue  # skip comment lines (markers cite old casings on purpose)
            if stripped.startswith("#include") or stripped.startswith("#import"):
                continue  # #include "foo.h" is not an asset path
            for m in LITERAL.finditer(line):
                lit = m.group(1)
                if "%" in lit:
                    continue  # runtime-built path (sprintf) -> no static binary string
                if lit.lower().endswith((".h", ".hpp", ".cc", ".cpp")):
                    continue  # C++ source header, not an asset
                if looks_like_asset(lit):
                    yield lit, p.relative_to(PROJECT_ROOT).as_posix(), i


# A binary string is treated as an asset path if it is filename/path-shaped
# (only path-safe chars) AND has a known asset extension or a resource dir hint.
BIN_PATH_CHARS = re.compile(r"^[A-Za-z0-9_./-]{4,120}$")


def binary_asset_strings(exact):
    out = []
    for s in exact:
        if not BIN_PATH_CHARS.match(s):
            continue
        low = s.lower()
        if low.endswith(ASSET_EXTS) or PATH_HINT.search(s):
            out.append(s)
    return out


def index_port_literals():
    """lower(literal) -> list of (literal, file, line) for ALL port literals."""
    idx = {}
    for p in SRC_DIR.rglob("*"):
        if p.suffix not in (".cpp", ".h", ".hpp", ".cc"):
            continue
        try:
            text = p.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        rel = p.relative_to(PROJECT_ROOT).as_posix()
        for i, line in enumerate(text.splitlines(), 1):
            stripped = line.lstrip()
            if stripped.startswith("//") or stripped.startswith("*"):
                continue
            if stripped.startswith("#include") or stripped.startswith("#import"):
                continue
            for m in LITERAL.finditer(line):
                lit = m.group(1)
                idx.setdefault(lit.lower(), []).append((lit, rel, i))
    return idx


def binary_driven_scan(exact):
    """For each asset string in the binary, check the port's casing.

    Authoritative direction: the binary is ground truth. Returns
    (case_bugs, missing) where case_bugs = binary string the port references
    only with wrong casing.
    """
    port_idx = index_port_literals()
    case_bugs = []
    missing = []
    for s in sorted(binary_asset_strings(exact)):
        hits = port_idx.get(s.lower())
        if not hits:
            missing.append(s)
            continue
        if any(lit == s for lit, _, _ in hits):
            continue  # port references binary's exact casing somewhere -> OK
        # port references it only with different casing -> real case bug
        case_bugs.append({
            "binary": s,
            "port": [{"literal": lit, "file": f, "line": ln} for lit, f, ln in hits],
        })
    return case_bugs, missing


def main():
    show_all = "--all" in sys.argv
    exact, by_lower = extract_binary_strings(BINARY)
    bin_case_bugs, bin_missing = binary_driven_scan(exact)

    mismatches = []   # port casing != binary casing
    unmatched = []    # not in binary at all (any case)
    seen = {}         # dedup identical (lit,file,line)
    for lit, file, line in iter_port_literals():
        key = (lit, file, line)
        if key in seen:
            continue
        seen[key] = True
        if lit in exact:
            continue
        alts = by_lower.get(lit.lower())
        if alts:
            mismatches.append({
                "port": lit, "binary": sorted(alts),
                "file": file, "line": line,
            })
        else:
            unmatched.append({"port": lit, "file": file, "line": line})

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    report = {
        "binary": str(BINARY),
        "binary_string_count": len(exact),
        # authoritative: binary asset strings the port loads with wrong casing
        "binary_driven_case_bugs": bin_case_bugs,
        "binary_assets_not_referenced": sorted(bin_missing),
        # port-driven (secondary): port literals not found in binary
        "case_mismatches": mismatches,
        "unmatched": unmatched,
    }
    (OUT_DIR / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")

    print("=== string-case-check ===")
    print("binary strings: %d   binary asset paths: %d   port asset literals: %d"
          % (len(exact), len(bin_case_bugs) + len(bin_missing) + 0, len(seen)))
    print()
    print("[BINARY-DRIVEN] authoritative: binary asset string vs port casing")
    if bin_case_bugs:
        print("CASE BUG (%d) -- port loads a binary asset with WRONG case:" % len(bin_case_bugs))
        for b in bin_case_bugs:
            print("  binary: \"%s\"" % b["binary"])
            for h in b["port"]:
                print("    port %s:%d  \"%s\"" % (h["file"], h["line"], h["literal"]))
    else:
        print("CASE BUG: none")
    print("  (binary asset strings the port never references statically: %d"
          " -- runtime-built or unported; see report.json)" % len(bin_missing))
    print()
    print("[PORT-DRIVEN] secondary: port literal vs binary (may false-positive on runtime paths)")
    if mismatches:
        print("CASE MISMATCH (%d):" % len(mismatches))
        for m in mismatches:
            print("  %s:%d" % (m["file"], m["line"]))
            print("    port:   \"%s\"" % m["port"])
            print("    binary: %s" % ", ".join('"%s"' % b for b in m["binary"]))
    else:
        print("CASE MISMATCH: none")
    print()
    print("UNMATCHED (%d): port literals with no binary string in any case"
          % len(unmatched))
    if show_all:
        for u in sorted(unmatched, key=lambda x: x["file"]):
            print("  %s:%d  \"%s\"" % (u["file"], u["line"], u["port"]))
    else:
        print("  (re-run with --all to list; many are port-specific/SDL paths)")
    print()
    print("report: %s" % (OUT_DIR / "report.json"))
    return 1 if (bin_case_bugs or mismatches) else 0


if __name__ == "__main__":
    sys.exit(main())
