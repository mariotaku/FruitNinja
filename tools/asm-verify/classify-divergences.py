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
# JSON-first: this script ENRICHES report.json in place rather than emitting a
# markdown product. report.json stays the single structured source of truth.
#
# Outputs:
#   report.json  (re-saved)  each symbol gains two fields:
#                              `cause`      -- the classifier tag (e.g. wrong-field)
#                              `likelihood` -- HIGH / MED / LOW, or null when not
#                                              classified (non-escalated rows).
#                            Every pre-existing field is preserved unchanged.
#   suggested-triage.json    proposed ACCEPT entries for the benign rows (review only).
#                            Only causes in BENIGN_VERDICT are eligible; LOW-but-
#                            not-benign causes (port-stub / port-stub-defunct) are
#                            excluded -- LOW ranking never implies auto-accept.
#
# No markdown is written. The ranked shortlist (HIGH rows + top MED) is printed
# to STDOUT instead, alongside a per-cause count summary and a PASS/FAIL
# validation table against known ground truth. Consumers (asm-fix-loop,
# asm-triager) read the `cause`/`likelihood` fields from report.json directly.
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
# ISA mismatch (#111) -- Thumb-2 binary body vs ARM cross-build body.
#
# The two instruction streams are incomparable BY CONSTRUCTION (different
# encodings, IT blocks vs ARM predication, movs/adds vs mov/add), so the LCS
# score is neither a pass nor a fail -- it is noise. Rows in this set are tagged
# `isa-mismatch` / LOW so they rank as noise instead of as high-ratio
# divergences, and carry `isa_mismatch: true` in report.json regardless of
# verdict (an already-triaged row must still be findable for re-triage).
#
# The set comes from tmp/asm-verify/isa-modes.json, written by
# detect-isa-mismatch.py. NO FILE => FEATURE OFF => the report is bit-identical
# to the pre-#111 output. Same for --no-isa.
#
# `isa-mismatch` is deliberately NOT in BENIGN_VERDICT: "the score cannot be
# computed" is not "the code is correct". These rows are excluded, not accepted.
ISA_MISMATCH = set()


def load_isa_modes(report_path):
    """Populate ISA_MISMATCH from isa-modes.json next to the report. Best effort."""
    path = os.path.join(os.path.dirname(os.path.abspath(report_path)),
                        "isa-modes.json")
    if not os.path.exists(path):
        return 0
    try:
        with open(path, "r", encoding="utf-8") as fh:
            data = json.load(fh)
    except Exception:                                            # noqa: BLE001
        return 0
    ISA_MISMATCH.update(data.get("mismatched", []))
    return len(ISA_MISMATCH)

# ---------------------------------------------------------------------------
# Small parsing helpers
# ---------------------------------------------------------------------------

LCS_RE = re.compile(r"([0-9]+(?:\.[0-9]+)?)%\s*LCS")
NPB_RE = re.compile(r"\((\d+)p vs (\d+)b\)")


def parse_lcs(reason):
    """Pull the LCS percentage out of a reason string, or None."""
    if not reason:
        return None
    m = LCS_RE.search(reason)
    return float(m.group(1)) if m else None


def parse_counts(reason):
    """Pull the (port_instr, bin_instr) counts out of a reason string like
    '13.3% LCS (9p vs 9b)', or (None, None)."""
    if not reason:
        return (None, None)
    m = NPB_RE.search(reason)
    if not m:
        return (None, None)
    return (int(m.group(1)), int(m.group(2)))


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


COND_BRANCH_RE = re.compile(
    r"^\s*(beq|bne|blt|ble|bgt|bge|bcs|bcc|bhi|bls|bmi|bpl|bvs|bvc)\w*\s+<SYM>")


def _n_cond_branch(lines):
    """Count conditional branches to a label (skip/continue-shaped control flow),
    excluding calls (bl/blx render as CALL) and unconditional b."""
    return sum(1 for l in lines if COND_BRANCH_RE.match(l))


def detect_guard_skips_work(mangled, common, bin_only, port_only, lcs):
    """Port added a conditional-branch GUARD the binary LACKS that SKIPS work with
    side effects -- an invented early-`continue` / branch-over-a-loop-body (the
    ScreenEffect::Activate `if (img.m_bAddedToHUD) continue;` that skipped every
    AddControl, silently disabling all screen effects).

    This is the DANGEROUS twin of detect_port_guard: a benign guard bails early by
    RETURNING a safe default (materializes -1/0 and pops/returns -- the binary just
    assumed non-null); a dangerous one branches PAST side-effecting code
    (calls/stores) without returning, so it drops behaviour that never shows up as
    a wrong value -- only as "the thing didn't happen". Auto-accepting these (as the
    old port-guard=LOW did) hides real regressions, so this is HIGH (human review,
    never auto-accept).

    Tell: the divergence is LOCALIZED to a small net-added guard (1-2 compare-fed
    conditional branches to a label that the binary lacks), NOT a wholesale rewrite
    -- a handful of port-only lines, same size bound as detect_port_guard. In a
    heavily-divergent function (low LCS, dozens of port-only lines) the guard can't
    be isolated from codegen noise by a per-line diff, so we do NOT fire there (that
    is what runtime/HLE verification is for) -- firing would flood HIGH with every
    function whose compiler emitted one extra branch. There must be NO return-default
    materialization (that is port-guard's LOW case) and the body must have side
    effects (calls/stores) the branch can skip."""
    # Localized only: a guard is a few added lines, not a near-rewrite. This is the
    # key precision knob -- without it the detector fires on ~530 codegen-noisy rows.
    extra = len(port_only) - len(bin_only)
    if not (1 <= extra <= 6) or len(port_only) > 10:
        return None
    net_added_br = _n_cond_branch(port_only) - _n_cond_branch(bin_only)
    if not (1 <= net_added_br <= 2):
        return None
    blob = "\n".join(port_only)
    if not re.search(r"\b(cmp|tst|cbz|cbnz)\b", blob):
        return None
    # A benign early-return guard materializes a default (-1/0) into the result
    # register; that is detect_port_guard's (LOW) job, not this one.
    returns_default = bool(
        re.search(r"\bmvn\w*\s+GREG,\s*#0\b", blob)
        or re.search(r"\bmov\w*\s+GREG,\s*#-1\b", blob)
    )
    if returns_default:
        return None
    # The skipped region must have OBSERVABLE side effects, else dropping it is a
    # no-op (and likely just a benign short-circuit).
    body = common + bin_only + port_only
    has_side_effect = any(
        CALL_RE.search(l) or mnemonic(l) in ("str", "strb", "strh", "vstr")
        for l in body
    )
    if not has_side_effect:
        return None
    return ("guard-skips-work", "HIGH")


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


# Name keywords that mark a symbol as belonging to a KNOWN defunct/platform-bound
# subsystem (online services, P2P MP, news, Bada/Osp platform glue, splash). A
# trivial port body under one of these names is almost certainly an intentional
# policy stub, not an unported gap. Everything else stays in plain `port-stub`
# so a human can enumerate candidate unported gaps from report.json.
DEFUNCT_NAME_HINT = re.compile(
    r"News|Facebook|OpenFeint|Feint|P2P|Network|Leaderboard|Bada|Osp|Splash"
    r"|GameCenter|Multiplayer|Online")


def detect_port_stub(mangled, common, bin_only, port_only, lcs):
    """Port side compiled to a TRIVIAL body (<= 3 instructions, e.g. `bx lr` or
    `mov GREG, #0; bx lr`) against a SUBSTANTIAL binary body (>= 4 instructions
    AND at least 2x the port body -- a flat >=10 floor left the b=4..8 defunct
    one-liners, IsP2POnline / OpenFeintOnline / ConnectGameCenter and friends,
    pinning the 0.0%-LCS top of the MED shortlist).
    Two sub-cases the instruction counts alone cannot distinguish:
      - a correctly-stubbed defunct/platform feature (project stub-don't-skip
        policy) -- not a bug; or
      - a genuinely UNPORTED function someone should still port.
    So: likelihood LOW (must not pin the HIGH/MED shortlist -- ~40 honest stubs
    otherwise sit at max score forever), but deliberately NOT auto-accepted --
    neither cause appears in BENIGN_VERDICT, and the suggested-triage writer only
    emits causes listed there, so these rows stay visible for human enumeration.
    `port-stub-defunct` = name matches a known defunct/platform keyword;
    `port-stub` = everything else (the candidate unported gaps).
    Runs AFTER wrong-field (a real disjoint-cluster bug keeps HIGH) and BEFORE
    call-graph (which used to grab these rows as MED and pin the ranking)."""
    port_n = len(common) + len(port_only)
    bin_n = len(common) + len(bin_only)
    if port_n <= 3 and bin_n >= 4 and bin_n >= 2 * port_n:
        if DEFUNCT_NAME_HINT.search(mangled):
            return ("port-stub-defunct", "LOW")
        return ("port-stub", "LOW")
    return None


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
    detect_guard_skips_work,  # HIGH real bug -- invented guard that SKIPS side
                              # effects; must run BEFORE port_guard so a dangerous
                              # loop-continue guard isn't auto-accepted as benign
    detect_port_guard,    # benign(deferred), extra defensive early-RETURN guard
    detect_got_idiom,     # benign, PIC relocation noise
    detect_addr_form,     # benign, same-value different instruction selection
    detect_wrong_field,   # HIGH real bug -- run before offset/const so a wholesale
                          # structural mismatch isn't downgraded to a 1-line offset
    detect_port_stub,     # LOW but NOT auto-accepted -- trivial port body vs
                          # substantial binary body (honest stub OR unported gap);
                          # must run before call-graph, which otherwise grabs
                          # these as MED and pins the shortlist
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

    # ISA mismatch outranks every detector: when the binary body is Thumb-2 and
    # the cross-build body is ARM, EVERY line of the diff is encoding noise, so
    # any other cause read off that diff would be a fiction.
    if mangled in ISA_MISMATCH:
        return ("isa-mismatch", "LOW")

    # Compiler-generated TU static-init is identifiable by NAME alone -- classify
    # it even when the diff is empty/missing (e.g. UNPAIRED rows) so a
    # _GLOBAL__I_* row can never fall through to the MED shortlist as `unknown`.
    res = detect_static_init(mangled, common, bin_only, port_only, lcs)
    if res:
        return res

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


# Map a benign cause to a proposed triage verdict. ONLY causes listed here are
# eligible for suggested-triage.json auto-accept. `port-stub` / `port-stub-defunct`
# are deliberately ABSENT: a trivial port body is either a correct policy stub or
# a genuinely unported gap, and the classifier cannot tell which -- so those rows
# rank LOW but must stay out of the auto-accept path (human enumerates them from
# report.json `cause` fields).
BENIGN_VERDICT = {
    "std-inline": "ACCEPT-cosmetic",
    "got-idiom": "ACCEPT-cosmetic",
    "addr-form": "ACCEPT-cosmetic",
    "static-init": "ACCEPT-cosmetic",
    "sched": "ACCEPT-cosmetic",
    "port-guard": "ACCEPT-deferred",
}

LIKELIHOOD_RANK = {"HIGH": 0, "MED": 1, "LOW": 2}


def _truncate(text, width):
    """Trim a string to `width` chars with an ASCII '...' tail if it overflows."""
    if len(text) <= width:
        return text
    if width <= 3:
        return text[:width]
    return text[: width - 3] + "..."


def run(report_path):
    with open(report_path, "r", encoding="utf-8") as fh:
        report = json.load(fh)
    symbols = report["symbols"]

    # Populate ISA_MISMATCH before classifying. Absent isa-modes.json => empty
    # set => feature off => output byte-identical to the pre-#111 report.
    load_isa_modes(report_path)

    out_dir = os.path.dirname(os.path.abspath(report_path))
    rows = []  # (sym, cause, likelihood, lcs)
    for sym in symbols:
        # Factual row metadata, independent of verdict: an ALREADY-TRIAGED row
        # whose score is ISA noise must stay findable so it can be re-triaged.
        # Only written when true, so with the feature off report.json is
        # byte-identical to the pre-#111 output.
        if sym.get("mangled") in ISA_MISMATCH:
            sym["isa_mismatch"] = True
        if sym.get("verdict") not in ESCALATED:
            # Non-escalated rows (MATCH / COSMETIC / ...) aren't classified.
            # Set the two fields to null consistently so every symbol carries
            # them and downstream `.get('likelihood')` filters are well-defined.
            sym["cause"] = None
            sym["likelihood"] = None
            continue
        cause, likelihood = classify(sym)
        # ---- enrich report.json in place: add cause + likelihood, preserve all
        #      existing fields (mangled/addr/verdict/reason/score/max_score/
        #      asm_hash/diff). report.json stays the structured source of truth.
        sym["cause"] = cause
        sym["likelihood"] = likelihood
        rows.append((sym, cause, likelihood, parse_lcs(sym.get("reason", ""))))

    high = [r for r in rows if r[2] == "HIGH"]
    med = [r for r in rows if r[2] == "MED"]
    low = [r for r in rows if r[2] == "LOW"]

    # ---- re-save the enriched report.json ----
    with open(report_path, "w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=2)
        fh.write("\n")

    # ---- suggested-triage.json (already structured -- kept) ----
    triage_path = os.path.join(out_dir, "suggested-triage.json")
    suggested = {}
    for sym, cause, lk, lcs in low:
        # Only causes with an explicit BENIGN_VERDICT entry get an auto-accept
        # proposal. port-stub / port-stub-defunct (and any future LOW-but-not-
        # benign cause) are skipped: LOW ranking must not imply acceptance.
        if cause not in BENIGN_VERDICT:
            continue
        suggested[sym["mangled"]] = {
            "verdict": BENIGN_VERDICT[cause],
            "reason": "auto: {}".format(cause),
            "asm_hash": sym.get("asm_hash"),
        }
    with open(triage_path, "w", encoding="utf-8") as fh:
        json.dump(suggested, fh, indent=2, sort_keys=True)
        fh.write("\n")

    # ---- ranked shortlist -> STDOUT (replaces the old shortlist.md) ----
    # HIGH rows first, then the top ~15 MED rows, as a compact aligned table.
    MAX_MED = 15
    listed = sorted(
        high,
        key=lambda r: (r[3] if r[3] is not None else 999.0),
    )
    med_sorted = sorted(
        med,
        key=lambda r: (r[3] if r[3] is not None else 999.0),
    )
    listed = listed + med_sorted[:MAX_MED]

    print("=" * 100)
    print("asm-verify divergence shortlist (auto-classified)  --  {}".format(
        os.path.basename(report_path)))
    print("source: {} escalated rows ({} HIGH, {} MED, {} LOW/benign)".format(
        len(rows), len(high), len(med), len(low)))
    print("=" * 100)
    if listed:
        hdr = "{:<46} {:<12} {:<5} {:>6}  {}".format(
            "mangled", "cause", "lk", "LCS%", "diff summary")
        print(hdr)
        print("-" * 100)
        for sym, cause, lk, lcs in listed:
            print("{:<46} {:<12} {:<5} {:>6}  {}".format(
                _truncate(sym["mangled"], 46),
                cause,
                lk,
                "{:.1f}".format(lcs) if lcs is not None else "?",
                _truncate(one_line_summary(sym), 60),
            ))
        if len(med) > MAX_MED:
            print("... (+{} more MED rows -- see report.json likelihood/cause "
                  "fields)".format(len(med) - MAX_MED))
    else:
        print("(no HIGH/MED rows)")

    # ---- port-stub section: LOW-ranked but NOT auto-accepted ----
    # Trivial port body vs substantial binary body. The non-defunct rows are the
    # candidate UNPORTED GAPS -- listed in full so a human can enumerate them.
    # Defunct-keyword rows are policy stubs; count + a few examples only.
    stub_gap = [r for r in rows if r[1] == "port-stub"]
    stub_def = [r for r in rows if r[1] == "port-stub-defunct"]
    if stub_gap or stub_def:
        def _bin_n(r):
            c, b, p = split_sides(r[0].get("diff", []))
            return len(c) + len(b)
        print()
        print("port-stub rows (trivial port body vs substantial binary; LOW but "
              "NOT auto-accepted):")
        print("  {} defunct/platform-keyword stubs (policy stubs, e.g.: {})".format(
            len(stub_def),
            ", ".join(_truncate(r[0]["mangled"], 40)
                      for r in sorted(stub_def, key=_bin_n, reverse=True)[:3])))
        print("  {} candidate unported gaps (no defunct keyword) -- review these:".format(
            len(stub_gap)))
        for r in sorted(stub_gap, key=_bin_n, reverse=True):
            print("    {:<70} bin {:>4} instrs".format(
                _truncate(r[0]["mangled"], 70), _bin_n(r)))

    # ---- per-cause count summary ----
    cause_counts = Counter(r[1] for r in rows)
    print()
    print("per-cause breakdown:")
    for cause, n in cause_counts.most_common():
        print("  {:<14} {:>5}".format(cause, n))
    print()
    print("escalated rows : {}  (HIGH {} / MED {} / LOW {})".format(
        len(rows), len(high), len(med), len(low)))
    print("enriched: {}  (added cause + likelihood per symbol)".format(report_path))
    print("wrote:    {}".format(triage_path))
    return rows


# ---------------------------------------------------------------------------
# Validation gate -- proves detectors match known ground truth from RE session.
# Each entry: mangled -> a predicate over (cause, likelihood).
#
# The gate validates the DETECTOR LOGIC against fixed, canonical diff fixtures
# (GATE_FIXTURES below), NOT against whatever the live report.json currently
# holds. This is deliberate: report.json is regenerated every run and a symbol's
# diff legitimately CHANGES as the port is fixed (e.g. once FruitSaveData::
# ClearCombo's #528/#532 loop is ported, its diff stops being a disjoint
# wrong-field cluster and the row drops off the shortlist -- correct behaviour,
# but it would spuriously "fail" a gate that read the live diff). Pinning the
# canonical ground-truth diffs keeps the gate a stable regression test on the
# classifier itself. A fixture is preferred when present; otherwise the gate
# falls back to the live symbol so newly-added ground truth still gets checked.
# ---------------------------------------------------------------------------

# Canonical ground-truth diffs (mangled -> {reason, diff}) captured from the RE
# session that established each detector. Tag convention matches report.json:
# "  " common, "- " binary-only, "+ " port-only.
GATE_FIXTURES = {
    # ClearCombo (the wrong-field HIGH case): binary clears the combo array with
    # a counted loop over #528/#532; the (pre-fix) port wrote a DISJOINT trailing
    # field cluster #32..#44 -- zero offset overlap, <40% LCS -> HIGH wrong-field.
    "_ZN13FruitSaveData10ClearComboEv": {
        "reason": "13.3% LCS (9p vs 9b)",
        "diff": [
            "- mov GREG, #0",
            "- str GREG, [GREG, #528]",
            "- add GREG, GREG, #1",
            "- str GREG, [GREG, #532]",
            "- cmp GREG, #11",
            "- add GREG, GREG, #4",
            "- bne <SYM>",
            "+ str GREG, [GREG, #32]",
            "+ str GREG, [GREG, #36]",
            "+ str GREG, [GREG, #40]",
            "+ str GREG, [GREG, #44]",
            "  bx lr",
        ],
    },
    # GetCriticalChance: single vldr displacement drift #116 vs #112 -> wrong-offset.
    "_ZN11WaveManager17GetCriticalChanceEi": {
        "reason": "87.5% LCS (8p vs 8b)",
        "diff": [
            "  add GREG, GREG, GREG, lsl #2",
            "- vldr VREG, [GREG, #116]",
            "+ vldr VREG, [GREG, #112]",
            "  ldr GREG, [GREG, #564]",
            "  cmp GREG, #0",
            "  vldrne VREG, [GREG, #100]",
            "  vmoveq VREG, #112",
            "  vmul VREG, VREG, VREG",
            "  bx lr",
        ],
    },
    # BonusType ctor: binary CALLs the member ctor; port flattened it into a run
    # of zero-init stores -> std-inline (benign).
    "_ZN9BonusTypeC1Ev": {
        "reason": "16.7% LCS (12p vs 9b)",
        "diff": [
            "- push {GREG, lr}",
            "- mov GREG, GREG",
            "- CALL <SYM>",
            "- add GREG, GREG, #24",
            "- CALL <SYM>",
            "  mov GREG, #0",
            "- mov GREG, GREG",
            "+ add GREG, GREG, #4",
            "  strb GREG, [GREG, #36]",
            "- pop {GREG, pc}",
            "+ str GREG, [GREG, #16]",
            "+ str GREG, [GREG, #20]",
            "+ strb GREG, [GREG, #4]",
            "+ str GREG, [GREG, #8]",
            "+ str GREG, [GREG, #12]",
            "+ str GREG, [GREG, #24]",
            "+ str GREG, [GREG, #28]",
            "+ str GREG, [GREG, #32]",
            "+ bx lr",
        ],
    },
    # PROBABILITY_OVERIDE::GetType: port added a cmp #0 + conditional escape
    # (mvnle #0 / pople) guard the binary lacks -> port-guard (benign-deferred).
    "_ZN19PROBABILITY_OVERIDE7GetTypeEv": {
        "reason": "61.5% LCS (13p vs 9b)",
        "diff": [
            "+ ldr GREG, [GREG, #104]",
            "  push {GREG, lr}",
            "+ cmp GREG, #0",
            "  mov GREG, GREG",
            "+ mvnle GREG, #0",
            "+ pople {GREG, pc}",
            "  CALL <SYM>",
            "  ldr GREG, [GREG, #104]",
            "- ldr GREG, [GREG, #32]",
            "+ add GREG, GREG, #8",
            "  CALL <SYM>",
            "  add GREG, GREG, #6",
            "  ldr GREG, [GREG, GREG, lsl #2]",
            "  pop {GREG, pc}",
        ],
    },
    # GetUncompressedSizeLZ8: same multi-byte little-endian value built via a
    # different instruction selection -> addr-form (benign).
    "_ZN4Math22GetUncompressedSizeLZ8EPKv": {
        "reason": "42.9% LCS (7p vs 7b)",
        "diff": [
            "- add GREG, GREG, #1",
            "+ ldrb GREG, [GREG, #3]",
            "+ ldrb GREG, [GREG, #2]",
            "  ldrb GREG, [GREG, #1]",
            "- ldrb GREG, [GREG, #1]",
            "- ldrb GREG, [GREG, #2]",
            "+ lsl GREG, GREG, #16",
            "  orr GREG, GREG, GREG, lsl #8",
            "- orr GREG, GREG, GREG, lsl #16",
            "+ orr GREG, GREG, GREG",
            "  bx lr",
        ],
    },
    # MenuButton::HasNewSymbol / IsLoadingSymbol: single vldr displacement drift,
    # confirmed BENIGN (trailing-field pack difference) -> must NOT be HIGH.
    "_ZN10MenuButton12HasNewSymbolEv": {
        "reason": "83.3% LCS (6p vs 6b)",
        "diff": [
            "- vldr VREG, [GREG, #252]",
            "+ vldr VREG, [GREG, #256]",
            "  vcmpe VREG, #0.0",
            "  vmrs APSR_nzcv, fpscr",
            "  movlt GREG, #0",
            "  movge GREG, #1",
            "  bx lr",
        ],
    },
    "_ZN10MenuButton15IsLoadingSymbolEv": {
        "reason": "83.3% LCS (6p vs 6b)",
        "diff": [
            "- vldr VREG, [GREG, #248]",
            "+ vldr VREG, [GREG, #252]",
            "  vcmpe VREG, #0.0",
            "  vmrs APSR_nzcv, fpscr",
            "  movlt GREG, #0",
            "  movge GREG, #1",
            "  bx lr",
        ],
    },
    # FruitNinjaNewsControl::Update (port-stub-defunct): defunct online-news
    # subsystem, port body is a bare `bx lr` policy stub vs a 200+ instruction
    # binary body. Must classify LOW (not pin the shortlist as call-graph MED)
    # but must NOT enter suggested-triage auto-accept.
    "_ZN21FruitNinjaNewsControl6UpdateEf": {
        "reason": "0.4% LCS (1p vs 242b)",
        "diff": [
            "- push {GREG, GREG, GREG, lr}",
            "- ldr GREG, [GREG, #12]",
            "- cmp GREG, #0",
            "- beq <SYM>",
            "- CALL <SYM>",
            "- ldr GREG, [GREG, #48]",
            "- vldr VREG, [GREG, #56]",
            "- CALL <SYM>",
            "- str GREG, [GREG, #16]",
            "- CALL <SYM>",
            "- mov GREG, #1",
            "- strb GREG, [GREG, #20]",
            "- pop {GREG, GREG, GREG, pc}",
            "+ bx lr",
        ],
    },
    # ActorManager::Update (plain port-stub): NO defunct keyword in the name --
    # a trivial port body here is a candidate UNPORTED GAP. Same LOW ranking,
    # same not-auto-accepted handling, but distinguishable cause.
    "_ZN6Mortar12ActorManager6UpdateEfP7ColAABBS2_": {
        "reason": "1.2% LCS (2p vs 85b)",
        "diff": [
            "- push {GREG, GREG, GREG, lr}",
            "- ldr GREG, [GREG, #8]",
            "- cmp GREG, GREG",
            "- beq <SYM>",
            "- CALL <SYM>",
            "- ldr GREG, [GREG, #4]",
            "- str GREG, [GREG, #12]",
            "- CALL <SYM>",
            "- add GREG, GREG, #1",
            "- bne <SYM>",
            "- pop {GREG, GREG, GREG, pc}",
            "+ mov GREG, #0",
            "+ bx lr",
        ],
    },
    # Compiler-generated TU static-init row with an EMPTY diff (UNPAIRED shape):
    # must still classify static-init LOW by name alone, never `unknown MED`.
    "_GLOBAL__I_ExampleScreen.cpp": {
        "reason": "unpaired",
        "diff": [],
    },
    # ScreenEffect::Activate (the guard-skips-work HIGH case): the port added an
    # `if (img.m_bAddedToHUD) continue;` bit-test guard the binary lacks (tst #1 +
    # beq back to the loop head) that branches PAST the per-image AddControl calls,
    # so no HUD control is ever created and every screen effect is invisible. The
    # binary has no such guard. NOT a benign return-default bail -> HIGH, not the
    # auto-accepted LOW port-guard.
    "_ZN12ScreenEffect8ActivateEv": {
        "reason": "26.2% LCS (191p vs 183b)",
        "diff": [
            "  ldr GREG, [GREG, #12]",
            "+ ldrb GREG, [GREG, #13]",
            "+ tst GREG, #1",
            "+ beq <SYM>",
            "  CALL <SYM>",
            "  CALL <SYM>",
            "  str GREG, [GREG, #16]",
            "  bx lr",
        ],
    },
}


def _gate_clearcombo(cause, lk):
    return lk == "HIGH" and cause == "wrong-field"


def _gate_critchance(cause, lk):
    return cause == "wrong-offset" and lk in ("HIGH", "MED")


def _gate_guard_skips_work(cause, lk):
    return lk == "HIGH" and cause == "guard-skips-work"


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
    ("_ZN12ScreenEffect8ActivateEv", "HIGH guard-skips-work", _gate_guard_skips_work),
    ("_ZN4Math22GetUncompressedSizeLZ8EPKv", "LOW (benign)", _gate_low_any),
    ("_ZN10MenuButton12HasNewSymbolEv", "NOT HIGH", _gate_not_high),
    ("_ZN10MenuButton15IsLoadingSymbolEv", "NOT HIGH", _gate_not_high),
    ("_ZN21FruitNinjaNewsControl6UpdateEf", "LOW port-stub-defunct",
     _gate_low_cause("port-stub-defunct")),
    ("_ZN6Mortar12ActorManager6UpdateEfP7ColAABBS2_", "LOW port-stub",
     _gate_low_cause("port-stub")),
    ("_GLOBAL__I_ExampleScreen.cpp", "LOW static-init",
     _gate_low_cause("static-init")),
]


def validate(report_path):
    # Live symbols are used only as a fallback for GATE entries that have no
    # canonical fixture (so freshly-added ground truth is still exercised).
    with open(report_path, "r", encoding="utf-8") as fh:
        report = json.load(fh)
    live = {s["mangled"]: s for s in report["symbols"]}
    print()
    print("=" * 72)
    print("VALIDATION GATE  --  canonical fixtures (classifier regression)")
    print("=" * 72)
    print("{:<44} {:<22} {:<18} {}".format("symbol", "expected", "got", "result"))
    print("-" * 72)
    all_pass = True
    for mangled, expect_desc, pred in GATE:
        # Prefer the pinned ground-truth fixture; fall back to the live symbol.
        fixture = GATE_FIXTURES.get(mangled)
        if fixture is not None:
            sym = {"mangled": mangled, "reason": fixture["reason"],
                   "diff": fixture["diff"]}
        else:
            sym = live.get(mangled)
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
    # Default report; allow override.
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.abspath(os.path.join(here, "..", ".."))
    report_path = argv[1] if len(argv) > 1 else os.path.join(
        root, "tmp", "asm-verify", "report.json")

    if not os.path.exists(report_path):
        print("report not found: {}".format(report_path), file=sys.stderr)
        return 2

    run(report_path)

    # The gate validates the detectors against canonical GATE_FIXTURES (pinned
    # ground truth), falling back to the live report only for entries lacking a
    # fixture. It is therefore stable across report regenerations.
    ok = validate(report_path)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
