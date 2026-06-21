#!/usr/bin/env python3
# classify-divergences.py
#
# Raises the precision of the asm-verify "bug shortlist" by auto-classifying
# each escalated divergence row (SUSPICIOUS / DIVERGE / UNPAIRED) by a likely
# CAUSE tag plus a real-bug LIKELIHOOD (HIGH / MED / LOW).
#
# Self-contained: reads an asm-verify report JSON (default tmp/asm-verify/report.json),
# no Docker / Ghidra / network. The report's "symbols" array has, per entry:
#   {mangled, addr, verdict, reason, score, max_score, asm_hash, diff}
# where `diff` is the LCS-aligned renderer output, a list of lines:
#   "  ..."  common to both sides
#   "- ..."  binary-only
#   "+ ..."  port-only
# Registers are normalized to GREG/VREG/DREG; immediates/offsets are KEPT
# (e.g. `ldr GREG, [GREG, #116]`); calls render as `CALL <SYM>` / `bl <SYM>`.
# The LCS% lives in `reason` (e.g. "42.9% LCS (7p vs 7b)"), not as a field.
#
# Outputs (all under the report's directory, never tools/asm-verify/triage.json):
#   shortlist.md            HIGH/MED rows listed; LOW/benign collapsed to a count table
#   suggested-triage.json   proposed ACCEPT entries for the benign rows (review only)
# and prints a summary + a PASS/FAIL validation table against known ground truth.
#
# Detector design notes (why each threshold is what it is) live inline below.

import json
import os
import re
import sys
from collections import Counter, defaultdict

# Verdicts that are "escalated" -- the rows asm-verify is unsure about and that a
# human would otherwise have to read by hand. These are what we classify.
ESCALATED = {"SUSPICIOUS", "DIVERGE", "UNPAIRED"}

# ---------------------------------------------------------------------------
# Small parsing helpers
# ---------------------------------------------------------------------------

LCS_RE = re.compile(r"([0-9]+(?:\.[0-9]+)?)%\s*LCS")


def parse_lcs(reason):
    """Pull the LCS percentage out of a reason string, or None."""
    if not reason:
        return None
    m = LCS_RE.search(reason)
    return float(m.group(1)) if m else None


def split_sides(diff):
    """Return (common, binary_only, port_only) line lists with the 2-char tag
    stripped. Tag convention: '  ' common, '- ' binary-only, '+ ' port-only."""
    common, bin_only, port_only = [], [], []
    for raw in diff or []:
        if raw.startswith("+ "):
            port_only.append(raw[2:])
        elif raw.startswith("- "):
            bin_only.append(raw[2:])
        elif raw.startswith("  "):
            common.append(raw[2:])
        else:
            # Defensive: unknown prefix -> treat as common-ish noise.
            common.append(raw.lstrip())
    return common, bin_only, port_only


# Mnemonic + operand extraction. Lines look like "ldr GREG, [GREG, #116]".
MNEMONIC_RE = re.compile(r"^\s*([a-z][a-z0-9.]*)\b")
OFFSET_RE = re.compile(r"\[GREG,\s*#(-?\d+)\]")           # [GREG, #116]
ANY_IMM_RE = re.compile(r"#(-?\d+(?:\.\d+)?)")             # any #imm (incl float)
CALL_RE = re.compile(r"\b(?:CALL|bl|blx)\b")


def mnemonic(line):
    m = MNEMONIC_RE.match(line)
    return m.group(1) if m else ""


def is_mem_access(mn):
    return mn[:3] in ("ldr", "str", "vld", "vst") or mn in (
        "ldrb", "ldrh", "strb", "strh", "vldr", "vstr",
    )


def strip_offset(line):
    """Canonicalize a memory-access line by zeroing its [GREG,#N] offset, so two
    lines that differ ONLY in the displacement compare equal."""
    return OFFSET_RE.sub("[GREG, #_]", line)


def strip_all_imm(line):
    """Canonicalize by replacing every #imm with a placeholder."""
    return ANY_IMM_RE.sub("#_", line)


# Names of std/library ctors that, when inlined by the port, expand to a run of
# zero-init stores. We can't see the demangled symbol (diff shows `CALL <SYM>`),
# so we rely on the structural shape instead; this list is only used when a
# symbol name happens to be present in `reason`.
STD_CTOR_HINT = re.compile(r"basic_string|_Rb_tree|vector|map|allocator|string")


# ---------------------------------------------------------------------------
# Detectors. Each takes the parsed pieces and returns (cause, likelihood) or None.
# Order matters: benign structural detectors run before the "real bug" ones so a
# clearly-benign std-inline ctor isn't mistaken for a wrong-field structural diff.
# ---------------------------------------------------------------------------


def detect_static_init(mangled, common, bin_only, port_only, lcs):
    """Compiler-emitted static-init thunks are never hand-ported; always benign."""
    if (
        mangled.startswith("_GLOBAL__")
        or "__static_initialization" in mangled
        or "_Z41__static_initialization" in mangled
    ):
        return ("static-init", "LOW")
    return None


def detect_sched(mangled, common, bin_only, port_only, lcs):
    """Pure instruction scheduling: identical multiset of lines on both sides,
    only the order differs. Requires there to actually BE divergent lines."""
    if not bin_only and not port_only:
        return None
    if Counter(bin_only) == Counter(port_only) and bin_only:
        return ("sched", "LOW")
    return None


def detect_std_inline(mangled, common, bin_only, port_only, lcs):
    """Port inlined a std ctor: several `+ str GREG, [GREG,#N]` zero-init stores
    on the port side paired against `- CALL <SYM>` lines on the binary side,
    inside a ctor (mangled contains C1/C2). The binary calls the real member
    ctor; the port flattened it into stores. Byte-different, behaviour-identical."""
    is_ctor = bool(re.search(r"C[12]E", mangled)) or "C1" in mangled or "C2" in mangled
    if not is_ctor:
        return None
    port_stores = sum(
        1
        for l in port_only
        if mnemonic(l) in ("str", "strb", "strh") and OFFSET_RE.search(l)
    )
    bin_calls = sum(1 for l in bin_only if CALL_RE.search(l))
    # Need a real run of stores standing in for at least one call.
    if port_stores >= 3 and bin_calls >= 1:
        return ("std-inline", "LOW")
    return None


def detect_port_guard(mangled, common, bin_only, port_only, lcs):
    """Port added a defensive guard the binary lacks: extra port-only lines that
    form a `cmp GREG, #0` + a conditional escape (`mvn #-1` / `mov #-1` /
    conditional branch / conditional pop). Net: port has a few MORE instrs than
    the binary, and they cluster as a guard. Behaviour-safe (early bail on a
    null/0 the binary assumes non-null). -> ACCEPT-deferred."""
    extra = len(port_only) - len(bin_only)
    if extra <= 0:
        return None
    blob = "\n".join(port_only)
    has_cmp0 = bool(re.search(r"\bcmp GREG, #0\b", blob))
    # Conditional escapes the compiler emits for an early `return -1;` style guard.
    has_escape = bool(
        re.search(r"\bmvn\w* GREG, #0\b", blob)        # mvn/ mvnle GREG,#0  => -1
        or re.search(r"\bmov\w* GREG, #-1\b", blob)
        or re.search(r"\bpop\w+ \{", blob)             # pople {...} conditional return
        or re.search(r"\b(beq|bne|blt|ble|bgt|bge)\w* <SYM>\b", blob)
    )
    # "small guard-sized amount": a guard is a handful of insns, not a rewrite.
    if has_cmp0 and has_escape and 1 <= extra <= 8:
        return ("port-guard", "LOW")
    return None


def detect_got_idiom(mangled, common, bin_only, port_only, lcs):
    """PIC/GOT addressing idiom difference: `add GREG, pc, GREG` or `.word`
    PICOFF lines differing across sides. Position-independent-code relocation
    noise, not a semantic divergence."""
    blob = "\n".join(bin_only + port_only)
    if re.search(r"\badd GREG, pc, GREG\b", blob) or re.search(r"\.word\b", blob):
        # Only call it GOT-idiom if the GOT lines are the bulk of the diff,
        # otherwise it's incidental and we let a real detector win.
        got_lines = sum(
            1
            for l in (bin_only + port_only)
            if "add GREG, pc, GREG" in l or ".word" in l
        )
        if got_lines and got_lines >= max(1, (len(bin_only) + len(port_only)) // 3):
            return ("got-idiom", "LOW")
    return None


def _call_targets(lines):
    """Set of called symbol tokens. Diff normalizes most to `CALL <SYM>`, so this
    mostly counts call PRESENCE rather than identity, which is all we can do."""
    return [l for l in lines if CALL_RE.search(l)]


def detect_wrong_offset(mangled, common, bin_only, port_only, lcs):
    """A single memory-access line whose [GREG,#N] displacement differs between an
    otherwise-identical binary/port pair, with the rest mostly matching. The
    classic struct-field-offset drift (GetCriticalChance #116 vs #112,
    MenuButton vldr #256 vs #252).

    Likelihood MED, deliberately NOT HIGH: this exact shape covers BOTH a real
    layout bug (GetCriticalChance) AND confirmed-benign cases where the port and
    binary structs simply pack a trailing field differently
    (MenuButton::HasNewSymbol/IsLoadingSymbol were byte-exact-benign). The diff
    alone cannot tell a real offset bug from a benign one, so we flag it for human
    RE at MED rather than burying it (LOW) or over-claiming it (HIGH)."""
    # Pair up binary-only vs port-only lines that match after stripping the offset.
    if not bin_only or not port_only:
        return None
    matched = 0
    differing_offsets = 0
    used = [False] * len(port_only)
    for b in bin_only:
        if not is_mem_access(mnemonic(b)):
            continue
        bcanon = strip_offset(b)
        boff = OFFSET_RE.search(b)
        for i, p in enumerate(port_only):
            if used[i]:
                continue
            if strip_offset(p) == bcanon and is_mem_access(mnemonic(p)):
                poff = OFFSET_RE.search(p)
                used[i] = True
                matched += 1
                if boff and poff and boff.group(1) != poff.group(1):
                    differing_offsets += 1
                break
    # Want: at least one offset-only difference, and the divergence is dominated by
    # such pairs (a localized displacement mismatch, not a wholesale rewrite).
    total_div = max(len(bin_only), len(port_only))
    if differing_offsets >= 1 and matched == total_div and matched > 0:
        return ("wrong-offset", "MED")
    return None


def detect_wrong_const(mangled, common, bin_only, port_only, lcs):
    """An otherwise-identical binary/port line pair differing only in a non-address
    #imm (not a [GREG,#off] displacement). A wrong literal constant -> MED."""
    if not bin_only or not port_only:
        return None
    used = [False] * len(port_only)
    differing = 0
    matched = 0
    for b in bin_only:
        # Skip pure memory-offset lines; those are wrong-offset's job.
        if is_mem_access(mnemonic(b)) and OFFSET_RE.search(b):
            continue
        bcanon = strip_all_imm(b)
        bimms = ANY_IMM_RE.findall(b)
        for i, p in enumerate(port_only):
            if used[i]:
                continue
            if strip_all_imm(p) == bcanon:
                used[i] = True
                matched += 1
                if ANY_IMM_RE.findall(p) != bimms:
                    differing += 1
                break
    total_div = max(len(bin_only), len(port_only))
    if differing >= 1 and matched == total_div and matched > 0:
        return ("wrong-const", "MED")
    return None


def _offsets(lines):
    s = set()
    for l in lines:
        if not is_mem_access(mnemonic(l)):
            continue
        for m in OFFSET_RE.finditer(l):
            s.add(int(m.group(1)))
    return s


def detect_addr_form(mangled, common, bin_only, port_only, lcs):
    """Benign instruction-selection / addressing-form difference: both sides
    assemble the SAME bytes into the SAME value via different opcodes -- e.g. a
    3-byte little-endian load built with `add #1` + shifted-orr on one side vs
    explicit `ldrb #3` + `lsl #16` on the other (Math::GetUncompressedSizeLZ8).

    Tell: the set of memory-access offsets touched is identical on both sides,
    the divergent lines are pure byte-assembly ops (ldrb/orr/lsl/lsr/add/mov) with
    NO calls and NO stores (read-only value construction), so behaviour is the
    same despite the byte diff. -> LOW."""
    if not bin_only or not port_only:
        return None
    # No calls / stores involved -- this is value construction, not a side effect.
    if any(CALL_RE.search(l) for l in bin_only + port_only):
        return None
    if any(mnemonic(l) in ("str", "strb", "strh", "vstr") for l in bin_only + port_only):
        return None
    # All divergent lines must be byte-assembly arithmetic / loads.
    allowed = ("ldr", "ldrb", "ldrh", "orr", "lsl", "lsr", "asr", "add",
               "mov", "and", "bic", "ubfx", "uxtb", "uxth")
    if not all(mnemonic(l) in allowed for l in bin_only + port_only):
        return None
    # The union of byte offsets read across BOTH sides must be a small, contiguous
    # window (a multi-byte little-endian load, e.g. bytes 1..3), and there must be
    # at least one common anchor line proving both sides share the same reduction
    # (the shared `orr ... lsl` / `ldrb` skeleton). Pointer-arithmetic addressing
    # (`add #1` then `[#0]`) vs explicit `[#3]` legitimately yields slightly
    # different rendered offsets for the SAME byte window, so we do NOT require the
    # per-side offset sets to be equal -- only that the overall window is tight and
    # the divergence is pure value construction.
    all_off = _offsets(bin_only) | _offsets(port_only) | _offsets(common)
    if not all_off:
        return None
    window = max(all_off) - min(all_off)
    anchored = any(
        mnemonic(l) in ("orr", "ldrb", "ldrh") for l in common
    )
    if window <= 4 and anchored:
        return ("addr-form", "LOW")
    return None


def detect_wrong_field(mangled, common, bin_only, port_only, lcs):
    """Structural: the two bodies touch DIFFERENT fields entirely. Signature is
    very low LCS (<40%) AND each side references a substantive, DISJOINT cluster of
    `[GREG,#off]` accesses -- the function clears or touches the wrong member
    region (FruitSaveData::ClearCombo: binary writes #528/#532 with a loop, port
    loads/writes #32..#44 -- zero offset overlap). This is the highest-value
    real-bug tell, so it is gated TIGHTLY to avoid burying MED rows under false
    HIGHs: both sides must touch >=2 distinct offsets and share none.
    -> HIGH."""
    if lcs is None or lcs >= 40.0:
        return None
    if not bin_only or not port_only:
        return None

    bin_off = _offsets(bin_only)
    port_off = _offsets(port_only)

    # Both sides must touch a real cluster of distinct fields...
    if len(bin_off) < 2 or len(port_off) < 2:
        return None
    # ...and those clusters must be fully disjoint (different region of the struct).
    if bin_off & port_off:
        return None
    return ("wrong-field", "HIGH")


def detect_call_graph(mangled, common, bin_only, port_only, lcs):
    """A named call present on one side and absent on the other (missing/extra
    call), or an inverted condition code on a branch. Real-ish but often a
    refactor artifact -> MED."""
    bin_calls = len(_call_targets(bin_only))
    port_calls = len(_call_targets(port_only))
    if (bin_calls == 0) != (port_calls == 0):
        return ("call-graph", "MED")
    if abs(bin_calls - port_calls) >= 1 and (bin_calls + port_calls) >= 1:
        return ("call-graph", "MED")
    # Inverted branch condition: same branch mnemonic family with opposite cc.
    cc_pairs = [("beq", "bne"), ("blt", "bge"), ("ble", "bgt"), ("bgt", "ble"),
                ("bge", "blt"), ("bne", "beq")]
    bin_br = {mnemonic(l) for l in bin_only if mnemonic(l).startswith("b") and "<SYM>" in l}
    port_br = {mnemonic(l) for l in port_only if mnemonic(l).startswith("b") and "<SYM>" in l}
    for a, b in cc_pairs:
        if a in bin_br and b in port_br:
            return ("call-graph", "MED")
    return None


# Detector pipeline, in priority order. First non-None wins.
DETECTORS = [
    detect_static_init,   # benign, structural identity
    detect_sched,         # benign, identical multiset reordered
    detect_std_inline,    # benign, ctor store-run vs CALL
    detect_port_guard,    # benign(deferred), extra defensive guard
    detect_got_idiom,     # benign, PIC relocation noise
    detect_addr_form,     # benign, same-value different instruction selection
    detect_wrong_field,   # HIGH real bug -- run before offset/const so a wholesale
                          # structural mismatch isn't downgraded to a 1-line offset
    detect_wrong_offset,  # MED real-ish, localized displacement
    detect_wrong_const,   # MED real-ish, wrong literal
    detect_call_graph,    # MED, missing/extra call or inverted branch
]


def classify(sym):
    """Return (cause, likelihood) for one symbol entry's diff."""
    mangled = sym.get("mangled", "")
    diff = sym.get("diff", [])
    lcs = parse_lcs(sym.get("reason", ""))
    common, bin_only, port_only = split_sides(diff)

    # No divergent lines at all -> nothing to classify (shouldn't be escalated).
    if not bin_only and not port_only:
        return ("unknown", "MED")

    for det in DETECTORS:
        res = det(mangled, common, bin_only, port_only, lcs)
        if res:
            return res
    # Nothing matched: leave for a human, never a confident benign tag.
    return ("unknown", "MED")


def one_line_summary(sym):
    """A compact human-readable description of what diverged."""
    common, bin_only, port_only = split_sides(sym.get("diff", []))
    bits = []
    if bin_only:
        bits.append("bin:" + "; ".join(bin_only[:2]) + ("..." if len(bin_only) > 2 else ""))
    if port_only:
        bits.append("port:" + "; ".join(port_only[:2]) + ("..." if len(port_only) > 2 else ""))
    return " | ".join(bits) if bits else "(no divergent lines)"


# Map a benign cause to a proposed triage verdict.
BENIGN_VERDICT = {
    "std-inline": "ACCEPT-cosmetic",
    "got-idiom": "ACCEPT-cosmetic",
    "addr-form": "ACCEPT-cosmetic",
    "static-init": "ACCEPT-cosmetic",
    "sched": "ACCEPT-cosmetic",
    "port-guard": "ACCEPT-deferred",
}

LIKELIHOOD_RANK = {"HIGH": 0, "MED": 1, "LOW": 2}


def run(report_path):
    with open(report_path, "r", encoding="utf-8") as fh:
        report = json.load(fh)
    symbols = report["symbols"]

    out_dir = os.path.dirname(os.path.abspath(report_path))
    rows = []  # (sym, cause, likelihood, lcs)
    for sym in symbols:
        if sym.get("verdict") not in ESCALATED:
            continue
        cause, likelihood = classify(sym)
        rows.append((sym, cause, likelihood, parse_lcs(sym.get("reason", ""))))

    high = [r for r in rows if r[2] == "HIGH"]
    med = [r for r in rows if r[2] == "MED"]
    low = [r for r in rows if r[2] == "LOW"]

    # ---- shortlist.md ----
    shortlist_path = os.path.join(out_dir, "shortlist.md")
    listed = sorted(
        high + med,
        key=lambda r: (LIKELIHOOD_RANK[r[2]], r[3] if r[3] is not None else 999.0),
    )
    with open(shortlist_path, "w", encoding="utf-8") as fh:
        fh.write("# asm-verify divergence shortlist (auto-classified)\n\n")
        fh.write(
            "Source: `{}`  --  {} escalated rows "
            "({} HIGH, {} MED, {} LOW/benign).\n\n".format(
                os.path.basename(report_path), len(rows), len(high), len(med), len(low)
            )
        )
        fh.write("## Real-bug shortlist (HIGH then MED)\n\n")
        fh.write("| mangled | cause | likelihood | LCS% | diff summary |\n")
        fh.write("|---|---|---|---|---|\n")
        for sym, cause, lk, lcs in listed:
            fh.write(
                "| `{}` | {} | {} | {} | {} |\n".format(
                    sym["mangled"],
                    cause,
                    lk,
                    "{:.1f}".format(lcs) if lcs is not None else "?",
                    one_line_summary(sym).replace("|", "\\|"),
                )
            )
        fh.write("\n## Benign / LOW rows (collapsed)\n\n")
        fh.write("Auto-demoted; see `suggested-triage.json` for proposed entries.\n\n")
        cause_counts = Counter(r[1] for r in low)
        fh.write("| cause | count | proposed verdict |\n")
        fh.write("|---|---|---|\n")
        for cause, n in cause_counts.most_common():
            fh.write(
                "| {} | {} | {} |\n".format(
                    cause, n, BENIGN_VERDICT.get(cause, "ACCEPT-cosmetic")
                )
            )
        fh.write("| **total benign** | **{}** | |\n".format(len(low)))

    # ---- suggested-triage.json ----
    triage_path = os.path.join(out_dir, "suggested-triage.json")
    suggested = {}
    for sym, cause, lk, lcs in low:
        suggested[sym["mangled"]] = {
            "verdict": BENIGN_VERDICT.get(cause, "ACCEPT-cosmetic"),
            "reason": "auto: {}".format(cause),
            "asm_hash": sym.get("asm_hash"),
        }
    with open(triage_path, "w", encoding="utf-8") as fh:
        json.dump(suggested, fh, indent=2, sort_keys=True)
        fh.write("\n")

    # ---- summary ----
    cause_counts = Counter(r[1] for r in rows)
    print("=" * 64)
    print("asm-verify divergence classification  --  {}".format(os.path.basename(report_path)))
    print("=" * 64)
    print("escalated rows : {}".format(len(rows)))
    print("  HIGH (real)  : {}".format(len(high)))
    print("  MED  (review): {}".format(len(med)))
    print("  LOW  (benign): {}".format(len(low)))
    print()
    print("per-cause breakdown:")
    for cause, n in cause_counts.most_common():
        lk = {c: l for (_s, c, l, _x) in rows}.get(cause, "?")
        print("  {:<14} {:>5}".format(cause, n))
    print()
    print("wrote: {}".format(shortlist_path))
    print("wrote: {}".format(triage_path))
    return rows


# ---------------------------------------------------------------------------
# Validation gate -- proves detectors match known ground truth from RE session.
# Each entry: mangled -> a predicate over (cause, likelihood).
# ---------------------------------------------------------------------------

def _gate_clearcombo(cause, lk):
    return lk == "HIGH" and cause == "wrong-field"


def _gate_critchance(cause, lk):
    return cause == "wrong-offset" and lk in ("HIGH", "MED")


def _gate_low_cause(want_cause):
    return lambda cause, lk: lk == "LOW" and cause == want_cause


def _gate_low_any(cause, lk):
    return lk == "LOW"


def _gate_not_high(cause, lk):
    return lk != "HIGH"


GATE = [
    ("_ZN13FruitSaveData10ClearComboEv", "HIGH wrong-field", _gate_clearcombo),
    ("_ZN11WaveManager17GetCriticalChanceEi", "wrong-offset HIGH/MED", _gate_critchance),
    # BonusType ctor: mangled is _ZN9BonusTypeC1Ev / C2Ev.
    ("_ZN9BonusTypeC1Ev", "LOW std-inline", _gate_low_cause("std-inline")),
    ("_ZN19PROBABILITY_OVERIDE7GetTypeEv", "LOW port-guard", _gate_low_cause("port-guard")),
    ("_ZN4Math22GetUncompressedSizeLZ8EPKv", "LOW (benign)", _gate_low_any),
    ("_ZN10MenuButton12HasNewSymbolEv", "NOT HIGH", _gate_not_high),
    ("_ZN10MenuButton15IsLoadingSymbolEv", "NOT HIGH", _gate_not_high),
]


def validate(report_path):
    with open(report_path, "r", encoding="utf-8") as fh:
        report = json.load(fh)
    syms = {s["mangled"]: s for s in report["symbols"]}
    print()
    print("=" * 72)
    print("VALIDATION GATE  --  {}".format(os.path.basename(report_path)))
    print("=" * 72)
    print("{:<44} {:<22} {:<18} {}".format("symbol", "expected", "got", "result"))
    print("-" * 72)
    all_pass = True
    for mangled, expect_desc, pred in GATE:
        sym = syms.get(mangled)
        if sym is None:
            print("{:<44} {:<22} {:<18} {}".format(
                mangled[:42], expect_desc, "MISSING", "FAIL"))
            all_pass = False
            continue
        cause, lk = classify(sym)
        got = "{} {}".format(lk, cause)
        ok = pred(cause, lk)
        all_pass = all_pass and ok
        print("{:<44} {:<22} {:<18} {}".format(
            mangled[:42], expect_desc, got, "PASS" if ok else "FAIL"))
    print("-" * 72)
    print("GATE: {}".format("ALL PASS" if all_pass else "FAILURES PRESENT"))
    return all_pass


def main(argv):
    # Default report; allow override. Validation runs against whichever report
    # contains the ground-truth symbols (defaults to report.after.json if the
    # primary report lacks them).
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.abspath(os.path.join(here, "..", ".."))
    report_path = argv[1] if len(argv) > 1 else os.path.join(
        root, "tmp", "asm-verify", "report.json")

    if not os.path.exists(report_path):
        print("report not found: {}".format(report_path), file=sys.stderr)
        return 2

    run(report_path)

    # Pick a report for the gate: prefer the run one if it has the targets,
    # else fall back to report.after.json in the same dir.
    gate_path = report_path
    with open(report_path, "r", encoding="utf-8") as fh:
        names = {s["mangled"] for s in json.load(fh)["symbols"]}
    needed = {m for (m, _d, _p) in GATE}
    if not needed.issubset(names):
        alt = os.path.join(os.path.dirname(report_path), "report.after.json")
        if os.path.exists(alt):
            print("\n[gate] primary report lacks ground-truth symbols; "
                  "validating against report.after.json")
            gate_path = alt
    ok = validate(gate_path)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
