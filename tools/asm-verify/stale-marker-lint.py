#!/usr/bin/env python3
"""Stale ASM-marker linter for FruitNinja port.

Scans src/ for source-side RE markers (ASM-verified, ASM-spec, TODO, DIFFERS,
Defunct) and checks each against the binary symbol table.

Verdicts per marker:
  OK          -- addr resolves to a symbol whose demangled name contains the
                 cited symbol string, AND 'v1.6.1' is present.
  STALE       -- addr resolves to a symbol whose name does NOT match the cited
                 symbol (address has been re-used or was always wrong).
  NO-VERSION  -- marker lacks 'v1.6.1' version tag.
  UNRESOLVED  -- addr not found as a known symbol start in the binary.

Usage:
    python tools/asm-verify/stale-marker-lint.py [--src <dir>] [--binary <path>]

Output:
    tmp/stale-markers/report.json  (machine-readable, full detail)
    stdout                         (ranked summary: STALE first)
"""
import argparse
import json
import pathlib
import re
import sys

try:
    import lief
except ImportError:
    sys.exit("ERROR: lief is required. Install with: pip install lief")

try:
    import itanium_demangler
    _HAVE_DEMANGLER = True
except ImportError:
    _HAVE_DEMANGLER = False
    print("WARNING: itanium-demangler not installed; symbol matching will use mangled names only.",
          file=sys.stderr)
    print("         Install with: pip install itanium-demangler", file=sys.stderr)


# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
SCRIPT_DIR   = pathlib.Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent.parent
BINARY_DEFAULT = PROJECT_ROOT / "FruitNinjaBada" / "Bin" / "FruitNinja.exe"
SRC_DIR      = PROJECT_ROOT / "src"
OUT_DIR      = PROJECT_ROOT / "tmp" / "stale-markers"


# ---------------------------------------------------------------------------
# Regex patterns
# ---------------------------------------------------------------------------
# Match any hex address like 0x001b07f0
_HEX_ADDR = r'0x[0-9a-fA-F]{6,10}'

# Patterns to extract (marker_kind, has_v161, cited_symbol, cited_addr) from a line.
# Each pattern is tried in order; first match wins.
#
# Group names: kind, sym (optional), addr, ver (optional sentinel)
#
# Note: some markers use '@0x' (no space) and some use '@ 0x' (with space).
# Some markers have '+0x<offset>' after the address (e.g. 'v1.6.1 IFile_Direct @ 0x001eb3b8 +0x14')
# -- we capture the base address only.

_PATTERNS = [
    # ASM-verified: <date> v1.6.1 <Symbol> @ 0x<addr>
    # e.g.: // ASM-verified: 2026-06-18 v1.6.1 Skeleton::BuildArrays @ 0x0023b6f0 (asm-inspector)
    (
        "ASM-verified",
        re.compile(
            r'ASM-verified:\s+'
            r'(?P<date>\S+)\s+'
            r'(?P<ver>v1\.6\.1)\s+'
            r'(?P<sym>[A-Za-z_][^\s@]+?)\s+'
            r'@\s*(?P<addr>' + _HEX_ADDR + r')'
        ),
    ),
    # ASM-verified: <date> binary @ 0x<addr>  (old form - no symbol, no version)
    # e.g.: // ASM-verified: 2026-04-29T00:00Z binary @ 0x001b07f0 (asm-inspector)
    (
        "ASM-verified",
        re.compile(
            r'ASM-verified:\s+'
            r'(?P<date>\S+)\s+'
            r'binary\s+'
            r'@\s*(?P<addr>' + _HEX_ADDR + r')'
        ),
    ),
    # ASM-verified: <Symbol> binary @ 0x<addr>  (another old form)
    # e.g.: // ASM-verified: MAMAudioController::LoadSound binary @ 0x0018c468
    (
        "ASM-verified",
        re.compile(
            r'ASM-verified:\s+'
            r'(?P<sym>[A-Za-z_][A-Za-z0-9_:~<>*&, ]+?)\s+'
            r'binary\s+'
            r'@\s*(?P<addr>' + _HEX_ADDR + r')'
        ),
    ),
    # ASM-spec v1.6.1 <Symbol> @ 0x<addr> / @0x<addr>
    # e.g.: // ASM-spec v1.6.1 BakedStringBox::SetGradient @0x0024566c:
    (
        "ASM-spec",
        re.compile(
            r'ASM-spec\s+'
            r'(?P<ver>v1\.6\.1)\s+'
            r'(?P<sym>[A-Za-z_][^\s@]+?)\s+'
            r'@\s*(?P<addr>' + _HEX_ADDR + r')'
        ),
    ),
    # ASM-spec for binary @ 0x<addr>  (old form without symbol)
    # e.g.: // ASM-spec for binary @ 0x00153f20 (re-analyst):
    (
        "ASM-spec",
        re.compile(
            r'ASM-spec\s+for\s+binary\s+'
            r'@\s*(?P<addr>' + _HEX_ADDR + r')'
        ),
    ),
    # TODO: v1.6.1 0x<addr> (<Symbol>)
    # e.g.: // TODO: v1.6.1 0x001CF534 (GameUpdate) — full InputSink class RE
    (
        "TODO",
        re.compile(
            r'TODO:\s+'
            r'(?P<ver>v1\.6\.1)\s+'
            r'(?P<addr>' + _HEX_ADDR + r')\s+'
            r'\((?P<sym>[A-Za-z_][A-Za-z0-9_:~<>*& ,]+?)\)'
        ),
    ),
    # TODO: v1.6.1 <Symbol> @0x<addr> / @ 0x<addr>
    # e.g.: // TODO: v1.6.1 PSPParticleManager::Draw @0x0013eccc
    (
        "TODO",
        re.compile(
            r'TODO:\s+'
            r'(?P<ver>v1\.6\.1)\s+'
            r'(?P<sym>[A-Za-z_][^\s@]+?)\s+'
            r'@\s*(?P<addr>' + _HEX_ADDR + r')'
        ),
    ),
    # DIFFERS: ... (v1.6.1 <Symbol> @0x<addr>) ...
    # e.g.: // DIFFERS: binary @ 0x00272dc8 hand-codes...
    # e.g.: // DIFFERS: original = X from DAT_addr (v1.6.1 <Symbol> @0x<addr>)
    (
        "DIFFERS",
        re.compile(
            r'DIFFERS:.*?'
            r'(?P<ver>v1\.6\.1)\s+'
            r'(?P<sym>[A-Za-z_][^\s@(]+?)\s+'
            r'@\s*(?P<addr>' + _HEX_ADDR + r')'
        ),
    ),
    # DIFFERS: binary @ 0x<addr>  (old form, no symbol, no version)
    (
        "DIFFERS",
        re.compile(
            r'DIFFERS:.*?'
            r'binary\s+'
            r'@\s*(?P<addr>' + _HEX_ADDR + r')'
        ),
    ),
    # Defunct: ... v1.6.1 <Symbol> @ 0x<addr>
    # e.g.: // Defunct: SetText -- no-op stub; v1.6.1 MenuButton::SetText @ 0x0019d4e0
    (
        "Defunct",
        re.compile(
            r'Defunct:.*?'
            r'(?P<ver>v1\.6\.1)\s+'
            r'(?P<sym>[A-Za-z_][^\s@(]+?)\s+'
            r'@\s*(?P<addr>' + _HEX_ADDR + r')'
        ),
    ),
    # Defunct: ... binary @ 0x<addr>  (old form, no symbol, no version)
    # e.g.: // Defunct: GetSaveRootDirectory — no-op stub; binary @ 0x0019ae64
    (
        "Defunct",
        re.compile(
            r'Defunct:.*?'
            r'binary\s+'
            r'@\s*(?P<addr>' + _HEX_ADDR + r')'
        ),
    ),
]


def _demangle(mangled: str) -> str:
    """Return demangled name, or mangled name on failure."""
    if not _HAVE_DEMANGLER:
        return mangled
    try:
        node = itanium_demangler.parse(mangled)
        if node is not None:
            return str(node)
    except Exception:
        pass
    return mangled


def _symbol_matches(cited_sym: str, binary_sym: str, demangled: str) -> bool:
    """Return True if the cited symbol is a reasonable match for binary_sym.

    Matching rules (any of):
    1. cited_sym is a substring of the demangled name (namespace-qualified or not).
    2. cited_sym == the unqualified name portion (last :: segment).
    3. The last segment of cited_sym matches the last segment of the demangled name.
    4. The mangled binary_sym contains a fragment derived from cited_sym.

    We strip ctor/dtor noise: '{ctor}', '{deleting dtor}', etc.
    """
    # Normalise both sides
    cited_clean = cited_sym.strip()
    dem_clean   = demangled

    # Strip argument lists from both for loose comparison
    cited_base = re.sub(r'\(.*', '', cited_clean).strip()
    dem_base   = re.sub(r'\(.*', '', dem_clean).strip()

    # Remove trailing whitespace and ::operator noise
    def _last_segment(s: str) -> str:
        """Extract the last :: segment, stripping template params."""
        s = re.sub(r'<[^>]*>', '', s)  # strip template args
        parts = s.rsplit('::', 1)
        return parts[-1].strip()

    cited_last = _last_segment(cited_base)
    dem_last   = _last_segment(dem_base)

    # 1. Full substring match (demangled contains cited_base)
    if cited_base and cited_base in dem_base:
        return True

    # 2. Last-segment exact match
    if cited_last and dem_last and cited_last == dem_last:
        return True

    # 3. Partial last-segment: cited is a prefix/suffix of dem_last
    if cited_last and dem_last:
        if dem_last.startswith(cited_last) or dem_last.endswith(cited_last):
            return True

    # 4. Cited matches the class name (e.g. 'Fruit' matches 'Fruit::Fruit()')
    #    Only accept if at least 4 chars to avoid false positives
    if len(cited_last) >= 4 and cited_last in dem_clean:
        return True

    return False


def load_binary_symbols(binary_path: pathlib.Path) -> tuple[dict, dict]:
    """Return (addr_to_mangled, addr_to_demangled) dicts from the ELF.

    addr_to_mangled: {int_addr: mangled_name}
    addr_to_demangled: {int_addr: demangled_name}

    Prefers FUNC-typed symbols over NOTYPE when both exist at the same address.
    Skips ARM mapping symbols ($a, $d, $t), FILE, and SECTION entries.
    """
    b = lief.parse(str(binary_path))
    if b is None:
        sys.exit(f"ERROR: lief could not parse {binary_path}")

    addr_to_mangled: dict[int, str] = {}
    addr_to_demangled: dict[int, str] = {}

    # The port's source-side markers use GHIDRA addresses, and Ghidra loads this
    # ELF at image_base 0x10000 (verified via GhidraMCP get_current_program_info:
    # GameOverScreen::Update is 0x176c80 in the raw ELF / LIEF but 0x00186c80 in
    # Ghidra and in every marker + asm-verify disasm). So Ghidra_addr = LIEF_value
    # + 0x10000. Without this offset the linter mis-reports every marker as stale.
    GHIDRA_IMAGE_BASE = 0x10000

    for sym in b.symbols:
        addr = sym.value + GHIDRA_IMAGE_BASE
        if addr == 0:
            continue
        name = sym.name
        if not name:
            continue
        # Skip ARM mapping symbols and ELF special symbols
        if name.startswith('$') or name.startswith('.'):
            continue
        # Skip FILE / SECTION type entries
        sym_type_str = str(sym.type)
        if sym_type_str in ('TYPE.FILE', 'TYPE.SECTION'):
            continue

        if addr not in addr_to_mangled:
            addr_to_mangled[addr] = name
            addr_to_demangled[addr] = _demangle(name)
        else:
            # Prefer FUNC over NOTYPE
            if sym_type_str == 'TYPE.FUNC' and str(b.symbols) != 'TYPE.FUNC':
                addr_to_mangled[addr] = name
                addr_to_demangled[addr] = _demangle(name)

    return addr_to_mangled, addr_to_demangled


def scan_sources(src_dir: pathlib.Path) -> list[dict]:
    """Scan all .cpp/.h files under src_dir for RE markers.

    Returns list of dicts:
      file, line, raw_line, kind, has_version, cited_sym (or None), cited_addr (int)
    """
    results = []
    extensions = {'.cpp', '.h', '.cc', '.cxx'}

    for fpath in sorted(src_dir.rglob('*')):
        if fpath.suffix not in extensions:
            continue
        try:
            text = fpath.read_text(encoding='utf-8', errors='replace')
        except Exception:
            continue

        for lineno, raw in enumerate(text.splitlines(), 1):
            for kind, pat in _PATTERNS:
                m = pat.search(raw)
                if not m:
                    continue

                addr_str = m.group('addr')
                addr_int = int(addr_str, 16)

                cited_sym = None
                try:
                    cited_sym = m.group('sym').strip()
                except IndexError:
                    pass

                has_version = False
                try:
                    if m.group('ver'):
                        has_version = True
                except IndexError:
                    pass
                # Also check raw line for v1.6.1 token (catches DIFFERS/Defunct
                # old forms where we may still have the version elsewhere)
                if not has_version and 'v1.6.1' in raw:
                    has_version = True

                results.append({
                    'file':        str(fpath.relative_to(src_dir.parent)),
                    'line':        lineno,
                    'raw_line':    raw.strip(),
                    'kind':        kind,
                    'has_version': has_version,
                    'cited_sym':   cited_sym,
                    'cited_addr':  addr_int,
                    'addr_str':    addr_str,
                })
                break   # stop at first matching pattern for this line

    return results


def classify(markers: list[dict],
             addr_to_mangled: dict,
             addr_to_demangled: dict) -> list[dict]:
    """Add 'verdict', 'binary_sym', 'binary_demangled' to each marker dict."""
    for m in markers:
        addr = m['cited_addr']
        if addr not in addr_to_mangled:
            m['verdict']           = 'UNRESOLVED'
            m['binary_sym']        = None
            m['binary_demangled']  = None
            # Still flag NO-VERSION if applicable
            if not m['has_version']:
                m['verdict'] = 'NO-VERSION+UNRESOLVED'
        else:
            mangled   = addr_to_mangled[addr]
            demangled = addr_to_demangled[addr]
            m['binary_sym']       = mangled
            m['binary_demangled'] = demangled

            cited_sym = m.get('cited_sym')

            if not m['has_version']:
                # Could still be STALE too, but NO-VERSION is the top flag
                if cited_sym and not _symbol_matches(cited_sym, mangled, demangled):
                    m['verdict'] = 'NO-VERSION+STALE'
                else:
                    m['verdict'] = 'NO-VERSION'
            elif not cited_sym:
                # Has version but no symbol in the marker (e.g. 'binary @ 0x...' with v1.6.1)
                m['verdict'] = 'OK-NO-SYM'
            elif _symbol_matches(cited_sym, mangled, demangled):
                m['verdict'] = 'OK'
            else:
                m['verdict'] = 'STALE'

    return markers


_VERDICT_ORDER = {
    'STALE':               0,
    'NO-VERSION+STALE':    1,
    'NO-VERSION':          2,
    'NO-VERSION+UNRESOLVED': 3,
    'UNRESOLVED':          4,
    'OK-NO-SYM':           5,
    'OK':                  6,
}


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--src',    default=str(SRC_DIR),
                    help='Source directory to scan (default: src/)')
    ap.add_argument('--binary', default=str(BINARY_DEFAULT),
                    help='Path to FruitNinja ELF binary')
    ap.add_argument('--out',    default=str(OUT_DIR / 'report.json'),
                    help='Output JSON path')
    args = ap.parse_args()

    binary_path = pathlib.Path(args.binary)
    src_dir     = pathlib.Path(args.src)
    out_path    = pathlib.Path(args.out)

    if not binary_path.exists():
        sys.exit(f"ERROR: binary not found: {binary_path}")
    if not src_dir.is_dir():
        sys.exit(f"ERROR: src dir not found: {src_dir}")

    print(f"Loading binary symbols from {binary_path.name} ...", flush=True)
    addr_to_mangled, addr_to_demangled = load_binary_symbols(binary_path)
    print(f"  {len(addr_to_mangled)} unique symbol addresses loaded.")

    print(f"Scanning {src_dir} for RE markers ...", flush=True)
    markers = scan_sources(src_dir)
    print(f"  {len(markers)} markers found.")

    print("Classifying ...", flush=True)
    markers = classify(markers, addr_to_mangled, addr_to_demangled)

    # Deduplicate: same (file, line) => keep first match (already done by break above)
    # but also deduplicate across multiple pattern matches for the same physical line
    seen_file_line = set()
    deduped = []
    for m in markers:
        key = (m['file'], m['line'])
        if key not in seen_file_line:
            seen_file_line.add(key)
            deduped.append(m)
    markers = deduped

    # Sort by verdict priority then file+line
    markers.sort(key=lambda m: (_VERDICT_ORDER.get(m['verdict'], 99), m['file'], m['line']))

    # Write JSON report
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open('w', encoding='utf-8') as f:
        json.dump({'markers': markers}, f, indent=2)
    print(f"\nFull report: {out_path}")

    # -----------------------------------------------------------------------
    # Stdout summary
    # -----------------------------------------------------------------------
    from collections import Counter
    verdict_counts = Counter(m['verdict'] for m in markers)

    print("\n" + "=" * 70)
    print("STALE-MARKER LINT SUMMARY")
    print("=" * 70)
    print(f"  Total markers scanned : {len(markers)}")
    for v in sorted(_VERDICT_ORDER, key=lambda x: _VERDICT_ORDER[x]):
        c = verdict_counts.get(v, 0)
        if c:
            print(f"  {v:<28}: {c}")
    print()

    # Show STALE and NO-VERSION+STALE rows first
    stale_rows = [m for m in markers if 'STALE' in m['verdict']]
    if stale_rows:
        print(f"--- STALE ({len(stale_rows)}) ---")
        for m in stale_rows[:40]:
            sym_cited = m['cited_sym'] or '(none)'
            sym_bin   = m['binary_demangled'] or m['binary_sym'] or '?'
            # Trim long symbols
            if len(sym_cited) > 50:
                sym_cited = sym_cited[:47] + '...'
            if len(sym_bin) > 60:
                sym_bin = sym_bin[:57] + '...'
            print(f"  [{m['verdict']}] {m['file']}:{m['line']}")
            print(f"    cited:  {sym_cited} @ {m['addr_str']}")
            print(f"    binary: {sym_bin}")
        if len(stale_rows) > 40:
            print(f"  ... and {len(stale_rows) - 40} more (see report.json)")
        print()

    # Show NO-VERSION rows (up to 20)
    noversion_rows = [m for m in markers if m['verdict'].startswith('NO-VERSION')]
    if noversion_rows:
        print(f"--- NO-VERSION ({len(noversion_rows)}) (first 20) ---")
        for m in noversion_rows[:20]:
            sym_cited = m['cited_sym'] or '(none)'
            if len(sym_cited) > 55:
                sym_cited = sym_cited[:52] + '...'
            print(f"  {m['file']}:{m['line']}  [{m['verdict']}]  {sym_cited} @ {m['addr_str']}")
        if len(noversion_rows) > 20:
            print(f"  ... and {len(noversion_rows) - 20} more")
        print()

    print(f"Full detail in: {out_path}")


if __name__ == '__main__':
    main()
