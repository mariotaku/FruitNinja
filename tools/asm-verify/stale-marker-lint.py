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
  PLT-THUNK       -- addr lands inside the .plt import-stub block and decodes to
                     a real PLT entry. Ghidra names PLT stubs after their
                     target, so a marker written by searching a symbol name and
                     taking the FIRST hit records the thunk, not the function
                     body. The thunk's GOT slot is matched to its ARM_JUMP_SLOT
                     relocation, the relocation's symbol is looked up in the
                     symbol table, and (per trap (a) below) any 4-byte 'b'
                     veneer at that landing point is followed until a real body
                     is reached. report.json carries 'plt_target_addr' /
                     'plt_target_sym' / 'plt_target_dem' / 'plt_hops' /
                     'plt_chain' / 'plt_target_matches_cited'.
                     plt_hops > 1 means a SECOND veneer layer had
                     to be traversed (proven cases: the TiXml non-const
                     FirstChildElement/NextSiblingElement 4-byte stubs at
                     0x0011953c / 0x0012210c, whose real bodies are the const
                     overloads in the 0x0022xxxx tinyxml block).
  PLT-RANGE-UNMAPPED -- addr is inside the .plt address range but is NOT the
                     start of a decodable PLT entry (nor its Thumb 'bx pc'
                     interworking preamble). The address is FABRICATED, not a
                     thunk -- hand-derived ('<sym>_plt + 4' / '+ 8'), a
                     one-nibble typo (0x0010c144 for 0x0011c144), or a landing
                     inside a static-initialiser blob. This is a MORE serious
                     finding than PLT-THUNK: there is no target to restamp to,
                     the marker must be re-RE'd from scratch.
                     report.json carries 'plt_reason'.
  UNRESOLVED      -- addr not found as a known symbol start, and NOT within any
                     symbol's size range. Truly unknown.
  OK-NO-SYM       -- has v1.6.1 but no symbol name in the marker (old 'binary @')

Marker kinds recognised by _PATTERNS (task #139 widened this list): the five
grammar forms defined in CLAUDE.md -- TODO / ASM-verified / ASM-spec / DIFFERS
/ Defunct -- plus two informal-but-common bare citation forms that carry no
prefix keyword at all and were previously invisible to this lint:
  Bare-v161    -- '// v1.6.1 <Symbol> @0x<addr>' or '// v1.6.1 @0x<addr>'
                  (no symbol). Common as a plain RE citation inside an
                  ordinary explanatory comment, not attached to any of the
                  five grammar keywords.
  Bare-Binary  -- '// Binary @ 0x<addr>' / '// binary @ 0x<addr>' (old-style,
                  no version, no symbol).
These go through the exact same classify() pipeline as every other kind (OK /
STALE / MID-SYMBOL-MISMATCH / etc. all apply); they are simply excluded from
Check D (sweep corroboration) by audit-config.toml's audited_marker_kinds,
since they never claimed "ASM-verified" in the first place.

All NO-VERSION variants carry a sub-verdict:
  NO-VERSION       -- addr resolves to a matching symbol (OK except missing tag)
  NO-VERSION+STALE -- addr resolves but symbol name doesn't match
  NO-VERSION+MID-SYMBOL -- addr is inside a function body (valid mid-func ref)
  NO-VERSION+UNRESOLVED -- addr not found at all

SKIPPED (coverage counter, task #139) -- any '//' comment line that contains
    an address-like token ('@0x<addr>' or a bare '0x<addr>') but matched NONE
    of the _PATTERNS kinds above. This is the complement of the widened
    recogniser: it is printed alongside the existing 'N markers claim
    ASM-verified...' coverage line so a SEVENTH marker form appearing later in
    the tree shows up as a number that moves, never as silence. Proven cost of
    silence: 3 of 5 hand-found stale markers (Fruit.h, SlashEntity.h,
    MainScreen::Init) were invisible to this lint before task #139 because
    they used the bare forms above. report.json['skipped_markers'] carries the
    full list (file/line/raw_line/addr_str).

    KNOWN REMAINING BLIND SPOT (not fixed here, task #139 explicitly scoped it
    out): an address cited inside a LOG_INFO format STRING rather than a
    comment is invisible to this line-based, comment-only scanner by design --
    this is how the DojoScreen.cpp mis-stamp hid (see commit 7f1f6748). A
    string-literal scan is a separate, larger change (needs to avoid matching
    unrelated hex-looking data in format strings) and is intentionally not
    attempted in this pass.

Cross-cutting audit class (orthogonal to 'verdict', reported separately):

  NO-SYMBOL -- the marker names NO symbol at all (bare '// ... binary @ 0x...' /
      '// ASM-spec for binary @ 0x...'). There is nothing to cross-check, so
      such markers used to fall silently into OK-NO-SYM / MID-SYMBOL, which
      READS AS "fine". That exact shape hid both hand-proven GameSound bugs
      ('Update @0x0012930c' was list/map template code; 'Release @0x0012917c'
      was std::map::operator[]). Every symbol-less marker is now reported with
      the symbol that ACTUALLY contains (or starts at) its address, so a human
      can eyeball plausibility. Markers whose resolved symbol is STL/compiler
      internal (std::, __gnu_cxx::, _Rb_tree, _GLOBAL__, ...) are ranked HIGH:
      gameplay/UI code citing template guts is the strongest mis-stamp tell.
      Fields: 'symbol_less', 'resolved_kind' (exact/contained/plt/none),
      'resolved_addr', 'resolved_sym', 'resolved_dem', 'no_symbol_priority'.
      report.json['symbol_less_markers'] carries the full ranked list.

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

  SWEEP-CONTRADICTION (Check D) -- cross-checks every `// ASM-verified:`
      marker against the asm-verify sweep (tmp/asm-verify/report.json). The
      marker is authored by the same agent that benefits from it and NOTHING
      recomputed it, so fabricated and stale stamps have repeatedly misled
      this project. Rules cannot fix that; only mechanism can.

      A stamp is only corroborated when the cited symbol is PAIRED in the
      sweep AND the sweep's verdict is not a contradiction. Outcomes:

        CONFIRMED           -- paired, verdict in [sweep_audit].confirming_verdicts.
        CONTRADICTED        -- ERROR. Paired, but the sweep says DIVERGE /
                               FIX-NEEDED / UNPAIRED / SUSPICIOUS-FORWARDER.
                               This is shape (a), the fabricated-stamp case:
                               a stamp on a symbol the sweep says diverges.
        NAME-NOT-IN-BINARY  -- ERROR. The cited symbol resolves to ZERO binary
                               addresses; the tool cannot even pair it. This is
                               shape (b) -- e.g. FruitFactBigClassicFactPage::Init,
                               a port-only method with no binary counterpart,
                               whose marker cited the CTOR's address.
        WEAK                -- paired but the verdict neither confirms nor
                               refutes (SUSPICIOUS / ACCEPT-deferred). Warned,
                               not failed.
        CANNOT-VERIFY       -- the symbol is legitimately outside the sweep:
                               a platform file (*SDL.cpp / *Posix.cpp /
                               *Win32.cpp / src/platform/), a TU absent from
                               verify-sources.cmake, or a symbol the manifest
                               never picked up. Reported as its own QUIET
                               category -- never silently skipped, because
                               "cannot verify" is exactly the state that has
                               been passing for verified.

      The run always prints the coverage line
        "N markers claim ASM-verified, M confirmed by this run, K cannot be
         checked"
      so unverified is a number that MOVES rather than something to hunt for.
      report.json['sweep_audit'] carries the full per-marker detail.
      Verdict vocabularies / exclusion globs live in audit-config.toml.

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
import datetime
import json
import pathlib
import re
import struct
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import audit_config    # noqa: E402  (target-specific knobs; see audit-config.toml)

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
# PLT resolution (blind spot 1) / NO-SYMBOL ranking (blind spot 2)
# ---------------------------------------------------------------------------
# Ghidra loads this ELF at image_base 0x10000; LIEF reports raw ELF values.
GHIDRA_IMAGE_BASE = 0x10000

# Max thunk/veneer hops to follow before declaring a cycle. Two hops is the
# deepest proven real case (PLT stub -> 4-byte 'b' veneer -> PLT stub -> body);
# the extra headroom just keeps pathological input from looping.
PLT_MAX_HOPS = 6

# A symbol whose body is exactly this many bytes and is a single ARM 'b' is a
# branch veneer, not a real function body (trap (a)).
VENEER_SIZE = 4

# Resolved symbols matching these are STL / compiler-internal: a symbol-less
# marker in gameplay/UI code that resolves into one of these is the strongest
# mis-stamp tell (both hand-proven GameSound bugs had this shape).
_STL_INTERNAL_RE = re.compile(
    r'^std::'
    r'|^__gnu_cxx::'
    r'|^_GLOBAL__'
    r'|\b_Rb_tree\b'
    r'|\b_Vector_base\b'
    r'|\b_List_base\b'
    r'|^_ZNSt|^_ZSt|^_ZNKSt'
    r'|^_ZN9__gnu_cxx')


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
    # ------------------------------------------------------------------
    # Bare forms (task #139): no TODO:/ASM-verified:/ASM-spec/DIFFERS:/
    # Defunct: prefix keyword at all -- just a plain RE citation inside an
    # ordinary comment. Listed LAST so any line carrying one of the five
    # grammar keywords above already matched (and broke) before reaching
    # these; only lines with none of those keywords fall through to here.
    # ------------------------------------------------------------------
    # Bare v1.6.1 <Symbol> @0x<addr>
    # e.g.: // v1.6.1 Bomb::GetWait @0x0010d4cc -- thunk returning the...
    (
        "Bare-v161",
        re.compile(
            r'(?P<ver>v1\.6\.1)\s+'
            r'(?P<sym>[A-Za-z_][^\s@]+?)\s+'
            r'@\s*(?P<addr>' + _HEX_ADDR + r')'
        ),
    ),
    # Bare v1.6.1 @0x<addr>  (no symbol)
    # e.g.: // v1.6.1 @0x0011f4c4 -- reads theGame+0x104
    (
        "Bare-v161",
        re.compile(
            r'(?P<ver>v1\.6\.1)\s+'
            r'@\s*(?P<addr>' + _HEX_ADDR + r')'
        ),
    ),
    # Bare Binary @ 0x<addr>  (old-style, no version, no symbol)
    # e.g.: // Binary @ 0x0010dca8: instantiate FileSystem_Direct(0x14 bytes)
    # e.g.: if (...)  // binary @ 0x001b9989
    (
        "Bare-Binary",
        re.compile(
            r'[Bb]inary\s+'
            r'@\s*(?P<addr>' + _HEX_ADDR + r')'
        ),
    ),
]

# Bare hex-address token, used only by the SKIPPED coverage counter (task
# #139) to find comment lines that reference SOME binary address but matched
# none of the _PATTERNS kinds above.
_ANY_ADDR_RE = re.compile(_HEX_ADDR)


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


def _strip_template_args(s: str) -> str:
    """Remove ALL template argument lists from a name, including NESTED ones
    (e.g. 'Read<SmartPtr<Effect>,allocator>' -> 'Read').

    A single-level regex (r'<[^>]*>') stops at the FIRST '>', so on a nested
    template it only strips up to the inner close -- 'Read<SmartPtr<Effect>'
    becomes 'Read' but leaves the dangling ',allocator>' behind, corrupting
    the comparison and producing a false STALE verdict (task #149: this hid
    the correct 'Mortar::Read<SmartPtr<Effect>,allocator>' marker in
    Effect.cpp/.h). A depth-counting scan strips the whole balanced <...>
    span regardless of nesting depth.
    """
    out = []
    depth = 0
    for ch in s:
        if ch == '<':
            depth += 1
        elif ch == '>':
            if depth > 0:
                depth -= 1
        elif depth == 0:
            out.append(ch)
    return ''.join(out)


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
        s = _strip_template_args(s)  # strip template args (handles nesting)
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
                dem_class = _strip_template_args(dem_class).strip()
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
        s = _strip_template_args(s)
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
                dem_class = _strip_template_args(dem_parts[-2].strip()).strip()
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


_BINARY_CACHE = {}


def _parse_binary(binary_path: pathlib.Path):
    """lief.parse() the ELF once and cache it (the symbol loader and the PLT
    loader both need it; parsing this 3MB binary twice is pure waste)."""
    key = str(binary_path)
    if key not in _BINARY_CACHE:
        b = lief.parse(key)
        if b is None:
            sys.exit(f"ERROR: lief could not parse {binary_path}")
        _BINARY_CACHE[key] = b
    return _BINARY_CACHE[key]


def _iter_symbols(b):
    """Yield (ghidra_addr, mangled_name, type_str, size) for every usable ELF
    symbol, applying the shared normalisation rules:

      - skip value==0, unnamed, '$'-prefixed ARM mapping symbols, '.'-prefixed
        ELF specials, and FILE / SECTION entries;
      - mask the Thumb bit out of STT_FUNC values (see below);
      - convert to GHIDRA convention (LIEF value + 0x10000).

    ARM/Thumb: an STT_FUNC symbol value carries the Thumb bit in bit 0, so a
    Thumb function at 0x0022e544 has symbol value 0x0022e545. Source markers
    (and asm-verify's report.json) cite the real, even function start. Without
    masking, every such marker misses the addr lookup and then lands inside the
    PRECEDING function's [start, start+size) range, producing a bogus
    MID-SYMBOL-MISMATCH whose "correct" address is the Thumb-tagged one --
    applying that suggestion would corrupt a correct marker.
    """
    for sym in b.symbols:
        raw_val = sym.value
        if raw_val == 0:
            continue
        name = sym.name
        if not name:
            continue
        if name.startswith('$') or name.startswith('.'):
            continue
        sym_type_str = str(sym.type)
        if sym_type_str in ('TYPE.FILE', 'TYPE.SECTION'):
            continue
        if sym_type_str == 'TYPE.FUNC':
            raw_val &= ~1
        yield (raw_val + GHIDRA_IMAGE_BASE, name, sym_type_str, sym.size)


def load_binary_symbols(binary_path: pathlib.Path):
    """Return (addr_to_mangled, addr_to_demangled, sym_ranges) from the ELF.

    addr_to_mangled:  {int_addr: mangled_name}
    addr_to_demangled:{int_addr: demangled_name}
    sym_ranges:       sorted list of (start_addr, end_addr, mangled_name) for
                      all symbols with size > 0. Used for CONTAINMENT check.

    All addresses use GHIDRA convention: LIEF_value + 0x10000.
    Validated anchors (Ghidra convention, confirmed via GhidraMCP):
      GameOverScreen::Update  @ 0x00186c80  (LIEF 0x176c80 + 0x10000)
      Fruit::RandomFruit      @ 0x001dc5d8  (LIEF 0x1cc5d8 + 0x10000)
      Fruit::CollisionResponse@ 0x001dd500  (LIEF 0x1cd500 + 0x10000)
    """
    b = _parse_binary(binary_path)

    addr_to_mangled = {}
    addr_to_demangled = {}
    sym_ranges = []

    for (addr, name, sym_type_str, size) in _iter_symbols(b):
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
        if size > 0:
            sym_ranges.append((addr, addr + size, name))

    # Sort ranges by start address for binary-search efficiency
    sym_ranges.sort(key=lambda t: t[0])

    return addr_to_mangled, addr_to_demangled, sym_ranges


def _arm_expand_imm(word: int) -> int:
    """Decode an ARM data-processing modified-immediate (imm8 ror 2*rot)."""
    imm8 = word & 0xff
    rot  = ((word >> 8) & 0xf) * 2
    if rot == 0:
        return imm8
    return ((imm8 >> rot) | (imm8 << (32 - rot))) & 0xffffffff


def load_plt_info(binary_path: pathlib.Path) -> dict:
    """Decode the .plt import-stub block and build a thunk -> target map.

    Resolution is done from REAL ELF data, not a heuristic:
      1. Every .plt entry is decoded as the classic 3-instruction ARM PLT stub
             add ip, pc, #imm ; add ip, ip, #imm ; ldr pc, [ip, #imm]!
         which yields the exact .got.plt slot it dereferences.
      2. That slot is matched against the ARM_JUMP_SLOT relocations in
         .rel.plt, giving the target's MANGLED SYMBOL NAME.
      3. The name is looked up in the symbol table for its definition address.
    A stub preceded by the 4-byte Thumb interworking preamble 'bx pc ; nop'
    (46c0 4778) is aliased so a marker citing the preamble address resolves to
    the same target.

    Returns a dict with:
      plt_start / plt_end   -- Ghidra address range of .plt (end exclusive)
      entry_target          -- {ghidra_plt_addr: mangled_target_name}
      name_to_addrs         -- {mangled: [ghidra_addr, ...]} definition sites
      sections              -- [(start, end, bytes)] for code reads
      addr_to_size          -- {ghidra_addr: size} (veneer detection)
      counts                -- decode statistics (informational)
    """
    b = _parse_binary(binary_path)

    sections = []
    plt_start = plt_end = None
    plt_bytes = None
    for s in b.sections:
        va = s.virtual_address
        if va == 0:
            continue
        try:
            content = bytes(s.content)
        except Exception:
            continue
        if not content:
            continue
        start = va + GHIDRA_IMAGE_BASE
        sections.append((start, start + len(content), content))
        if s.name == '.plt':
            plt_start, plt_end, plt_bytes = start, start + len(content), content
    sections.sort(key=lambda t: t[0])

    info = {
        'plt_start':    plt_start,
        'plt_end':      plt_end,
        'entry_target': {},
        'name_to_addrs': {},
        'sections':     sections,
        'addr_to_size': {},
        'counts':       {'entries': 0, 'thumb_aliases': 0, 'undecoded_words': 0,
                         'no_reloc': 0},
    }

    # Definition sites + sizes (same normalisation as load_binary_symbols).
    for (addr, name, sym_type_str, size) in _iter_symbols(b):
        lst = info['name_to_addrs'].setdefault(name, [])
        if addr not in lst:
            lst.append(addr)
        if size > info['addr_to_size'].get(addr, 0):
            info['addr_to_size'][addr] = size

    if plt_bytes is None:
        return info

    # GOT slot -> target symbol name, from the ARM_JUMP_SLOT relocations.
    got_to_name = {}
    for r in b.pltgot_relocations:
        if r.has_symbol and r.symbol is not None and r.symbol.name:
            got_to_name[r.address] = r.symbol.name

    n = len(plt_bytes)
    i = 0x14                       # skip the 20-byte PLT0 header
    prev_was_thumb_preamble_at = None
    while i + 12 <= n:
        w0, w1, w2 = struct.unpack_from('<3I', plt_bytes, i)
        is_entry = ((w0 & 0xffffff00) == 0xe28fc600 and     # add ip, pc, #imm
                    (w1 & 0xfffff000) == 0xe28cc000 and     # add ip, ip, #imm
                    (w2 & 0xfffff000) == 0xe5bcf000)        # ldr pc, [ip,#imm]!
        if is_entry:
            va  = plt_start + i
            # pc reads as (insn addr + 8); the GOT slot is a LIEF/ELF address,
            # so strip the Ghidra base back off before matching relocations.
            got = ((va - GHIDRA_IMAGE_BASE) + 8
                   + _arm_expand_imm(w0) + _arm_expand_imm(w1)
                   + (w2 & 0xfff)) & 0xffffffff
            name = got_to_name.get(got)
            if name is None:
                info['counts']['no_reloc'] += 1
            else:
                info['entry_target'][va] = name
                info['counts']['entries'] += 1
                if prev_was_thumb_preamble_at is not None:
                    info['entry_target'][prev_was_thumb_preamble_at] = name
                    info['counts']['thumb_aliases'] += 1
            prev_was_thumb_preamble_at = None
            i += 12
            continue
        # 'bx pc ; nop' Thumb->ARM interworking preamble immediately preceding
        # an entry (encoded as the single word 0x46c04778).
        if w0 == 0x46c04778:
            prev_was_thumb_preamble_at = plt_start + i
        else:
            prev_was_thumb_preamble_at = None
            info['counts']['undecoded_words'] += 1
        i += 4

    return info


def _read_u32(plt_info: dict, addr: int):
    """Read a little-endian 32-bit word at a Ghidra address, or None."""
    for (start, end, data) in plt_info['sections']:
        if start <= addr and addr + 4 <= end:
            return struct.unpack_from('<I', data, addr - start)[0]
    return None


def _branch_veneer_target(addr: int, plt_info: dict):
    """If addr is a 4-byte symbol whose single instruction is an unconditional
    ARM 'b <target>', return the branch target; else None.

    This is trap (a): some .text "bodies" are themselves 4-byte veneers that
    branch straight back into the .plt (proven: the TiXml non-const
    FirstChildElement @0x0011953c and NextSiblingElement @0x0012210c, whose
    real bodies are the const overloads in the 0x0022xxxx tinyxml block). A
    resolver that stops at the first non-thunk hit reports the veneer as if it
    were the answer.
    """
    if plt_info['addr_to_size'].get(addr) != VENEER_SIZE:
        return None
    w = _read_u32(plt_info, addr)
    if w is None or (w & 0xff000000) != 0xea000000:   # cond=AL, B (imm24)
        return None
    imm = w & 0x00ffffff
    if imm & 0x00800000:
        imm -= 0x01000000
    return (addr + 8 + imm * 4) & 0xffffffff


def _pick_definition(name: str, plt_info: dict):
    """Pick the definition address for a mangled name, preferring the largest
    (a 4-byte veneer and a real body can share a name across a build)."""
    addrs = plt_info['name_to_addrs'].get(name) or []
    if not addrs:
        return None
    return max(addrs, key=lambda a: (plt_info['addr_to_size'].get(a, 0), -a))


def resolve_plt_chain(addr: int, plt_info: dict,
                      addr_to_mangled: dict, addr_to_demangled: dict) -> dict:
    """Follow a .plt thunk (and any 4-byte 'b' veneer behind it) to a real body.

    Returns a dict:
      status  -- 'ok'        : final_addr is a real body
                 'unmapped'  : addr is inside .plt but is not a decodable entry
                               (FABRICATED address -- see PLT-RANGE-UNMAPPED)
                 'no-def'    : the relocation names a symbol with no definition
                 'cycle'     : hop limit / loop hit
      chain       -- list of {kind, from, sym, to} hops taken
      final_addr / final_sym / final_dem
      hops        -- number of thunk/veneer hops traversed (2+ == trap (a))
    """
    out = {'status': 'unmapped', 'chain': [], 'final_addr': None,
           'final_sym': None, 'final_dem': None, 'hops': 0, 'note': None}
    plt_start, plt_end = plt_info['plt_start'], plt_info['plt_end']
    if plt_start is None:
        out['note'] = 'no .plt section in binary'
        return out

    cur = addr
    seen = set()
    for _ in range(PLT_MAX_HOPS):
        if cur in seen:
            out['status'] = 'cycle'
            return out
        seen.add(cur)

        if plt_start <= cur < plt_end:
            name = plt_info['entry_target'].get(cur)
            if name is None:
                out['status'] = 'unmapped'
                out['note'] = ('0x%08x is inside .plt but is not the start of a '
                               'decodable PLT entry' % cur)
                return out
            tgt = _pick_definition(name, plt_info)
            if tgt is None:
                out['status'] = 'no-def'
                out['final_sym'] = name
                out['final_dem'] = _demangle(name)
                out['note'] = 'relocation target has no definition in symtab'
                return out
            out['chain'].append({'kind': 'plt', 'from': hex(cur),
                                 'sym': name, 'to': hex(tgt)})
            out['hops'] += 1
            cur = tgt
            continue

        vt = _branch_veneer_target(cur, plt_info)
        if vt is not None:
            out['chain'].append({'kind': 'veneer', 'from': hex(cur),
                                 'sym': addr_to_mangled.get(cur),
                                 'to': hex(vt)})
            out['hops'] += 1
            cur = vt
            continue

        out['status']     = 'ok'
        out['final_addr'] = cur
        out['final_sym']  = addr_to_mangled.get(cur)
        out['final_dem']  = addr_to_demangled.get(cur) or out['final_sym']
        return out

    out['status'] = 'cycle'
    return out


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


def scan_sources(src_dir: pathlib.Path):
    """Scan all .cpp/.h files under src_dir for RE markers.

    Returns (markers, skipped):
      markers  -- list of dicts: file, line, raw_line, kind, has_version,
                  cited_sym (or None), cited_addr (int).
      skipped  -- list of dicts (task #139 coverage counter): '//' comment
                  lines that contain an address-like token ('@0x<addr>' or a
                  bare '0x<addr>') but matched NONE of the _PATTERNS kinds.
                  file, line, raw_line, addr_str.
    """
    results = []
    skipped = []
    extensions = {'.cpp', '.h', '.cc', '.cxx'}

    for fpath in sorted(src_dir.rglob('*')):
        if fpath.suffix not in extensions:
            continue
        try:
            text = fpath.read_text(encoding='utf-8', errors='replace')
        except Exception:
            continue

        for lineno, raw in enumerate(text.splitlines(), 1):
            matched = False
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
                matched = True
                break   # stop at first matching pattern for this line

            if matched:
                continue

            # ----------------------------------------------------------
            # SKIPPED coverage counter (task #139): a '//' comment that
            # cites SOME address but matched no recognised marker kind.
            # Never silent -- see module docstring.
            # ----------------------------------------------------------
            cidx = raw.find('//')
            if cidx == -1:
                continue
            comment = raw[cidx:]
            addr_m = _ANY_ADDR_RE.search(comment)
            if addr_m:
                skipped.append({
                    'file':     str(fpath.relative_to(src_dir.parent)),
                    'line':     lineno,
                    'raw_line': raw.strip(),
                    'addr_str': addr_m.group(0),
                })

    return results, skipped


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
             sym_ranges: list,
             plt_info: dict = None) -> list:
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

            # ------------------------------------------------------------
            # Step 2a: .plt import-stub check (blind spot 1). A cited address
            # inside .plt is NEVER the function body -- Ghidra names PLT stubs
            # after their target, so name-search-then-take-first-hit records
            # the thunk. Resolve it, or call out a fabricated address.
            # ------------------------------------------------------------
            if (plt_info and plt_info.get('plt_start') is not None
                    and plt_info['plt_start'] <= addr < plt_info['plt_end']):
                res = resolve_plt_chain(addr, plt_info,
                                        addr_to_mangled, addr_to_demangled)
                m['plt_chain'] = res['chain']
                m['plt_hops']  = res['hops']
                if res['status'] == 'ok':
                    m['verdict']         = 'PLT-THUNK'
                    m['plt_target_addr'] = res['final_addr']
                    m['plt_target_sym']  = res['final_sym']
                    m['plt_target_dem']  = res['final_dem']
                    # Extra signal: does the resolved body actually match the
                    # name the marker cites? A mismatch means the restamp
                    # target still needs a human look.
                    if cited_sym and res['final_sym']:
                        m['plt_target_matches_cited'] = _symbol_matches(
                            cited_sym, res['final_sym'], res['final_dem'] or '')
                else:
                    m['verdict']    = 'PLT-RANGE-UNMAPPED'
                    m['plt_reason'] = res.get('note') or res['status']
                    m['plt_status'] = res['status']
                    if res.get('final_sym'):
                        m['plt_target_sym'] = res['final_sym']
                        m['plt_target_dem'] = res['final_dem']
                continue

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
    'PLT-RANGE-UNMAPPED':      0,
    'MID-SYMBOL-MISMATCH':     1,
    'PLT-THUNK':               2,
    'STALE-MISMATCH':          3,
    'STALE-AMBIGUOUS':         4,
    'STALE':                   5,
    'NO-VERSION+STALE':        6,
    'NO-VERSION':              7,
    'NO-VERSION+MID-SYMBOL':   8,
    'NO-VERSION+UNRESOLVED':   9,
    'UNRESOLVED':              10,
    'MID-SYMBOL':              11,
    'OK-NO-SYM':               12,
    'OK':                      13,
}

# Verdicts that --check treats as actionable bugs (exit non-zero).
# PLT-THUNK / PLT-RANGE-UNMAPPED are carved out of what used to be reported as
# UNRESOLVED; both are hard bugs (the marker cites an import stub, or an
# address that is not code at all), so they gate --check.
_CHECK_FAIL_VERDICTS = {'MID-SYMBOL-MISMATCH', 'STALE-MISMATCH', 'STALE',
                        'PLT-THUNK', 'PLT-RANGE-UNMAPPED'}
# Additionally failed under --strict.
_CHECK_STRICT_EXTRA = {'NO-VERSION', 'NO-VERSION+STALE'}


def _is_stl_internal(mangled: str, demangled: str) -> bool:
    """True if the symbol is STL / compiler-internal template guts."""
    for s in (demangled or '', mangled or ''):
        if s and _STL_INTERNAL_RE.search(s):
            return True
    return False


def annotate_symbol_less(markers: list) -> list:
    """Blind spot 2: promote symbol-less markers to a first-class audit class.

    A marker like '// Binary @ 0x00129138' names no symbol, so there is nothing
    to cross-check and it silently files as OK-NO-SYM / MID-SYMBOL -- which
    reads as "fine". Both hand-proven GameSound bugs had exactly this shape.

    For every marker with no cited symbol we record WHAT THE ADDRESS ACTUALLY
    IS (exact symbol start / containing function / PLT target), so a human can
    eyeball whether it is plausible for the surrounding code. Markers resolving
    into STL / compiler-internal template code are ranked HIGH -- gameplay or
    UI source citing std::_Rb_tree guts is the strongest mis-stamp tell.

    Returns the ranked list of symbol-less marker rows (also annotated in
    place on the marker dicts).
    """
    rows = []
    for m in markers:
        if m.get('cited_sym'):
            m['symbol_less'] = False
            continue
        m['symbol_less'] = True

        if m.get('binary_sym'):
            kind = 'exact'
            r_addr = m['cited_addr']
            r_sym  = m['binary_sym']
            r_dem  = m['binary_demangled'] or r_sym
        elif m.get('containing_sym'):
            kind = 'contained'
            r_addr = m.get('containing_addr')
            r_sym  = m['containing_sym']
            r_dem  = m.get('containing_dem') or r_sym
        elif m.get('plt_target_sym'):
            kind = 'plt'
            r_addr = m.get('plt_target_addr')
            r_sym  = m['plt_target_sym']
            r_dem  = m.get('plt_target_dem') or r_sym
        else:
            kind = 'none'
            r_addr = r_sym = r_dem = None

        priority = 'HIGH' if (r_sym and _is_stl_internal(r_sym, r_dem)) else 'NORMAL'
        # An address that resolves to nothing at all is not "fine" either.
        if kind == 'none':
            priority = 'HIGH'

        m['resolved_kind']      = kind
        m['resolved_addr']      = r_addr
        m['resolved_sym']       = r_sym
        m['resolved_dem']       = r_dem
        m['no_symbol_priority'] = priority

        rows.append({
            'file':      m['file'],
            'line':      m['line'],
            'kind':      m['kind'],
            'verdict':   m['verdict'],
            'addr_str':  m['addr_str'],
            'resolved_kind':      kind,
            'resolved_addr':      r_addr,
            'resolved_sym':       r_sym,
            'resolved_dem':       r_dem,
            'no_symbol_priority': priority,
        })
    rows.sort(key=lambda r: (r['no_symbol_priority'] != 'HIGH',
                             r['file'], r['line']))
    return rows


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


# ---------------------------------------------------------------------------
# Check D (SWEEP-CONTRADICTION) -- corroborate ASM-verified markers against
# the asm-verify sweep. See module docstring.
# ---------------------------------------------------------------------------
_VERIFY_SRC_RE = re.compile(r'\$\{_PROJECT_ROOT\}/(src/[^"\s]+)')


def _ctor_matches(cited_sym: str, demangled: str) -> bool:
    """True when a marker spells a constructor `X::X` and the demangled name is
    the itanium `X::{ctor}` / `X::{base ctor}` form.

    Deliberately NOT folded into _symbol_matches_strict: that matcher drives the
    --fix auto-rewriter, and widening it would change which addresses the fixer
    considers 'the one candidate'. Check D only needs the recognition, not the
    rewrite. (Without this, every ctor marker -- HUD::HUD, WaveInfo::WaveInfo --
    reads as 'cited symbol not in the binary', a pure false positive.)
    """
    if not cited_sym or not demangled:
        return False
    cited_base = re.sub(r'\(.*', '', cited_sym).strip()
    dem_base   = re.sub(r'\(.*', '', demangled).strip()
    if 'ctor' not in dem_base:
        return False
    strip_t = _strip_template_args
    cited_parts = strip_t(cited_base).split('::')
    dem_parts   = strip_t(dem_base).split('::')
    if len(cited_parts) < 2 or len(dem_parts) < 2:
        return False
    if cited_parts[-1] != cited_parts[-2]:
        return False                      # not spelled X::X
    return dem_parts[-2].strip() == cited_parts[-2].strip()


def _cited_matches(cited_sym: str, mangled: str, demangled: str) -> bool:
    """Union of every name-equivalence Check D accepts: strict, loose, ctor."""
    if not cited_sym:
        return False
    dem = demangled or ''
    return (_symbol_matches_strict(cited_sym, mangled or '', dem)
            or _symbol_matches(cited_sym, mangled or '', dem)
            or _ctor_matches(cited_sym, dem))


def load_verify_sources(cmake_path: pathlib.Path):
    """Set of project-relative source paths the cross-build actually compiles.

    A marker in a TU that the sweep never compiles can NEVER be corroborated by
    it -- that is 'cannot verify', not an error. Returns None when the file is
    missing (freshness/tooling problem; the caller reports it loudly rather
    than silently treating every TU as compiled)."""
    if not cmake_path.exists():
        return None
    text = cmake_path.read_text(encoding='utf-8', errors='replace')
    return set(m.group(1) for m in _VERIFY_SRC_RE.finditer(text))


def _report_freshness(report_path: pathlib.Path, project_root: pathlib.Path):
    """(is_stale, message) -- report.json vs the newest commit touching src/."""
    if not report_path.exists():
        return True, "sweep report not found: %s" % report_path
    r_mtime = report_path.stat().st_mtime
    try:
        out = subprocess.run(["git", "log", "-1", "--format=%ct", "--", "src"],
                             cwd=str(project_root), capture_output=True,
                             text=True, check=True)
        src_ct = int(out.stdout.strip())
    except Exception as e:
        return False, "git log for src/ unavailable (%s); freshness unknown" % e
    if r_mtime < src_ct:
        f = lambda t: datetime.datetime.fromtimestamp(t).strftime('%Y-%m-%d %H:%M')
        return True, ("sweep report (%s) is OLDER than the newest commit touching "
                      "src/ (%s) -- Check D below may be corroborating code that "
                      "no longer exists. Re-run tools/asm-verify/run.sh."
                      % (f(r_mtime), f(src_ct)))
    return False, "sweep report is newer than the last src/ commit"


def check_sweep_contradictions(markers: list,
                               sweep_rows: list,
                               verify_sources,
                               addr_to_mangled: dict,
                               addr_to_demangled: dict,
                               cfg) -> dict:
    """Check D: every ASM-verified marker must be PAIRED in the sweep and must
    not carry a contradicting verdict. Returns a dict with per-outcome lists
    plus the coverage counts.

    Join order (most specific first):
      1. the sweep row whose binary address IS the cited address;
      2. the sweep row for the mangled symbol that lives at the cited address;
      3. the sweep row for the cited SYMBOL NAME's real address(es), found by
         the strict forward lookup (covers a marker whose address drifted but
         whose name is right).
    """
    sa = cfg.sweep_audit
    audited_kinds = set(sa.audited_marker_kinds)
    confirming    = set(sa.confirming_verdicts)
    contradicting = set(sa.contradicting_verdicts)
    weak          = set(sa.weak_verdicts)
    platform_res  = sa.regexes('platform_file_res')

    by_addr, by_mangled = {}, {}
    for row in sweep_rows:
        try:
            by_addr[int(row['addr'], 16)] = row
        except (KeyError, TypeError, ValueError):
            pass
        if row.get('mangled'):
            by_mangled.setdefault(row['mangled'], row)

    # .h markers inherit the compiled-ness of their same-stem .cpp.
    stem_to_src = {}
    if verify_sources is not None:
        for p in verify_sources:
            stem_to_src.setdefault(pathlib.PurePosixPath(p).stem, p)

    out = {'confirmed': [], 'contradicted': [], 'name_not_in_binary': [],
           'weak': [], 'cannot_verify': [], 'total': 0}

    for m in markers:
        if m['kind'] not in audited_kinds:
            continue
        out['total'] += 1
        f_rel = m['file'].replace('\\', '/')
        cited = m.get('cited_sym')
        rec = {
            'file': m['file'], 'line': m['line'], 'cited_sym': cited,
            'addr_str': m['addr_str'], 'lint_verdict': m['verdict'],
        }

        # --- exclusions that make corroboration structurally impossible -----
        if any(r.search(f_rel) for r in platform_res):
            rec['reason'] = 'platform file (no binary counterpart)'
            out['cannot_verify'].append(rec)
            continue
        if verify_sources is not None:
            src_key = f_rel if f_rel in verify_sources else \
                stem_to_src.get(pathlib.PurePosixPath(f_rel).stem)
            if src_key is None:
                rec['reason'] = 'TU not in verify-sources.cmake (sweep never compiles it)'
                out['cannot_verify'].append(rec)
                continue

        # --- join to a sweep row -------------------------------------------
        row = by_addr.get(m['cited_addr'])
        if row is None and m.get('binary_sym'):
            row = by_mangled.get(m['binary_sym'])
        strict = _forward_lookup_strict(cited, addr_to_mangled,
                                        addr_to_demangled) if cited else []
        if row is None:
            for (a, _d) in strict:
                if a in by_addr:
                    row = by_addr[a]
                    rec['joined_via'] = 'cited symbol name @ 0x%08x' % a
                    break

        if row is None:
            # Nothing to diff against. Is that because the NAME does not exist
            # in the binary at all (shape (b), an ERROR), or because the sweep
            # manifest simply never picked the symbol up (cannot verify)?
            name_here_matches = bool(
                cited and (
                    _cited_matches(cited, m.get('binary_sym'),
                                   m.get('binary_demangled'))
                    or _cited_matches(cited, m.get('containing_sym'),
                                      m.get('containing_dem'))
                    or _cited_matches(cited, m.get('plt_target_sym'),
                                      m.get('plt_target_dem'))))
            if cited and not strict and not name_here_matches:
                rec['reason'] = ('cited symbol resolves to NO binary address -- '
                                 'port-only method; the marker cites some other '
                                 'function\'s address')
                if m.get('binary_demangled'):
                    rec['addr_actually_is'] = m['binary_demangled']
                elif m.get('containing_dem'):
                    rec['addr_actually_is'] = m['containing_dem'] + ' (containing)'
                out['name_not_in_binary'].append(rec)
            else:
                rec['reason'] = ('symbol not in the sweep manifest '
                                 '(never diffed this run)')
                out['cannot_verify'].append(rec)
            continue

        rec['sweep_symbol'] = row.get('mangled')
        rec['sweep_verdict'] = row.get('verdict')
        rec['sweep_reason'] = row.get('reason')
        rec['score'] = row.get('score')
        rec['max_score'] = row.get('max_score')
        if row.get('port_mangled'):
            rec['sweep_port_symbol'] = row['port_mangled']
        if row.get('pairing_suspect'):
            rec['pairing_suspect'] = row['pairing_suspect']

        # A stamp that cites a DIFFERENT function than the one the sweep diffed
        # at this address is shape (b) even though a row exists.
        if cited and row.get('mangled'):
            dem = _demangle(row['mangled'])
            # An aliased row diffs the port symbol named by `port_mangled`, so a
            # marker spelling the PORT's name for it is correct, not a mis-stamp.
            alias_dem = _demangle(row['port_mangled']) if row.get('port_mangled') else ''
            if not (_cited_matches(cited, row['mangled'], dem)
                    or (alias_dem and _cited_matches(cited, row['port_mangled'],
                                                     alias_dem))):
                if not strict:
                    rec['reason'] = ('cited symbol resolves to NO binary address; '
                                     'the cited addr belongs to %s' % dem)
                    rec['addr_actually_is'] = dem
                    out['name_not_in_binary'].append(rec)
                    continue

        v = row.get('verdict')
        if v in contradicting:
            rec['reason'] = 'sweep verdict %s contradicts the ASM-verified stamp' % v
            out['contradicted'].append(rec)
        elif v in confirming:
            out['confirmed'].append(rec)
        elif v in weak:
            rec['reason'] = 'sweep verdict %s neither confirms nor refutes' % v
            out['weak'].append(rec)
        else:
            rec['reason'] = 'sweep verdict %r is not in any configured set' % v
            out['weak'].append(rec)

    for key in ('contradicted', 'name_not_in_binary', 'weak', 'cannot_verify'):
        out[key].sort(key=lambda r: (r['file'], r['line']))
    return out


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
    ap.add_argument('--sweep-report', default=None,
                    help='Path to the asm-verify report.json for Check D '
                         '(default: [sweep_audit].report_path in audit-config.toml)')
    ap.add_argument('--no-sweep-check', action='store_true',
                    help='skip Check D (SWEEP-CONTRADICTION)')
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

    print("Decoding .plt import stubs (relocation-backed) ...", flush=True)
    plt_info = load_plt_info(binary_path)
    if plt_info['plt_start'] is None:
        print("  WARNING: no .plt section found -- PLT-THUNK detection disabled.")
    else:
        pc = plt_info['counts']
        print(f"  .plt 0x{plt_info['plt_start']:08x}-0x{plt_info['plt_end'] - 1:08x}"
              f"  {pc['entries']} entries, {pc['thumb_aliases']} thumb aliases,"
              f" {pc['no_reloc']} unrelocated, {pc['undecoded_words']} undecoded words.")

    # addr -> size for exact symbol-start lookups (Check A). Keep the largest
    # when multiple size-bearing symbols share a start address (thunks).
    addr_to_size = {}
    for (start, end, _mangled) in sym_ranges:
        sz = end - start
        if sz > addr_to_size.get(start, 0):
            addr_to_size[start] = sz

    print(f"Scanning {src_dir} for RE markers ...", flush=True)
    markers, skipped_lines = scan_sources(src_dir)
    print(f"  {len(markers)} markers found.")
    print(f"  {len(skipped_lines)} SKIPPED (address-bearing comment, no "
          f"recognised marker kind).")

    print("Classifying (forward + containment + PLT checks) ...", flush=True)
    markers = classify(markers, addr_to_mangled, addr_to_demangled, sym_ranges,
                       plt_info)
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
        markers, skipped_lines = scan_sources(src_dir)
        markers = classify(markers, addr_to_mangled, addr_to_demangled, sym_ranges,
                           plt_info)
        markers = _dedupe_sort(markers)

    # -----------------------------------------------------------------------
    # NO-SYMBOL audit class (blind spot 2): annotate every symbol-less marker
    # with what its address ACTUALLY is, ranked STL-internal-first.
    # -----------------------------------------------------------------------
    symbol_less_markers = annotate_symbol_less(markers)
    _nosym_high = sum(1 for r in symbol_less_markers
                      if r['no_symbol_priority'] == 'HIGH')
    print(f"  {len(symbol_less_markers)} symbol-less marker(s) "
          f"({_nosym_high} HIGH priority).")

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

    # -----------------------------------------------------------------------
    # Check D: SWEEP-CONTRADICTION -- corroborate every ASM-verified marker
    # against the asm-verify sweep. Nothing else in this project recomputes an
    # ASM-verified stamp, so this is the ONLY mechanism behind it.
    # -----------------------------------------------------------------------
    cfg = audit_config.load()
    # Config paths are project-relative and anchored on the REPO, not on --src:
    # pointing --src at a scratch tree must still read the real sweep report and
    # the real verify-sources.cmake.
    project_root = PROJECT_ROOT
    sweep = None
    sweep_stale = False
    sweep_msg = 'skipped (--no-sweep-check)'
    if not args.no_sweep_check:
        report_path = pathlib.Path(args.sweep_report) if args.sweep_report else \
            project_root / cfg.sweep_audit.report_path
        sweep_stale, sweep_msg = _report_freshness(report_path, project_root)
        print(f"Cross-checking ASM-verified markers against the sweep (Check D) ...",
              flush=True)
        if not report_path.exists():
            print(f"  ERROR: {sweep_msg}")
            print(f"  Check D CANNOT RUN -- every ASM-verified marker is unverified.")
        else:
            if sweep_stale and cfg.sweep_audit.warn_if_report_older_than_src:
                print("  " + "!" * 68)
                print("  !! STALE INPUT: " + sweep_msg)
                print("  " + "!" * 68)
            try:
                sweep_data = json.loads(report_path.read_text(encoding='utf-8'))
            except Exception as e:
                sys.exit(f"ERROR: cannot parse sweep report {report_path}: {e}")
            sweep_rows = sweep_data.get('symbols') or []
            if not sweep_rows:
                sys.exit(f"ERROR: {report_path} has no 'symbols' array -- "
                         f"truncated or wrong file")
            vs_path = project_root / cfg.sweep_audit.verify_sources_cmake
            verify_sources = load_verify_sources(vs_path)
            if verify_sources is None:
                print(f"  WARNING: {vs_path} not found -- cannot tell a "
                      f"non-compiled TU from a genuinely unpaired symbol.")
            sweep = check_sweep_contradictions(markers, sweep_rows, verify_sources,
                                               addr_to_mangled, addr_to_demangled,
                                               cfg)
            sweep['report_path']  = str(report_path)
            sweep['report_stale'] = sweep_stale
            sweep['paired_symbols_in_sweep'] = len(sweep_rows)
            print(f"  {sweep['total']} ASM-verified marker(s): "
                  f"{len(sweep['confirmed'])} confirmed, "
                  f"{len(sweep['contradicted'])} CONTRADICTED, "
                  f"{len(sweep['name_not_in_binary'])} NAME-NOT-IN-BINARY, "
                  f"{len(sweep['weak'])} weak, "
                  f"{len(sweep['cannot_verify'])} cannot-verify.")

    # Write JSON report
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open('w', encoding='utf-8') as f:
        json.dump({
            'markers':             markers,
            'skipped_markers':     skipped_lines,
            'hollow_markers':      hollow_markers,
            'symbol_less_markers': symbol_less_markers,
            'deferred_no_blocker': deferred_no_blocker,
            'deferred_high_ratio': deferred_high_ratio,
            'sweep_audit':         sweep,
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
    print(f"  {'SKIPPED (unrecognised)':<32}: {len(skipped_lines)}")
    print(f"  {'HOLLOW-MARKER':<32}: {len(hollow_markers)}")
    print(f"  {'NO-SYMBOL (audit class)':<32}: {len(symbol_less_markers)} "
          f"({_nosym_high} HIGH)")
    print(f"  {'DEFER-NO-BLOCKER':<32}: {len(deferred_no_blocker)}")
    print(f"  {'DEFERRED-HIGH-RATIO':<32}: {len(deferred_high_ratio)}")
    if sweep is not None:
        print(f"  {'SWEEP-CONTRADICTED':<32}: {len(sweep['contradicted'])}")
        print(f"  {'NAME-NOT-IN-BINARY':<32}: {len(sweep['name_not_in_binary'])}")
        print(f"  {'SWEEP-CANNOT-VERIFY':<32}: {len(sweep['cannot_verify'])}")
    print()

    # --- Check D coverage line. The whole point: unverified is a NUMBER that
    # moves, not something to hunt for.
    if sweep is not None:
        n = sweep['total']
        m_ok = len(sweep['confirmed'])
        k = (len(sweep['cannot_verify']) + len(sweep['weak'])
             + len(sweep['contradicted']) + len(sweep['name_not_in_binary']))
        print("=" * 70)
        print(f"  {n} markers claim ASM-verified, {m_ok} confirmed by this run, "
              f"{k} cannot be checked")
        print(f"  (of the {k}: {len(sweep['contradicted'])} CONTRADICTED, "
              f"{len(sweep['name_not_in_binary'])} NAME-NOT-IN-BINARY, "
              f"{len(sweep['weak'])} weak verdict, "
              f"{len(sweep['cannot_verify'])} outside the sweep)")
        if sweep.get('report_stale'):
            print(f"  WARNING: {sweep_msg}")
        print("=" * 70)
        print()
    elif not args.no_sweep_check:
        print("=" * 70)
        print("  Check D DID NOT RUN -- 0 of the ASM-verified markers are "
              "corroborated.")
        print(f"  {sweep_msg}")
        print("=" * 70)
        print()

    # --- Check D detail: the two ERROR shapes first, then the quiet class.
    if sweep is not None:
        if sweep['contradicted']:
            print(f"--- SWEEP-CONTRADICTED ({len(sweep['contradicted'])}) -- ERROR: marker "
                  f"claims ASM-verified but the sweep disagrees [shape (a)] ---")
            for r in sweep['contradicted']:
                print(f"  {r['file']}:{r['line']}  {r['cited_sym'] or '(none)'} "
                      f"@ {r['addr_str']}")
                print(f"    sweep: {r['sweep_verdict']}  "
                      f"{_ascii_safe(str(r.get('sweep_reason')))[:70]}  "
                      f"score={r.get('score')}/{r.get('max_score')}")
                if r.get('pairing_suspect'):
                    print(f"    NOTE: pairing itself is suspect "
                          f"({r['pairing_suspect'].get('shape')}) -- see detect-forwarders.py")
            print()
        if sweep['name_not_in_binary']:
            print(f"--- NAME-NOT-IN-BINARY ({len(sweep['name_not_in_binary'])}) -- ERROR: the "
                  f"tool cannot even pair the cited symbol [shape (b)] ---")
            for r in sweep['name_not_in_binary']:
                print(f"  {r['file']}:{r['line']}  {r['cited_sym'] or '(none)'} "
                      f"@ {r['addr_str']}")
                print(f"    {_ascii_safe(r['reason'])}")
                if r.get('addr_actually_is'):
                    print(f"    addr actually is: "
                          f"{_ascii_safe(r['addr_actually_is'])[:70]}")
            print()
        if sweep['weak']:
            print(f"--- SWEEP-WEAK ({len(sweep['weak'])}) -- paired, but the verdict neither "
                  f"confirms nor refutes ---")
            for r in sweep['weak'][:20]:
                print(f"  {r['file']}:{r['line']}  {r['cited_sym'] or '(none)'} "
                      f"@ {r['addr_str']}  -> {r['sweep_verdict']}")
            if len(sweep['weak']) > 20:
                print(f"  ... and {len(sweep['weak']) - 20} more")
            print()
        if sweep['cannot_verify']:
            from collections import Counter as _C
            reasons = _C(r['reason'] for r in sweep['cannot_verify'])
            print(f"--- SWEEP-CANNOT-VERIFY ({len(sweep['cannot_verify'])}) -- outside the "
                  f"sweep; NOT an error, but NOT verified either ---")
            for why, cnt in reasons.most_common():
                print(f"  {cnt:>4}  {_ascii_safe(why)}")
            print(f"  (per-marker list in report.json['sweep_audit']['cannot_verify'])")
            print()

    # --- SKIPPED (task #139 coverage counter) -- address-bearing comment that
    # matched no recognised marker kind. See module docstring.
    if skipped_lines:
        print(f"--- SKIPPED ({len(skipped_lines)}) -- address-bearing comment, "
              f"no recognised marker kind (first 20) ---")
        for s in skipped_lines[:20]:
            print(f"  {s['file']}:{s['line']}  @ {s['addr_str']}")
            print(f"    {_ascii_safe(s['raw_line'])}")
        if len(skipped_lines) > 20:
            print(f"  ... and {len(skipped_lines) - 20} more "
                  f"(full list in report.json['skipped_markers'])")
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

    # --- PLT-RANGE-UNMAPPED -- FABRICATED address inside .plt. Most serious:
    # there is no target to restamp to, the marker must be re-RE'd.
    pltbad_rows = [m for m in markers if m['verdict'] == 'PLT-RANGE-UNMAPPED']
    if pltbad_rows:
        print(f"--- PLT-RANGE-UNMAPPED ({len(pltbad_rows)}) -- addr is in .plt but is NOT a "
              f"real thunk; the address is FABRICATED ---")
        for m in pltbad_rows:
            print(f"  {m['file']}:{m['line']}  {m['cited_sym'] or '(none)'} "
                  f"@ {m['addr_str']}")
            print(f"    {_ascii_safe(m.get('plt_reason') or 'not a decodable PLT entry')}")
            print(f"    no restamp target -- re-RE the cited symbol from scratch")
        print()

    # --- PLT-THUNK -- cited address is an import stub, not the body.
    plt_rows = [m for m in markers if m['verdict'] == 'PLT-THUNK']
    if plt_rows:
        print(f"--- PLT-THUNK ({len(plt_rows)}) -- cited addr is a .plt import stub, "
              f"not the function body ---")
        for m in plt_rows:
            tgt_d = m.get('plt_target_dem') or m.get('plt_target_sym') or '?'
            if len(tgt_d) > 55:
                tgt_d = tgt_d[:52] + '...'
            hops = m.get('plt_hops', 0)
            extra = '  [2-HOP: veneer behind the thunk]' if hops > 1 else ''
            print(f"  {m['file']}:{m['line']}  {m['cited_sym'] or '(none)'} "
                  f"@ {m['addr_str']} -> real entry "
                  f"0x{m['plt_target_addr']:08x}{extra}")
            mism = ('  [NAME MISMATCH -- verify before restamping]'
                    if m.get('plt_target_matches_cited') is False else '')
            print(f"    target: {_ascii_safe(tgt_d)}{mism}")
            if hops > 1:
                for hop in m.get('plt_chain', []):
                    hop_sym = _ascii_safe(_demangle(hop['sym'])) if hop.get('sym') else ''
                    if len(hop_sym) > 45:
                        hop_sym = hop_sym[:42] + '...'
                    print(f"      hop[{hop['kind']}] {hop['from']} -> {hop['to']}"
                          f"{('  ' + hop_sym) if hop_sym else ''}")
        print()

    # --- NO-SYMBOL (audit class) -- marker names no symbol, so nothing could be
    # cross-checked; show what the address ACTUALLY is. HIGH rows first.
    if symbol_less_markers:
        shown = 30
        print(f"--- NO-SYMBOL ({len(symbol_less_markers)}, {_nosym_high} HIGH) -- marker "
              f"names no symbol; showing what the addr actually is ---")
        for r in symbol_less_markers[:shown]:
            dem = _ascii_safe(r['resolved_dem'] or '(nothing -- addr in no symbol range)')
            if len(dem) > 62:
                dem = dem[:59] + '...'
            tag = 'HIGH' if r['no_symbol_priority'] == 'HIGH' else '    '
            print(f"  [{tag}] {r['file']}:{r['line']}  @ {r['addr_str']} -> "
                  f"{r['resolved_kind']} in {dem}")
        if len(symbol_less_markers) > shown:
            print(f"  ... and {len(symbol_less_markers) - shown} more "
                  f"(full list in report.json['symbol_less_markers'])")
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
        # Check D errors gate --check too: a contradicted or unpairable
        # ASM-verified stamp is an actionable bug, not a note.
        n_sweep_err = 0
        if sweep is not None:
            n_sweep_err = len(sweep['contradicted']) + len(sweep['name_not_in_binary'])
            if n_sweep_err:
                fail_counts['SWEEP-CONTRADICTED'] = len(sweep['contradicted'])
                fail_counts['NAME-NOT-IN-BINARY'] = len(sweep['name_not_in_binary'])
        print("\n" + "=" * 70)
        if failing or n_sweep_err:
            detail = ', '.join(f"{v}={fail_counts[v]}"
                               for v in sorted(fail_counts) if fail_counts[v])
            print(f"CHECK: FAIL -- {len(failing) + n_sweep_err} actionable "
                  f"marker(s): {detail}")
            print("=" * 70)
            return 1
        scope = "actionable + NO-VERSION" if args.strict else "actionable"
        print(f"CHECK: PASS -- no {scope} marker bugs")
        print("=" * 70)
        return 0
    return 0


if __name__ == '__main__':
    sys.exit(main() or 0)
