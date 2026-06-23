#!/usr/bin/env python3
"""Stale ASM-marker linter for FruitNinja port.

Scans src/ for source-side RE markers (ASM-verified, ASM-spec, TODO, DIFFERS,
Defunct) and checks each against the binary symbol table.

Verdicts per marker:
  OK              -- addr resolves to a symbol whose demangled name contains the
                     cited symbol string, AND 'v1.6.1' is present.
  STALE-MISMATCH  -- cited symbol is found in the binary at exactly ONE other
                     address addr2 != cited_addr; the marker address is wrong.
                     report.json['correct_addr'] gives the correct Ghidra address.
  STALE-AMBIGUOUS -- cited symbol matches MULTIPLE binary addresses; cannot
                     auto-fix. Manual review required.
  STALE           -- addr resolves to a symbol whose name does NOT match the cited
                     symbol AND the cited symbol is not uniquely findable.
  NO-VERSION      -- marker lacks 'v1.6.1' version tag; addr resolves OK.
  MID-SYMBOL      -- addr is not a symbol start, but falls within the [start,
                     start+size) range of a known function/object; the address
                     is valid (deliberate mid-function reference). report.json
                     contains 'containing_sym' and 'containing_addr'.
  UNRESOLVED      -- addr not found as a known symbol start, and NOT within any
                     symbol's size range. Truly unknown.
  OK-NO-SYM       -- has v1.6.1 but no symbol name in the marker (old 'binary @')

All NO-VERSION variants carry a sub-verdict:
  NO-VERSION       -- addr resolves to a matching symbol (OK except missing tag)
  NO-VERSION+STALE -- addr resolves but symbol name doesn't match
  NO-VERSION+MID-SYMBOL -- addr is inside a function body (valid mid-func ref)
  NO-VERSION+UNRESOLVED -- addr not found at all

Addresses use GHIDRA convention throughout: Ghidra_addr = LIEF_value + 0x10000.

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
    Dtor variants: '~Foo' is treated as matching '{dtor}', '{deleting dtor}',
    '{base dtor}' for the same class.
    """
    # Normalise both sides
    cited_clean = cited_sym.strip()
    dem_clean   = demangled

    # Strip argument lists from both for loose comparison
    cited_base = re.sub(r'\(.*', '', cited_clean).strip()
    dem_base   = re.sub(r'\(.*', '', dem_clean).strip()

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

    # 5. Dtor match: '~Foo' in cited should match '{dtor}', '{deleting dtor}',
    #    '{base dtor}' in the same class. Extract 'Foo' from '~Foo' and check
    #    that the demangled name starts with 'Foo::' and contains 'dtor'.
    if cited_last.startswith('~'):
        class_name = cited_last[1:]
        if class_name and 'dtor' in dem_last:
            # Extract class portion from demangled (before the last ::)
            dem_parts = re.sub(r'\(.*', '', dem_clean).rsplit('::', 2)
            if len(dem_parts) >= 2:
                dem_class = dem_parts[-2].strip()
                # Strip template params from class name
                dem_class = re.sub(r'<[^>]*>', '', dem_class).strip()
                if dem_class == class_name or dem_class.endswith('::' + class_name):
                    return True

    # 6. Compiler-generated names: 'T_NNNN' matches 'T.NNNN' (GCC emits '.'
    #    which cannot appear in source identifiers so ports use '_' instead).
    if re.match(r'^T_\d+$', cited_last):
        dot_form = cited_last.replace('_', '.', 1)
        if dot_form == binary_sym or dot_form == demangled:
            return True

    return False


def _forward_lookup(cited_sym: str,
                    addr_to_mangled: dict,
                    addr_to_demangled: dict) -> list:
    """FORWARD check: find all binary addresses where cited_sym matches.

    Returns list of (addr, demangled) pairs sorted by addr.
    An empty list means the symbol wasn't found anywhere in the binary.
    """
    if not cited_sym:
        return []
    results = []
    seen = set()
    for addr, mangled in addr_to_mangled.items():
        dem = addr_to_demangled.get(addr, mangled)
        if _symbol_matches(cited_sym, mangled, dem):
            if addr not in seen:
                seen.add(addr)
                results.append((addr, dem))
    results.sort(key=lambda x: x[0])
    return results


def load_binary_symbols(binary_path: pathlib.Path):
    """Return (addr_to_mangled, addr_to_demangled, sym_ranges) from the ELF.

    addr_to_mangled:  {int_addr: mangled_name}
    addr_to_demangled:{int_addr: demangled_name}
    sym_ranges:       sorted list of (start_addr, end_addr, mangled_name) for
                      all symbols with size > 0. Used for CONTAINMENT check.

    All addresses use GHIDRA convention: LIEF_value + 0x10000.
    """
    b = lief.parse(str(binary_path))
    if b is None:
        sys.exit(f"ERROR: lief could not parse {binary_path}")

    # Ghidra loads this ELF at image_base 0x10000. Every source-side marker and
    # every asm-verify disasm uses Ghidra addresses. LIEF reports raw ELF .st_value
    # fields, so we must add 0x10000 to convert.
    # Validated anchors (Ghidra convention, confirmed via GhidraMCP):
    #   GameOverScreen::Update  @ 0x00186c80  (LIEF 0x176c80 + 0x10000)
    #   Fruit::RandomFruit      @ 0x001dc5d8  (LIEF 0x1cc5d8 + 0x10000)
    #   Fruit::CollisionResponse@ 0x001dd500  (LIEF 0x1cd500 + 0x10000)
    GHIDRA_IMAGE_BASE = 0x10000

    addr_to_mangled = {}
    addr_to_demangled = {}
    sym_ranges = []

    for sym in b.symbols:
        raw_val = sym.value
        if raw_val == 0:
            continue
        addr = raw_val + GHIDRA_IMAGE_BASE
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

        dem = _demangle(name)

        if addr not in addr_to_mangled:
            addr_to_mangled[addr] = name
            addr_to_demangled[addr] = dem
        else:
            # Prefer FUNC over NOTYPE
            if sym_type_str == 'TYPE.FUNC':
                addr_to_mangled[addr] = name
                addr_to_demangled[addr] = dem

        # Collect size-bearing symbols for containment check
        size = sym.size
        if size > 0:
            sym_ranges.append((addr, addr + size, name))

    # Sort ranges by start address for binary-search efficiency
    sym_ranges.sort(key=lambda t: t[0])

    return addr_to_mangled, addr_to_demangled, sym_ranges


def _containment_check(addr: int, sym_ranges: list):
    """CONTAINMENT check: is addr strictly inside any symbol's [start, start+size)?

    Returns (containing_start, containing_end, containing_mangled) or None.
    Uses a simple linear scan; sym_ranges is sorted by start.
    """
    for (start, end, mangled) in sym_ranges:
        if start < addr < end:
            return (start, end, mangled)
        if start > addr:
            # Since sorted, no later symbol can contain addr unless we missed overlap
            # But symbols can overlap (thunks), so we can't break early
            pass
    return None


def scan_sources(src_dir: pathlib.Path) -> list:
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
                    raw_sym = m.group('sym').strip()
                    # 'binary' is a legacy placeholder, not an actual symbol name
                    if raw_sym and raw_sym != 'binary':
                        cited_sym = raw_sym
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


def classify(markers: list,
             addr_to_mangled: dict,
             addr_to_demangled: dict,
             sym_ranges: list) -> list:
    """Add 'verdict' and auxiliary fields to each marker dict.

    Fields added:
      verdict          -- see module docstring
      binary_sym       -- mangled name at cited_addr (or None)
      binary_demangled -- demangled name at cited_addr (or None)
      correct_addr     -- (STALE-MISMATCH only) the correct Ghidra address
      correct_demangled-- (STALE-MISMATCH only) demangled name at correct_addr
      containing_sym   -- (MID-SYMBOL only) mangled name of the containing func
      containing_addr  -- (MID-SYMBOL only) start address of containing func
      forward_matches  -- (STALE-AMBIGUOUS only) list of (addr, dem) candidates
    """
    for m in markers:
        addr       = m['cited_addr']
        cited_sym  = m.get('cited_sym')
        has_ver    = m['has_version']

        # ----------------------------------------------------------------
        # Step 1: look up addr in symbol-start table
        # ----------------------------------------------------------------
        if addr in addr_to_mangled:
            mangled   = addr_to_mangled[addr]
            demangled = addr_to_demangled[addr]
            m['binary_sym']       = mangled
            m['binary_demangled'] = demangled

            if not has_ver:
                if cited_sym and not _symbol_matches(cited_sym, mangled, demangled):
                    m['verdict'] = 'NO-VERSION+STALE'
                else:
                    m['verdict'] = 'NO-VERSION'
            elif not cited_sym:
                m['verdict'] = 'OK-NO-SYM'
            elif _symbol_matches(cited_sym, mangled, demangled):
                m['verdict'] = 'OK'
            else:
                # Addr resolves to wrong symbol -- run FORWARD check
                fwd = _forward_lookup(cited_sym, addr_to_mangled, addr_to_demangled)
                # Filter out the current (wrong) address
                fwd = [(a, d) for (a, d) in fwd if a != addr]
                if len(fwd) == 1:
                    m['verdict']           = 'STALE-MISMATCH'
                    m['correct_addr']      = fwd[0][0]
                    m['correct_demangled'] = fwd[0][1]
                elif len(fwd) > 1:
                    m['verdict']         = 'STALE-AMBIGUOUS'
                    m['forward_matches'] = [(hex(a), d) for (a, d) in fwd[:10]]
                else:
                    m['verdict'] = 'STALE'

        # ----------------------------------------------------------------
        # Step 2: addr not a symbol start -- containment check
        # ----------------------------------------------------------------
        else:
            m['binary_sym']       = None
            m['binary_demangled'] = None

            container = _containment_check(addr, sym_ranges)
            if container:
                (c_start, c_end, c_mangled) = container
                m['containing_sym']  = c_mangled
                m['containing_addr'] = c_start
                m['containing_dem']  = _demangle(c_mangled)
                if not has_ver:
                    m['verdict'] = 'NO-VERSION+MID-SYMBOL'
                else:
                    m['verdict'] = 'MID-SYMBOL'
            else:
                if not has_ver:
                    m['verdict'] = 'NO-VERSION+UNRESOLVED'
                else:
                    m['verdict'] = 'UNRESOLVED'

    return markers


_VERDICT_ORDER = {
    'STALE-MISMATCH':          0,
    'STALE-AMBIGUOUS':         1,
    'STALE':                   2,
    'NO-VERSION+STALE':        3,
    'NO-VERSION':              4,
    'NO-VERSION+MID-SYMBOL':   5,
    'NO-VERSION+UNRESOLVED':   6,
    'UNRESOLVED':              7,
    'MID-SYMBOL':              8,
    'OK-NO-SYM':               9,
    'OK':                      10,
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
    addr_to_mangled, addr_to_demangled, sym_ranges = load_binary_symbols(binary_path)
    print(f"  {len(addr_to_mangled)} unique symbol addresses loaded.")
    print(f"  {len(sym_ranges)} symbols with size (for containment check).")

    print(f"Scanning {src_dir} for RE markers ...", flush=True)
    markers = scan_sources(src_dir)
    print(f"  {len(markers)} markers found.")

    print("Classifying (forward + containment checks) ...", flush=True)
    markers = classify(markers, addr_to_mangled, addr_to_demangled, sym_ranges)

    # Deduplicate: same (file, line) => keep first match
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
            print(f"  {v:<32}: {c}")
    print()

    # Show STALE-MISMATCH rows (actionable: have correct addr)
    mismatch_rows = [m for m in markers if m['verdict'] == 'STALE-MISMATCH']
    if mismatch_rows:
        print(f"--- STALE-MISMATCH ({len(mismatch_rows)}) -- correct addr known, safe to fix ---")
        for m in mismatch_rows:
            sym_cited = m['cited_sym'] or '(none)'
            correct   = hex(m.get('correct_addr', 0))
            correct_d = m.get('correct_demangled', '?')
            if len(correct_d) > 55:
                correct_d = correct_d[:52] + '...'
            print(f"  {m['file']}:{m['line']}")
            print(f"    cited:   {sym_cited} @ {m['addr_str']}")
            print(f"    correct: {sym_cited} @ {correct}  ({correct_d})")
        print()

    # Show STALE-AMBIGUOUS rows (need manual resolution)
    ambig_rows = [m for m in markers if m['verdict'] == 'STALE-AMBIGUOUS']
    if ambig_rows:
        print(f"--- STALE-AMBIGUOUS ({len(ambig_rows)}) -- multiple candidates, manual review ---")
        for m in ambig_rows:
            sym_cited = m['cited_sym'] or '(none)'
            matches   = m.get('forward_matches', [])
            print(f"  {m['file']}:{m['line']}  cited: {sym_cited} @ {m['addr_str']}")
            for (ha, hd) in matches[:4]:
                hd_s = hd[:55] if len(hd) > 55 else hd
                print(f"    candidate: {ha}  {hd_s}")
        print()

    # Show remaining STALE rows
    stale_rows = [m for m in markers if m['verdict'] == 'STALE']
    if stale_rows:
        print(f"--- STALE ({len(stale_rows)}) -- symbol not found at cited addr or anywhere ---")
        for m in stale_rows:
            sym_cited = m['cited_sym'] or '(none)'
            sym_bin   = m['binary_demangled'] or m['binary_sym'] or '?'
            if len(sym_cited) > 50:
                sym_cited = sym_cited[:47] + '...'
            if len(sym_bin) > 60:
                sym_bin = sym_bin[:57] + '...'
            print(f"  [{m['verdict']}] {m['file']}:{m['line']}")
            print(f"    cited:  {sym_cited} @ {m['addr_str']}")
            print(f"    binary: {sym_bin}")
        print()

    # Show NO-VERSION+STALE rows
    nvstale_rows = [m for m in markers if m['verdict'] == 'NO-VERSION+STALE']
    if nvstale_rows:
        print(f"--- NO-VERSION+STALE ({len(nvstale_rows)}) ---")
        for m in nvstale_rows:
            sym_cited = m['cited_sym'] or '(none)'
            sym_bin   = m['binary_demangled'] or '?'
            if len(sym_bin) > 55:
                sym_bin = sym_bin[:52] + '...'
            print(f"  {m['file']}:{m['line']}  cited={sym_cited}  binary={sym_bin}")
        print()

    # Show NO-VERSION rows (up to 20)
    noversion_rows = [m for m in markers if m['verdict'] == 'NO-VERSION']
    if noversion_rows:
        print(f"--- NO-VERSION ({len(noversion_rows)}) -- addr OK but tag missing (first 20) ---")
        for m in noversion_rows[:20]:
            sym_cited = m['cited_sym'] or '(none)'
            if len(sym_cited) > 55:
                sym_cited = sym_cited[:52] + '...'
            print(f"  {m['file']}:{m['line']}  {sym_cited} @ {m['addr_str']}")
        if len(noversion_rows) > 20:
            print(f"  ... and {len(noversion_rows) - 20} more")
        print()

    # Show NO-VERSION+MID-SYMBOL (up to 20)
    nvmid_rows = [m for m in markers if m['verdict'] == 'NO-VERSION+MID-SYMBOL']
    if nvmid_rows:
        print(f"--- NO-VERSION+MID-SYMBOL ({len(nvmid_rows)}) -- valid mid-func refs, tag missing (first 20) ---")
        for m in nvmid_rows[:20]:
            sym_cited    = m['cited_sym'] or '(none)'
            container_d  = _demangle(m.get('containing_sym', '?'))
            if len(container_d) > 50:
                container_d = container_d[:47] + '...'
            print(f"  {m['file']}:{m['line']}  {sym_cited} @ {m['addr_str']}  (in {container_d})")
        if len(nvmid_rows) > 20:
            print(f"  ... and {len(nvmid_rows) - 20} more")
        print()

    # Show UNRESOLVED rows (up to 20)
    unresolved_rows = [m for m in markers if m['verdict'] in ('UNRESOLVED', 'NO-VERSION+UNRESOLVED')]
    if unresolved_rows:
        print(f"--- UNRESOLVED ({len(unresolved_rows)}) -- addr not in any symbol range (first 20) ---")
        for m in unresolved_rows[:20]:
            sym_cited = m['cited_sym'] or '(none)'
            print(f"  [{m['verdict']}] {m['file']}:{m['line']}  {sym_cited} @ {m['addr_str']}")
        if len(unresolved_rows) > 20:
            print(f"  ... and {len(unresolved_rows) - 20} more")
        print()

    print(f"Full detail in: {out_path}")


if __name__ == '__main__':
    main()
