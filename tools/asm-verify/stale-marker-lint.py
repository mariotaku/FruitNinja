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
  MID-SYMBOL-MISMATCH -- addr falls inside some function's [start,start+size)
                     range, the marker cites a symbol NAME, and that cited name
                     does NOT match the containing function. This is the
                     high-value mis-stamp class (e.g. cited 'Fruit::Init
                     @0x00176708' whose addr actually lands inside
                     'FruitFactLeaderboard'). The cited symbol is resolved
                     against the binary with STRICT exact-identifier matching;
                     report.json['correct_addr'] (+ 'correct_candidates' when
                     >1) gives the real location of the cited symbol.
  CONVENTION-SLIP -- sub-flag on MID-SYMBOL-MISMATCH / STALE-MISMATCH: the cited
                     symbol's real address is exactly cited_addr +/- 0x10000
                     (a LIEF<->Ghidra image-base convention mix-up). The fix is
                     just the +/- 0x10000 correction. report.json carries
                     'convention_slip': true and 'correct_addr'.
  NO-VERSION      -- marker lacks 'v1.6.1' version tag; addr resolves OK.
  MID-SYMBOL      -- addr is not a symbol start, but falls within the [start,
                     start+size) range of a known function/object, AND the cited
                     name (if any) matches the containing function -- a genuine
                     deliberate mid-function reference. report.json contains
                     'containing_sym' and 'containing_addr'.
  UNRESOLVED      -- addr not found as a known symbol start, and NOT within any
                     symbol's size range. Truly unknown.
  OK-NO-SYM       -- has v1.6.1 but no symbol name in the marker (old 'binary @')

All NO-VERSION variants carry a sub-verdict:
  NO-VERSION       -- addr resolves to a matching symbol (OK except missing tag)
  NO-VERSION+STALE -- addr resolves but symbol name doesn't match
  NO-VERSION+MID-SYMBOL -- addr is inside a function body (valid mid-func ref)
  NO-VERSION+UNRESOLVED -- addr not found at all

report.json per-marker fields of note (added by this revision):
  correct_addr        -- int Ghidra addr where the cited symbol really lives
                         (STALE-MISMATCH and unambiguous MID-SYMBOL-MISMATCH).
  correct_demangled   -- demangled name at correct_addr.
  correct_candidates  -- [[hex_addr, demangled], ...] when the cited symbol
                         resolves to >1 strict address (ambiguous mismatch).
  convention_slip     -- true when correct_addr == cited_addr +/- 0x10000.
  containing_sym/_addr/_dem -- containing function for MID-SYMBOL[-MISMATCH].

Addresses use GHIDRA convention throughout: Ghidra_addr = LIEF_value + 0x10000.

Usage:
    python tools/asm-verify/stale-marker-lint.py [--src <dir>] [--binary <path>]
    python tools/asm-verify/stale-marker-lint.py --fix      # auto-correct safe cases
    python tools/asm-verify/stale-marker-lint.py --check     # CI: exit 1 on bugs
    python tools/asm-verify/stale-marker-lint.py --check --strict  # also fail NO-VERSION

  --fix    in-place, comment-only address rewrites for STALE-MISMATCH and
           MID-SYMBOL-MISMATCH / CONVENTION-SLIP where the cited symbol resolves
           to EXACTLY ONE address (preserves CRLF, only touches the addr text).
           Ambiguous (>1) and zero-match cases are left untouched and reported.
  --check  exit non-zero if any STALE-MISMATCH / STALE / MID-SYMBOL-MISMATCH
           exists (actionable bugs); exit 0 otherwise. --strict also fails on
           NO-VERSION. Prints a one-line PASS/FAIL.

Output:
    tmp/stale-markers/report.json  (machine-readable, full detail)
    stdout                         (ranked summary: mismatches first)

Additional audit checks (informational; do not gate --check by default):

  HOLLOW-MARKER (Check A) -- a '// ASM-verified:' or '// ASM-spec' marker sits
      on a port function whose body is trivial (empty / bare 'return;') while
      the cited binary function is non-trivial in size. Catches false
      confirmations like the MenuButton::~MenuButton empty-dtor bug, where the
      marker claimed verification but the port body never called the real
      logic. Bodies following a '// Defunct:' comment are intentionally-empty
      stubs and are skipped (see CLAUDE.md "Defunct features -- stub, never
      skip"). Only markers whose address is an EXACT binary symbol-table
      start are sized -- containment-fallback sizing was tried and produces
      false positives (a 4-byte unsymboled local stub can land inside an
      unrelated 676-byte neighbour's range). report.json['hollow_markers']
      carries the full list.

  DEFER-BLOCKER-REQUIRED (Check B, PRIMARY defer rule) -- every ACCEPT-deferred
      triage.json entry must name a CONCRETE blocker: an unported subsystem/
      symbol ("blocked on X", "X not ported", "X unported") or a linked task
      id ("#123", "task #123"). A reason that doesn't name one is vague
      ("further RE needed", "Same.", copy-paste dtor boilerplate, empty) and
      is auto-flagged for mandatory re-triage REGARDLESS of score -- if you
      can't name a concrete unported dependency, ACCEPT-deferred is a
      FIX-NEEDED being dodged. This is how Fruit::Init slipped through twice
      on "further RE needed" without ever naming what RE was blocked on.
      Legitimate defer = named blocker, so when that subsystem lands, every
      entry deferred on it can be swept for re-triage together.
      report.json['deferred_no_blocker'] carries the full list.

  DEFERRED-HIGH-RATIO (Check C, secondary signal) -- reads
      tools/asm-verify/triage.json and flags ACCEPT-deferred entries whose
      score/max_score ratio exceeds DEFERRED_RATIO_THRESHOLD. A high ratio on
      a small-weight divergence is a secondary real-bug tell (see project
      memory feedback_asm_verify_ratio_scan.md -- it previously surfaced
      MatrixManager/ColSphere/Utf8 bugs and would have caught Fruit::Init,
      which sat at ratio 1.7 re-affirmed twice without re-triage). Ratio is
      now secondary to Check B's blocker-reason validation.
      report.json['deferred_high_ratio'] carries the full list.
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
TRIAGE_DEFAULT = SCRIPT_DIR / "triage.json"

# ---------------------------------------------------------------------------
# Check A (HOLLOW-MARKER) thresholds
# ---------------------------------------------------------------------------
# Binary functions at or below this size are plausibly trivial themselves
# (thunks, tiny accessors) -- only flag a hollow port body when the cited
# binary function is bigger than this, i.e. it plausibly DOES something.
HOLLOW_MIN_BINARY_SIZE = 64  # bytes
# How many lines forward of the marker (and of the '{') we're willing to scan
# looking for the function signature / matching closing brace, before giving
# up. Generous enough for real bodies; hollow ones are short by definition.
HOLLOW_MAX_SCAN_LINES = 12

# Port bodies that normalise (whitespace-collapsed, comments stripped) to one
# of these strings are considered "hollow" -- no observable side effect.
_TRIVIAL_BODIES = frozenset([
    '', 'return;', 'return 0;', 'return false;', 'return true;',
    'return nullptr;', 'return NULL;',
])

# ---------------------------------------------------------------------------
# Check B (DEFER-BLOCKER-REQUIRED) -- primary defer rule
# ---------------------------------------------------------------------------
# An ACCEPT-deferred reason must name a CONCRETE blocker to be legitimate:
# an unported subsystem/symbol ("blocked on X" / "X not ported" / "X
# unported" / "awaiting X" / "pending X port") or a linked task id ("#123",
# "task #123"). Anything else ("further RE needed", "Same.", empty,
# copy-paste boilerplate) is vague and gets auto-flagged for re-triage
# regardless of score -- see module docstring (Fruit::Init precedent).
_BLOCKER_RE = re.compile(
    r'blocked\s+(?:on|by)\s+\S'
    r'|\bnot\s+(?:yet\s+)?ported\b'
    r'|\bunported\b'
    r'|\bawaiting\s+\S'
    r'|\bpending\s+\S+\s+port\b'
    r'|#\d+'
    r'|\btask\s*#?\d+',
    re.IGNORECASE)

# ---------------------------------------------------------------------------
# Check C (DEFERRED-HIGH-RATIO) -- secondary signal
# ---------------------------------------------------------------------------
# score/max_score ratio at/above which an ACCEPT-deferred triage.json entry is
# treated as a likely real bug rather than genuine cosmetic drift (see project
# memory feedback_asm_verify_ratio_scan.md: small-weight high-ratio divergences
# are the documented tell; found MatrixManager/ColSphere/Utf8 bugs this way).
# Secondary to Check B's blocker-reason validation.
DEFERRED_RATIO_THRESHOLD = 1.5


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


def _ascii_safe(s: str) -> str:
    """Best-effort ASCII-fold for stdout printing (Windows console codepages
    like cp932 crash on stray non-ASCII bytes -- e.g. an em-dash '—' in a
    triage.json reason string). Runtime output must be ASCII only per project
    convention; this only affects what we print, not the JSON report file."""
    if not s:
        return s
    return s.encode('ascii', 'replace').decode('ascii')


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


def _symbol_matches_strict(cited_sym: str, binary_sym: str, demangled: str) -> bool:
    """STRICT exact-identifier matcher (from fix_misstamps.py).

    Used by the auto-fixer's forward lookup and by MID-SYMBOL-MISMATCH resolution.
    DIFFERS from the loose _symbol_matches: it requires an EXACT identifier match
    (not a substring/prefix/suffix), so that "exactly 1 candidate" is real and
    lexical neighbours (e.g. 'Foo::Draw' vs 'Game::Draw') do not vote.

    Rules (all exact):
      - full namespace-qualified base equal, or dem ends with '::<cited_base>'
      - last-segment EXACT equality (only when cited is unqualified)
      - dtor:  '~Foo' vs '{dtor}/{base dtor}/...' of class Foo
      - compiler temp:  'T_NNNN' vs 'T.NNNN'
    """
    cited_clean = cited_sym.strip()
    dem_clean   = demangled
    cited_base = re.sub(r'\(.*', '', cited_clean).strip()
    dem_base   = re.sub(r'\(.*', '', dem_clean).strip()

    def _last_segment(s):
        s = re.sub(r'<[^>]*>', '', s)
        return s.rsplit('::', 1)[-1].strip()

    cited_last = _last_segment(cited_base)
    dem_last   = _last_segment(dem_base)

    # 1. Full qualified-name match (cited may omit a leading namespace).
    if cited_base and '::' in cited_base:
        if dem_base == cited_base or dem_base.endswith('::' + cited_base):
            return True
    # 2. Last-segment EXACT equality, only for unqualified cited names.
    if cited_last and dem_last and cited_last == dem_last:
        if '::' not in cited_base:
            return True
    # 3. Dtor.
    if cited_last.startswith('~'):
        class_name = cited_last[1:]
        if class_name and 'dtor' in dem_last:
            dem_parts = re.sub(r'\(.*', '', dem_clean).rsplit('::', 2)
            if len(dem_parts) >= 2:
                dem_class = re.sub(r'<[^>]*>', '', dem_parts[-2].strip()).strip()
                if dem_class == class_name or dem_class.endswith('::' + class_name):
                    return True
    # 4. Compiler temp.
    if re.match(r'^T_\d+$', cited_last):
        dot_form = cited_last.replace('_', '.', 1)
        if dot_form == binary_sym or dot_form == demangled:
            return True
    return False


def _forward_lookup_strict(cited_sym: str,
                           addr_to_mangled: dict,
                           addr_to_demangled: dict) -> list:
    """STRICT forward check: addresses where cited_sym EXACTLY matches.

    Returns sorted list of (addr, demangled). Empty == symbol absent.
    """
    if not cited_sym:
        return []
    results = []
    seen = set()
    for addr, mangled in addr_to_mangled.items():
        dem = addr_to_demangled.get(addr, mangled)
        if _symbol_matches_strict(cited_sym, mangled, dem):
            if addr not in seen:
                seen.add(addr)
                results.append((addr, dem))
    results.sort(key=lambda x: x[0])
    return results


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
        # ARM/Thumb: an STT_FUNC symbol value carries the Thumb bit in bit 0, so
        # a Thumb function at 0x0022e544 has symbol value 0x0022e545. Source
        # markers (and asm-verify's report.json) cite the real, even function
        # start. Without masking, every such marker misses addr_to_mangled and
        # then lands inside the PRECEDING function's [start, start+size) range,
        # producing a bogus MID-SYMBOL-MISMATCH whose "correct" address is the
        # Thumb-tagged one -- applying that suggestion would corrupt a correct
        # marker. Mask it off so both the lookup map and the containment ranges
        # below use true function starts.
        if sym_type_str == 'TYPE.FUNC':
            raw_val &= ~1
        addr = raw_val + GHIDRA_IMAGE_BASE

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


def _resolve_cited(cited_sym, cited_addr, addr_to_mangled, addr_to_demangled):
    """STRICT-resolve cited_sym against the binary, excluding cited_addr itself.

    Returns dict with keys:
      candidates       -- list of (addr, demangled), strict matches, sorted.
      correct_addr     -- int (only when exactly 1 candidate), else None.
      correct_demangled-- demangled at correct_addr, else None.
      convention_slip  -- True when the sole correct_addr == cited_addr +/-0x10000.
    """
    fwd = _forward_lookup_strict(cited_sym, addr_to_mangled, addr_to_demangled)
    fwd = [(a, d) for (a, d) in fwd if a != cited_addr]
    info = {'candidates': fwd, 'correct_addr': None,
            'correct_demangled': None, 'convention_slip': False}
    if len(fwd) == 1:
        info['correct_addr']      = fwd[0][0]
        info['correct_demangled'] = fwd[0][1]
        if abs(fwd[0][0] - cited_addr) == 0x10000:
            info['convention_slip'] = True
    return info


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
                # Addr resolves to wrong symbol -- STRICT-resolve the cited name.
                res = _resolve_cited(cited_sym, addr, addr_to_mangled, addr_to_demangled)
                cand = res['candidates']
                if len(cand) == 1:
                    m['verdict']           = 'STALE-MISMATCH'
                    m['correct_addr']      = res['correct_addr']
                    m['correct_demangled'] = res['correct_demangled']
                    if res['convention_slip']:
                        m['convention_slip'] = True
                elif len(cand) > 1:
                    m['verdict']            = 'STALE-AMBIGUOUS'
                    m['forward_matches']    = [(hex(a), d) for (a, d) in cand[:10]]
                    m['correct_candidates'] = [[hex(a), d] for (a, d) in cand[:10]]
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
                c_dem = _demangle(c_mangled)
                m['containing_sym']  = c_mangled
                m['containing_addr'] = c_start
                m['containing_dem']  = c_dem

                # Does the cited name match the CONTAINING function? If a symbol
                # is cited and it does NOT match the container, this is the
                # high-value mis-stamp class (cited 'Fruit::Init' but addr lands
                # inside 'FruitFactLeaderboard'). Resolve the cited symbol
                # strictly to find where it really lives.
                cited_matches_container = (
                    cited_sym is not None
                    and (_symbol_matches(cited_sym, c_mangled, c_dem)
                         or _symbol_matches_strict(cited_sym, c_mangled, c_dem))
                )

                if cited_sym is not None and not cited_matches_container:
                    res = _resolve_cited(cited_sym, addr,
                                         addr_to_mangled, addr_to_demangled)
                    cand = res['candidates']
                    m['verdict'] = 'MID-SYMBOL-MISMATCH'
                    if len(cand) == 1:
                        m['correct_addr']      = res['correct_addr']
                        m['correct_demangled'] = res['correct_demangled']
                        if res['convention_slip']:
                            m['convention_slip'] = True
                    elif len(cand) > 1:
                        m['correct_candidates'] = [[hex(a), d] for (a, d) in cand[:10]]
                    # len 0 -> cited symbol absent; no correct_addr.
                elif not has_ver:
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
    'MID-SYMBOL-MISMATCH':     0,
    'STALE-MISMATCH':          1,
    'STALE-AMBIGUOUS':         2,
    'STALE':                   3,
    'NO-VERSION+STALE':        4,
    'NO-VERSION':              5,
    'NO-VERSION+MID-SYMBOL':   6,
    'NO-VERSION+UNRESOLVED':   7,
    'UNRESOLVED':              8,
    'MID-SYMBOL':              9,
    'OK-NO-SYM':               10,
    'OK':                      11,
}

# Verdicts that --check treats as actionable bugs (exit non-zero).
_CHECK_FAIL_VERDICTS = {'MID-SYMBOL-MISMATCH', 'STALE-MISMATCH', 'STALE'}
# Additionally failed under --strict.
_CHECK_STRICT_EXTRA = {'NO-VERSION', 'NO-VERSION+STALE'}


def _dedupe_sort(markers: list) -> list:
    """Dedupe by (file, line) keeping first, then sort by verdict priority."""
    seen = set()
    out = []
    for m in markers:
        key = (m['file'], m['line'])
        if key not in seen:
            seen.add(key)
            out.append(m)
    out.sort(key=lambda m: (_VERDICT_ORDER.get(m['verdict'], 99), m['file'], m['line']))
    return out


def apply_fixes(markers: list, project_root: pathlib.Path) -> dict:
    """Comment-only, in-place auto-fix of safely-resolvable mis-stamps.

    Fixes STALE-MISMATCH and MID-SYMBOL-MISMATCH (incl. CONVENTION-SLIP) markers
    that have a single 'correct_addr'. Only the cited address text is rewritten;
    CRLF / line endings are preserved (we splitlines(keepends=True) and replace
    inside the matched line). Ambiguous (>1 candidate) and zero-match markers are
    left untouched and reported.

    Returns counts dict: fixed / ambiguous / zero_match / skipped.
    """
    counts = {'fixed': 0, 'ambiguous': 0, 'zero_match': 0, 'skipped': 0}
    # Group fixable markers by file so we read/write each file once.
    by_file = {}
    fixable_verdicts = ('STALE-MISMATCH', 'MID-SYMBOL-MISMATCH')
    for m in markers:
        if m['verdict'] not in fixable_verdicts:
            continue
        if m.get('correct_addr') is None:
            if m.get('correct_candidates'):
                counts['ambiguous'] += 1
            else:
                counts['zero_match'] += 1
            continue
        by_file.setdefault(m['file'], []).append(m)

    for f_rel, recs in by_file.items():
        f_abs = project_root / f_rel
        if not f_abs.exists():
            counts['skipped'] += len(recs)
            continue
        # keepends=True preserves CRLF / LF exactly per line.
        with f_abs.open('r', encoding='utf-8', errors='replace', newline='') as fh:
            lines = fh.read().splitlines(keepends=True)
        changed = False
        for m in recs:
            idx = m['line'] - 1
            if idx < 0 or idx >= len(lines):
                counts['skipped'] += 1
                continue
            cur = lines[idx]
            old_addr = m['addr_str']
            if old_addr not in cur:
                counts['skipped'] += 1   # line shifted since scan
                continue
            new_addr = "0x%08x" % m['correct_addr']
            lines[idx] = cur.replace(old_addr, new_addr)
            counts['fixed'] += 1
            changed = True
        if changed:
            with f_abs.open('w', encoding='utf-8', newline='') as fh:
                fh.write(''.join(lines))
    return counts


_LINE_COMMENT_RE  = re.compile(r'//.*$', re.MULTILINE)
_BLOCK_COMMENT_RE = re.compile(r'/\*.*?\*/', re.DOTALL)


def _strip_comments(text: str) -> str:
    """Strip // and /* */ comments from a chunk of C++ source (best-effort;
    does not understand string/char literals, which is an acceptable risk for
    this heuristic since marker-adjacent signatures rarely contain braces
    inside string literals)."""
    text = _BLOCK_COMMENT_RE.sub('', text)
    text = _LINE_COMMENT_RE.sub('', text)
    return text


def _find_function_body(lines: list, marker_line_idx: int):
    """Locate the braced body of the function a marker sits on.

    Starting at marker_line_idx (0-based), skips forward over blank/comment
    lines (the rest of the marker's comment block) to the function signature,
    then finds the first '{' and its matching '}' via brace-depth counting.

    Returns (body_text, is_defunct, has_init_list):
      body_text     -- text strictly between the braces, or None if no braced
                       body was found within HOLLOW_MAX_SCAN_LINES (e.g. a bare
                       declaration ending in ';', or the scan window ran out).
      is_defunct    -- True if a '// Defunct:' comment was seen in the marker's
                       own line or the comment block leading to the signature --
                       those are legitimate no-op stubs and must be skipped.
      has_init_list -- True if a non-empty ctor member-initializer list
                       (': Base(), m_Field(0), ...') sits between the
                       signature and the body brace. A ctor that zeroes/inits
                       every field this way is NOT hollow even if its {} body
                       is empty -- the initializer list IS the ctor logic
                       (observed false positives: ColSphere::ColSphere(),
                       InputDeviceBada::InputDeviceBada(),
                       GlobalProbabilityOveride::GlobalProbabilityOveride()).
    """
    n = len(lines)
    is_defunct = 'Defunct:' in lines[marker_line_idx] if marker_line_idx < n else False

    j = marker_line_idx + 1
    scanned = 0
    while j < n and scanned < HOLLOW_MAX_SCAN_LINES:
        stripped = lines[j].strip()
        if not stripped:
            j += 1; scanned += 1; continue
        if stripped.startswith('//'):
            if 'Defunct:' in stripped:
                is_defunct = True
            j += 1; scanned += 1; continue
        break
    if j >= n:
        return None, is_defunct, False

    # Generous window: real (non-hollow) bodies may run long, but we only
    # need to positively identify HOLLOW ones -- if the window runs out we
    # simply give up on this marker (informational check, safe to under-flag).
    window_end = min(n, j + HOLLOW_MAX_SCAN_LINES * 4)
    text = ''.join(_strip_comments(l) + '\n' for l in lines[j:window_end])

    brace_pos = text.find('{')
    semi_pos  = text.find(';')
    if brace_pos == -1:
        return None, is_defunct, False
    if semi_pos != -1 and semi_pos < brace_pos:
        return None, is_defunct, False  # bare declaration (e.g. 'void Foo();')

    # Ctor member-initializer list: a ':' between the signature and '{' that
    # isn't part of a '::' qualifier. Any non-whitespace content after it
    # counts as real initialization work.
    pre_brace = text[:brace_pos]
    init_match = re.search(r'(?<!:):(?!:)(.*)$', pre_brace, re.DOTALL)
    has_init_list = bool(init_match and init_match.group(1).strip())

    depth = 0
    body_start = None
    for idx in range(brace_pos, len(text)):
        ch = text[idx]
        if ch == '{':
            depth += 1
            if depth == 1:
                body_start = idx + 1
        elif ch == '}':
            depth -= 1
            if depth == 0:
                return text[body_start:idx], is_defunct, has_init_list
    return None, is_defunct, False  # unbalanced within window -- give up


def _is_hollow_body(body_text: str) -> bool:
    """True if body_text (already comment-stripped) normalises to a trivial
    no-op per _TRIVIAL_BODIES (whitespace-collapsed comparison)."""
    stripped = re.sub(r'\s+', ' ', body_text).strip()
    return stripped in _TRIVIAL_BODIES


def _binary_fn_size(addr: int, addr_to_size: dict):
    """Byte size of the binary function at addr, EXACT symbol-start only.

    Deliberately does NOT fall back to the containment check: local/static
    functions that lack their own symbol-table entry get silently subsumed
    into whatever unrelated exported symbol's [start,end) range happens to
    span their address (observed case: two 4-byte AsinIdx/AcosIdx stubs in
    MathUtil.cpp landed inside ListBox::{ctor}'s 676-byte range, which would
    have wrongly flagged their genuinely-4-byte-stub 'return 0;' bodies as
    hollow-but-binary-is-big). Only trust a size when addr IS a real symbol
    start, so Check A stays a high-confidence signal, not containment noise.
    """
    return addr_to_size.get(addr)


def check_hollow_markers(markers: list, addr_to_size: dict,
                          project_root: pathlib.Path) -> list:
    """Check A: flag ASM-verified/ASM-spec markers sitting on a trivial port
    body while the cited binary function is non-trivial in size.

    Only markers whose cited_addr is an EXACT binary symbol-table start are
    considered (see _binary_fn_size) -- this keeps the check high-confidence
    instead of attributing an unrelated enclosing symbol's size to a small
    unsymboled local function.

    Returns a list of dicts: file, line, kind, cited_sym, addr_str,
    port_body (the hollow text, or '(empty)'), binary_fn_size.
    """
    results = []
    file_cache = {}
    for m in markers:
        if m['kind'] not in ('ASM-verified', 'ASM-spec'):
            continue
        f_rel = m['file']
        if f_rel not in file_cache:
            f_abs = project_root / f_rel
            try:
                file_cache[f_rel] = f_abs.read_text(
                    encoding='utf-8', errors='replace').splitlines()
            except Exception:
                file_cache[f_rel] = None
        lines = file_cache[f_rel]
        if lines is None:
            continue

        marker_idx = m['line'] - 1
        if marker_idx < 0 or marker_idx >= len(lines):
            continue

        body, is_defunct, has_init_list = _find_function_body(lines, marker_idx)
        if is_defunct or body is None or has_init_list:
            continue
        if not _is_hollow_body(body):
            continue

        bsize = _binary_fn_size(m['cited_addr'], addr_to_size)
        if bsize is None or bsize <= HOLLOW_MIN_BINARY_SIZE:
            continue

        results.append({
            'file':           m['file'],
            'line':           m['line'],
            'kind':           m['kind'],
            'cited_sym':      m['cited_sym'],
            'addr_str':       m['addr_str'],
            'port_body':      body.strip() or '(empty)',
            'binary_fn_size': bsize,
        })
    return results


def _load_triage(triage_path: pathlib.Path) -> dict:
    """Load triage.json (symbol -> {verdict, score, max_score, reason, ...}).
    Returns {} if the file doesn't exist."""
    if not triage_path.exists():
        return {}
    with triage_path.open('r', encoding='utf-8') as f:
        return json.load(f)


def _has_concrete_blocker(reason: str) -> bool:
    """True if reason names a concrete unported dependency or linked task id
    per _BLOCKER_RE (see module docstring / Check B)."""
    if not reason or not reason.strip():
        return False
    return bool(_BLOCKER_RE.search(reason))


def check_deferred_no_blocker(triage: dict) -> list:
    """Check B (PRIMARY defer rule): ACCEPT-deferred entries whose reason does
    NOT name a concrete blocker. Flagged regardless of score/ratio -- a defer
    with no named blocker is a FIX-NEEDED being dodged (see module docstring).

    Returns a list of dicts: symbol, ratio (or None if max_score missing),
    score, max_score, reason, decided_at. Sorted descending by ratio (ratio-
    less entries last) so the highest-signal candidates surface first.
    """
    results = []
    for sym, entry in triage.items():
        if entry.get('verdict') != 'ACCEPT-deferred':
            continue
        reason = entry.get('reason', '')
        if _has_concrete_blocker(reason):
            continue
        score     = entry.get('score')
        max_score = entry.get('max_score')
        ratio = (score / max_score) if max_score else None
        results.append({
            'symbol':     sym,
            'ratio':      ratio,
            'score':      score,
            'max_score':  max_score,
            'reason':     reason,
            'decided_at': entry.get('decided_at'),
        })
    results.sort(key=lambda r: (r['ratio'] is not None, r['ratio'] or 0), reverse=True)
    return results


def check_deferred_high_ratio(triage: dict) -> list:
    """Check C (secondary signal): ACCEPT-deferred entries whose score/
    max_score ratio is >= DEFERRED_RATIO_THRESHOLD -- see module docstring.
    Sorted descending by ratio.

    Returns a list of dicts: symbol, ratio, score, max_score, reaffirm_count
    (informational: how many times 're-affirmed' appears in the reason text),
    reason, decided_at.
    """
    results = []
    for sym, entry in triage.items():
        if entry.get('verdict') != 'ACCEPT-deferred':
            continue
        score     = entry.get('score')
        max_score = entry.get('max_score')
        if not max_score:
            continue
        ratio = score / max_score
        if ratio < DEFERRED_RATIO_THRESHOLD:
            continue
        reason = entry.get('reason', '')
        results.append({
            'symbol':         sym,
            'ratio':          ratio,
            'score':          score,
            'max_score':      max_score,
            'reaffirm_count': reason.count('re-affirmed'),
            'reason':         reason,
            'decided_at':     entry.get('decided_at'),
        })
    results.sort(key=lambda r: r['ratio'], reverse=True)
    return results


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--src',    default=str(SRC_DIR),
                    help='Source directory to scan (default: src/)')
    ap.add_argument('--binary', default=str(BINARY_DEFAULT),
                    help='Path to FruitNinja ELF binary')
    ap.add_argument('--out',    default=str(OUT_DIR / 'report.json'),
                    help='Output JSON path')
    ap.add_argument('--triage', default=str(TRIAGE_DEFAULT),
                    help='Path to triage.json (for the DEFERRED-HIGH-RATIO check)')
    ap.add_argument('--fix',    action='store_true',
                    help='auto-correct safely-resolvable mis-stamps in place '
                         '(comment-only address rewrites)')
    ap.add_argument('--check',  action='store_true',
                    help='CI mode: exit non-zero if actionable bugs exist '
                         '(MID-SYMBOL-MISMATCH / STALE-MISMATCH / STALE)')
    ap.add_argument('--strict', action='store_true',
                    help='with --check, also fail on NO-VERSION markers')
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

    # addr -> size for exact symbol-start lookups (Check A). Keep the largest
    # when multiple size-bearing symbols share a start address (thunks).
    addr_to_size = {}
    for (start, end, _mangled) in sym_ranges:
        sz = end - start
        if sz > addr_to_size.get(start, 0):
            addr_to_size[start] = sz

    print(f"Scanning {src_dir} for RE markers ...", flush=True)
    markers = scan_sources(src_dir)
    print(f"  {len(markers)} markers found.")

    print("Classifying (forward + containment checks) ...", flush=True)
    markers = classify(markers, addr_to_mangled, addr_to_demangled, sym_ranges)
    markers = _dedupe_sort(markers)

    # -----------------------------------------------------------------------
    # --fix: auto-correct safely-resolvable mis-stamps, then re-scan so the
    # report/summary reflect the post-fix state.
    # -----------------------------------------------------------------------
    fix_counts = None
    if args.fix:
        print("\nApplying fixes (comment-only address rewrites) ...", flush=True)
        fix_counts = apply_fixes(markers, src_dir.parent)
        print(f"  fixed={fix_counts['fixed']}  ambiguous={fix_counts['ambiguous']}"
              f"  zero_match={fix_counts['zero_match']}  skipped={fix_counts['skipped']}")
        # Re-scan & re-classify against the rewritten sources.
        markers = scan_sources(src_dir)
        markers = classify(markers, addr_to_mangled, addr_to_demangled, sym_ranges)
        markers = _dedupe_sort(markers)

    # -----------------------------------------------------------------------
    # Check A: HOLLOW-MARKER -- ASM-verified/ASM-spec marker on a trivial
    # port body while the cited binary function is non-trivial in size.
    # -----------------------------------------------------------------------
    print("Checking for hollow markers (Check A) ...", flush=True)
    hollow_markers = check_hollow_markers(markers, addr_to_size, src_dir.parent)
    print(f"  {len(hollow_markers)} hollow marker(s) found.")

    # -----------------------------------------------------------------------
    # Check B (primary) / Check C (secondary): mine triage.json ACCEPT-
    # deferred entries -- B for a missing concrete blocker, C for high ratio.
    # -----------------------------------------------------------------------
    triage_path = pathlib.Path(args.triage)
    triage = _load_triage(triage_path)
    print(f"Checking triage.json ACCEPT-deferred entries for a named blocker (Check B) ...", flush=True)
    deferred_no_blocker = check_deferred_no_blocker(triage)
    print(f"  {len(deferred_no_blocker)} ACCEPT-deferred entr(y/ies) with no named blocker.")
    print(f"Checking triage.json for high-ratio deferred entries (Check C) ...", flush=True)
    deferred_high_ratio = check_deferred_high_ratio(triage)
    print(f"  {len(deferred_high_ratio)} high-ratio ACCEPT-deferred entr(y/ies) found.")

    # Write JSON report
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open('w', encoding='utf-8') as f:
        json.dump({
            'markers':             markers,
            'hollow_markers':      hollow_markers,
            'deferred_no_blocker': deferred_no_blocker,
            'deferred_high_ratio': deferred_high_ratio,
        }, f, indent=2)
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
    print(f"  {'HOLLOW-MARKER':<32}: {len(hollow_markers)}")
    print(f"  {'DEFER-NO-BLOCKER':<32}: {len(deferred_no_blocker)}")
    print(f"  {'DEFERRED-HIGH-RATIO':<32}: {len(deferred_high_ratio)}")
    print()

    # --- Check A: HOLLOW-MARKER -- trivial port body, non-trivial binary fn.
    if hollow_markers:
        print(f"--- HOLLOW-MARKER ({len(hollow_markers)}) -- marker claims verification but "
              f"port body is trivial ---")
        for h in hollow_markers:
            print(f"  {h['file']}:{h['line']}  [{h['kind']}] {h['cited_sym'] or '(none)'} "
                  f"@ {h['addr_str']}")
            print(f"    port body:  {_ascii_safe(h['port_body'])!r}")
            print(f"    binary fn size: {h['binary_fn_size']} bytes "
                  f"(> {HOLLOW_MIN_BINARY_SIZE} threshold)")
        print()

    # --- Check B (PRIMARY): DEFER-NO-BLOCKER -- ACCEPT-deferred with no named
    # blocker, i.e. likely a FIX-NEEDED being dodged. Shown before Check C
    # since this is now the primary defer rule.
    if deferred_no_blocker:
        print(f"--- DEFER-NO-BLOCKER ({len(deferred_no_blocker)}) -- ACCEPT-deferred with no "
              f"concrete blocker named; mandatory re-triage ---")
        for d in deferred_no_blocker:
            ratio_s = f"{d['ratio']:.2f}x" if d['ratio'] is not None else '?'
            reason = _ascii_safe(d['reason']) or '(empty)'
            if len(reason) > 100:
                reason = reason[:97] + '...'
            print(f"  {d['symbol']}  ratio={ratio_s}  score={d['score']}/{d['max_score']}  "
                  f"decided_at={d['decided_at']}")
            print(f"    reason: {reason}")
        print()

    # --- Check C (secondary): DEFERRED-HIGH-RATIO.
    if deferred_high_ratio:
        print(f"--- DEFERRED-HIGH-RATIO ({len(deferred_high_ratio)}) -- ratio >= "
              f"{DEFERRED_RATIO_THRESHOLD} on an ACCEPT-deferred entry (secondary signal) ---")
        for d in deferred_high_ratio:
            reason = _ascii_safe(d['reason']) or '(empty)'
            if len(reason) > 90:
                reason = reason[:87] + '...'
            print(f"  {d['symbol']}  ratio={d['ratio']:.2f}x  score={d['score']}/{d['max_score']}  "
                  f"reaffirm_count={d['reaffirm_count']}")
            print(f"    reason: {reason}")
        print()

    # Show MID-SYMBOL-MISMATCH rows FIRST (the real bugs: cited name lands
    # inside a DIFFERENT function than the one it names).
    midmis_rows = [m for m in markers if m['verdict'] == 'MID-SYMBOL-MISMATCH']
    if midmis_rows:
        print(f"--- MID-SYMBOL-MISMATCH ({len(midmis_rows)}) -- cited symbol's addr "
              f"is inside a DIFFERENT function ---")
        for m in midmis_rows:
            sym_cited   = m['cited_sym'] or '(none)'
            container_d = m.get('containing_dem') or _demangle(m.get('containing_sym', '?'))
            if len(container_d) > 55:
                container_d = container_d[:52] + '...'
            print(f"  {m['file']}:{m['line']}")
            print(f"    cited:     {sym_cited} @ {m['addr_str']}")
            print(f"    addr is in: {container_d}")
            if m.get('correct_addr') is not None:
                correct_d = m.get('correct_demangled', '?')
                if len(correct_d) > 55:
                    correct_d = correct_d[:52] + '...'
                slip = '  [CONVENTION-SLIP +/-0x10000]' if m.get('convention_slip') else ''
                print(f"    correct:   {sym_cited} @ {hex(m['correct_addr'])}"
                      f"  ({correct_d}){slip}")
            elif m.get('correct_candidates'):
                print(f"    AMBIGUOUS: {len(m['correct_candidates'])} candidates "
                      f"(manual review)")
                for ha, hd in m['correct_candidates'][:4]:
                    print(f"      candidate: {ha}  {hd[:55]}")
            else:
                print(f"    cited symbol ABSENT in binary (defunct/inlined)")
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
            slip = '  [CONVENTION-SLIP +/-0x10000]' if m.get('convention_slip') else ''
            print(f"  {m['file']}:{m['line']}")
            print(f"    cited:   {sym_cited} @ {m['addr_str']}")
            print(f"    correct: {sym_cited} @ {correct}  ({correct_d}){slip}")
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

    # -----------------------------------------------------------------------
    # --fix summary
    # -----------------------------------------------------------------------
    if fix_counts is not None:
        print("\n" + "=" * 70)
        print("FIX SUMMARY")
        print("=" * 70)
        print(f"  fixed       : {fix_counts['fixed']}")
        print(f"  ambiguous   : {fix_counts['ambiguous']}  (>1 candidate, left untouched)")
        print(f"  zero_match  : {fix_counts['zero_match']}  (cited symbol absent, left untouched)")
        print(f"  skipped     : {fix_counts['skipped']}  (line shifted / missing)")

    # -----------------------------------------------------------------------
    # --check: CI exit code
    # -----------------------------------------------------------------------
    if args.check:
        fail_verdicts = set(_CHECK_FAIL_VERDICTS)
        if args.strict:
            fail_verdicts |= _CHECK_STRICT_EXTRA
        failing = [m for m in markers if m['verdict'] in fail_verdicts]
        fail_counts = Counter(m['verdict'] for m in failing)
        print("\n" + "=" * 70)
        if failing:
            detail = ', '.join(f"{v}={fail_counts[v]}"
                               for v in sorted(fail_counts))
            print(f"CHECK: FAIL -- {len(failing)} actionable marker(s): {detail}")
            print("=" * 70)
            return 1
        scope = "actionable + NO-VERSION" if args.strict else "actionable"
        print(f"CHECK: PASS -- no {scope} marker bugs")
        print("=" * 70)
        return 0
    return 0


if __name__ == '__main__':
    sys.exit(main() or 0)
