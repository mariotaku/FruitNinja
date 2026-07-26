#!/usr/bin/env python3
"""asm-verify: per-symbol asm comparison between cross-build .o and binary.

- Reads tools/asm-verify-manifest.toml.
- For each [[symbol]]:
    * Disassembles the symbol from the cross-build .o (port side).
    * Reads the corresponding pre-exported binary asm from bada-binary/symbols/.
    * Normalizes both sides at the OPERAND level (_norm_instr): abstracts
      reg-alloc / reloc-model codegen noise, keeps immediates, struct offsets
      and predication (real signal). See _parse_instr's block comment.
    * Diffs (LCS) and classifies: MATCH / COSMETIC / SUSPICIOUS / DIVERGE /
      UNPAIRED, with an LCS-aligned -/+ body in the report.
- Writes tmp/asm-verify/report.md (+ report.json for the asm-triager agent).

Operand-level scoring was validated once bada-sdk was confirmed to be the
binary's exact compiler (task #55); a standalone asm-differ integration that
once stood in for this was removed (task #63) in favour of the in-house
operand normalizer.
"""
import argparse
import difflib
import hashlib
import os
import pathlib
import re
import subprocess
import sys
import textwrap

try:
    import tomllib
except ImportError:
    import tomli as tomllib  # type: ignore

ASM_VERIFY_DIR = pathlib.Path(__file__).resolve().parent
PROJECT_ROOT   = ASM_VERIFY_DIR.parent.parent
OBJDUMP = pathlib.Path(os.environ.get(
    "ASM_VERIFY_OBJDUMP",
    PROJECT_ROOT / "tools" / "toolchain" / "sourcery-2010q1" / "bin" / "arm-none-eabi-objdump"))
OUT_DIR = pathlib.Path(os.environ.get(
    "ASM_VERIFY_REPORT_DIR",
    PROJECT_ROOT / "tmp" / "asm-verify"))
BINARY_SYMBOL_DIR = pathlib.Path(os.environ.get(
    "ASM_VERIFY_BIN_SYMBOL_DIR",
    PROJECT_ROOT / "bada-binary" / "symbols"))

# Ghidra loads this ELF at image_base 0x10000; nm/LIEF report raw ELF
# .st_value with no such offset. Every source-side `@0x<addr>` marker and
# every Ghidra address is image_base-relative (canonical definition:
# stale-marker-lint.py's load_binary_symbols). The manifest's "addr" field
# (from discover-symbols.py's nm read) is RAW -- it must STAY raw, since
# export-binary-symbols.py uses it as a literal `objdump --start-address`
# into the unmodified ELF. Only the report-facing `addr` (report.md /
# report.json, consumed by humans + asm-triager cross-referencing markers)
# is converted to Ghidra convention -- see verify_one().
GHIDRA_IMAGE_BASE = 0x10000


def _to_ghidra_addr(raw_hex: str) -> str:
    """Convert a raw nm/LIEF addr string ("0x00109dfc") to Ghidra convention
    ("0x00119dfc" = raw + GHIDRA_IMAGE_BASE) for report display."""
    return "0x{:08x}".format(int(raw_hex, 16) + GHIDRA_IMAGE_BASE)

# Triage sidecar: sticky decisions that downgrade SUSPICIOUS/DIVERGE rows
# the user (or asm-triager agent) has already classified as "accept". Keyed
# by the symbol's mangled name; an entry is invalidated when the normalized
# asm content changes (we store a sha256 of the normalized port+binary lines,
# so cosmetic scorer tweaks don't silently wipe human triage decisions).
TRIAGE_PATH = pathlib.Path(os.environ.get(
    "ASM_VERIFY_TRIAGE_PATH",
    ASM_VERIFY_DIR / "triage.json"))

# Lines removed entirely before diffing.
DROP_RE = re.compile(
    r"^\s*("
    r"#\s|"                          # our header comments
    r"\.ident\s|"                    # GCC: ... ident strings
    r"\.size\s|"                     # .size foo, .-foo
    r"\.thumb_func|"                 # marker
    r"\.align\s|"                    # alignment padding
    r"\.section\s|"                  # section directives
    r"\.global\s|"
    r"\.type\s|"
    r"\.file\s|"
    r"\.cpu\s|"
    r"\.fpu\s|"
    r"\.eabi_attribute\s|"
    r"\.thumb\s*$|"
    r"\.syntax\s|"
    r"\.text\s*$|"
    r"\.bss\s*$|"
    r"\.data\s*$|"
    r"^$"
    r")"
)

# objdump preamble lines we want to drop wholesale, not just per-line patterns.
PREAMBLE_RE = re.compile(
    r"^("
    r".*\bfile format\b.*|"           # "...obj: file format elf32-littlearm"
    r"Disassembly of section\b.*|"   # section banner
    r"[0-9a-f]+\s*<.*>:\s*$|"        # function entry header `00111f74 <_ZN...>:`
    r"\s*\.\.\.|"                     # ellipsis
    r")$"
)

# Per-symbol mnemonic normalisation. Map equivalent encodings to a single
# canonical token so register-allocation drift and rel-vs-abs branches
# don't mask the actual diff.
MNEM_REWRITES = [
    # Treat bl/blx as the same logical "call". Asm-differ does this too.
    (re.compile(r"\bblx?\b"), "CALL"),
    # Branches: keep direction (cbz/cbnz/b/beq...) but ignore distance form.
    # (b vs b.n vs b.w are encoding-size choices)
    (re.compile(r"\bb\.[wn]\b"), "b"),
]

# --- Pre-normalization: canonicalise objdump output format ---
# Both binary and port sides come from the SAME arm-none-eabi-objdump,
# so there are no cross-disassembler formatting differences. Only
# objdump surface noise to strip.

# Objdump prints raw instruction bytes before the mnemonic.
# "f7ff fffe  bl 0 <Name>" -> "bl 0 <Name>"
OBJDUMP_BYTES_RE = re.compile(r"^\s*([0-9a-f]{4}\s+)+")

# Objdump appends "; 0xNN" offset comments to ldr/str instructions.
# "ldrb r6, [r0, #320]  ; 0x140" -> "ldrb r6, [r0, #320]"
OBJDUMP_OFFSET_COMMENT_RE = re.compile(r"\s*;\s*0x[0-9a-f]+\s*$")

# Port .o has "bl 0 <Name>" (relative offset 0 in unlinked .o).
# Binary ELF has "bl <offset> <Name>" with a real offset.
# Normalise both to "bl <Name>".
CALL_OFFSET_RE = re.compile(r"\b(blx?)\s+[0-9a-f]+\s+<")

# Objdump appends .w to wide Thumb-2 encodings.
W_SUFFIX_RE = re.compile(r"\.w\b")

# Multi-register ldm/stm: collapse register lists.
LDM_RE = re.compile(r"\bldm(?:ia|db|\.w)?\s+r\d+,\s*\{[^}]*\}")
STM_RE = re.compile(r"\bstm(?:ia|db|\.w)?\s+r\d+,\s*\{[^}]*\}")

# Collapse pop/push register lists.
POP_RE   = re.compile(r"\bpop\b\s*\{[^}]*\}")
PUSH_RE  = re.compile(r"\bpush\b\s*\{[^}]*\}")

# Collapse single mov rN, rN for register-allocation noise.
MOVR_RE  = re.compile(r"\bmov\s+r\d+,\s*r\d+")

# --- After pre-normalization, mask remaining literals / addresses ---
#   - leading hex address ("  111f74:") -> ""
#   - branch target labels (".L\d+", "<sym+0xNN>") -> "LBL"
#   - literal-pool offsets (e.g. "[pc, #0x28]") -> "[pc, #IMM]"
#   - bl/blx target addresses ("blx 0x123") -> "blx LBL"
LINE_PATTERNS = [
    (re.compile(r"^\s*[0-9a-f]+:\s*"), ""),
    (re.compile(r"<\S+\+0x[0-9a-f]+>"), "<+OFF>"),
    (re.compile(r"<\S+>"), "<SYM>"),
    (re.compile(r"\.L\d+"), ".LX"),
    (re.compile(r"#\s*0x[0-9a-f]+"), "#IMM"),
    (re.compile(r"#\s*-?\d+"), "#IMM"),
    (re.compile(r"0x[0-9a-f]{4,}"), "ADDR"),
]

# Patterns that must not differ -- if these line-tokens diverge between
# binary and port, the symbol is escalated as SUSPICIOUS.
MAJOR_TOKENS = re.compile(
    r"\b(bl|blx|b|beq|bne|bge|ble|bgt|blt|bhi|bls|bcc|bcs|cbz|cbnz|"
    r"vcmpe?|vcmp|str|strb|strh|ldr|ldrb|ldrh)\b"
)


# ==== Semantic normalization (Phase B) ====

ARM_COND_CODES_RE = r'(eq|ne|cs|hs|cc|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le|al)'
ARM_TO_CANON = {
    'stmdb': 'push', 'stmfd': 'push', 'ldmia': 'pop', 'ldmfd': 'pop',
    'fstmias': 'vpush', 'fstmiad': 'vpush',
    'fldmias': 'vpop',  'fldmiad': 'vpop',
    'vstmia': 'vstm', 'vldmia': 'vldm',
}

# asm-verify is now OPERAND-LEVEL (task #34/#56, ENABLED 2026-06-21).
#
# Previously _parse_instr/_norm_instr returned only the mnemonic column so
# classify_lcs was blind to operands (registers, struct offsets, constants).
# Operand-level was tried+reverted once (commit df6aadd) because the cross-build
# compiler wasn't confirmed to match the binary's, so operand LCS flooded with
# codegen noise. Task #55 REMOVED that blocker: it empirically confirmed
# bada-sdk == the binary's exact compiler (Samsung Sourcery G++ 4.4-261 /
# "Sourcery 4.4-157" upstream) -- Bomb::Chuck compiled BYTE-IDENTICAL, and a
# controlled clamp-idiom test reproduced the binary's exact vcmpe / it pl /
# vmovpl predicated-move encoding. With the compiler confirmed, operand-level
# divergence is REAL SIGNAL (wrong struct offset / wrong magic constant /
# missing predication-clamp), not codegen skew.
#
# _norm_instr now NORMALIZES only codegen noise and KEEPS real signal:
#   NORMALIZE (noise): register names (s/d/r -> VREG/DREG/GREG, sp/lr/pc kept),
#     symbol/.LANCHOR/.L refs -> <SYM>, .w/.n + VFP type suffixes, [pc,#off]
#     pool-slot offset -> [pc,#POOL], bl/blx -> CALL.
#   KEEP STRICT (signal): immediates / movw|movt constants, struct displacement
#     [GREG,#off], condition-code predication suffixes (vmovls/movne/it ls/...),
#     literal-pool .word values, branch direction (beq/bne/...).
def _parse_instr(line):
    """Extract the instruction (mnemonic + operands) from an objdump line;
    None for non-instructions. (Operand-level -- see the block comment above.)"""
    line = line.strip()
    if not line or 'Disassembly' in line or 'file format' in line:
        return None
    # Strip objdump's trailing "; <comment>" (pool target, redundant hex of an
    # immediate, etc.) -- but keep the instruction + operand columns intact.
    if ';' in line:
        line = line.split(';')[0].strip()
    parts = line.split('\t')
    if len(parts) < 2:
        return None
    if len(parts) == 2 and ':' in parts[0] and not parts[1].strip():
        return None
    # objdump column layout: [addr:][raw-bytes][mnemonic][operands].
    # Operand-level: return mnemonic + operands joined (parts[2:]), not just the
    # mnemonic column. For lines without a raw-bytes column, fall back to parts[1:].
    if len(parts) >= 3:
        return ' '.join(p.strip() for p in parts[2:]).strip()
    return parts[1].strip()


def _norm_instr(instr):
    """Normalize ONE instruction to canonical operand-level form.

    Strips codegen noise (reg-alloc, reloc model, encoding-size choices) but
    KEEPS real signal (immediates, struct offsets, predication). See the block
    comment above _parse_instr for the full NORMALIZE/KEEP policy (task #55)."""
    if not instr:
        return None
    parts = instr.split(None, 1)
    mnem = parts[0].lower() if parts else ''
    ops = parts[1] if len(parts) > 1 else ''

    # Drop nop / alignment padding. The PORT side disassembles the whole
    # .text.<sym> section, whose tail includes inter-function alignment `nop`s
    # (objdump renders them as `nop`, `nop.w`, `nop {0}`); the BINARY side is an
    # exact byte-range extract with no padding, so these are pure noise that
    # inflates the port line count. (asm-differ drops nops the same way.)
    if mnem in ('nop', 'nop.w', 'nop.n') or mnem.startswith('nop'):
        return None

    # --- bl/blx -> CALL (logical call; keep direction-less) ---
    if mnem in ('bl', 'blx'):
        return 'CALL <SYM>'

    # --- VFP / size type suffixes are an encoding choice: strip (rule 3) ---
    mnem = re.sub(r'\.(8|16|32|64|f16|f32|f64|s8|s16|s32|s64|u8|u16|u32|u64|i8|i16|i32|i64|p8)\b', '', mnem)
    # --- Thumb width suffix .w/.n (rule 3) ---
    mnem = re.sub(r'\.[wn]$', '', mnem)

    # --- ARM<->Thumb push/pop/vpush/vpop canonicalisation ---
    mnem = ARM_TO_CANON.get(mnem, mnem)

    # KEEP predication suffixes (rule 8): do NOT strip condition codes. The
    # clamp idiom (vmovls / vmovpl / bxle / it pl ...) is real port-faithfulness
    # signal -- #55 proved a `>`-max vs binary `<`-min clamp surfaces here.

    # Register canonicalization (rule 1): abstract reg-alloc, keep sp/lr/pc.
    # VFP single s0..s31 -> VREG, double d0..d31 -> DREG, GP r0..r12 -> GREG.
    ops = re.sub(r'\bs([0-9]|[12][0-9]|3[01])\b', 'VREG', ops)
    ops = re.sub(r'\bd([0-9]|[12][0-9]|3[01])\b', 'DREG', ops)
    ops = re.sub(r'\br([0-9]|1[0-2])\b', 'GREG', ops)
    # r13/r14/r15 are sp/lr/pc -- keep them DISTINCT, not GREG.
    ops = re.sub(r'\br13\b', 'sp', ops)
    ops = re.sub(r'\br14\b', 'lr', ops)
    ops = re.sub(r'\br15\b', 'pc', ops)
    # GP register aliases objdump sometimes prints instead of rN: ip=r12,
    # fp=r11, sl=r10, sb=r9, and the ATPCS a1-a4 / v1-v8 names. All are GP
    # reg-alloc -> GREG (rule 1). (Order matters: these run after r0-r12.)
    ops = re.sub(r'\b(ip|fp|sl|sb)\b', 'GREG', ops)
    ops = re.sub(r'\b(a[1-4]|v[1-8])\b', 'GREG', ops)

    # PC-relative literal-pool load offset is a slot index, not semantic (rule 4):
    #   [pc, #28] / [pc, #0x1c] -> [pc, #POOL]   (must run BEFORE keep-imm logic,
    #   and must NOT touch [GREG, #off] which is a struct displacement -- rule 7).
    ops = re.sub(r'\[pc,\s*#-?(?:0x[0-9a-f]+|\d+)\]', '[pc, #POOL]', ops)

    # Symbol / anchor / local-label relocation model -> ONE canonical ref (rule 2).
    #   <_ZN..>, <name+0x4>, .LANCHOR3+0x8, .L12  all collapse to <SYM>.
    ops = re.sub(r'<[^>]*>', '<SYM>', ops)
    ops = re.sub(r'\.LANCHOR\d+(?:\s*\+\s*0x[0-9a-f]+)?', '<SYM>', ops)
    ops = re.sub(r'\.L\d+', '<SYM>', ops)
    # Branch-target form "b<cc> <addr> <SYM>" -- objdump prints the target
    # address (no 0x prefix) before the symbol. In the linked binary that's a
    # real address; in the unlinked port .o it's a placeholder `0` (the reloc
    # isn't applied yet). Either way it's reloc-model noise (rule 2); the <SYM>
    # already carries the destination. Match 1+ hex digits to catch both.
    ops = re.sub(r'\b[0-9a-f]+\s+<SYM>', '<SYM>', ops)

    # KEEP immediates strict (rules 6/7/9): canonicalise hex<->dec so disassembler
    # formatting (0x48 vs 72) never trips, but PRESERVE the literal value.
    def _canon_imm(m):
        try:
            return '#%d' % int(m.group(1), 0)
        except ValueError:
            return m.group(0)
    ops = re.sub(r'#(-?0x[0-9a-f]+)\b', _canon_imm, ops)
    ops = re.sub(r'#(-?\d+)\b', _canon_imm, ops)

    # KEEP literal-pool .word values strict (rule 9): canonicalise the hex.
    if mnem == '.word':
        ops = re.sub(r'\b(0x[0-9a-f]+|\d+)\b',
                     lambda m: '0x%x' % int(m.group(1), 0), ops)
    else:
        # Bare residual addresses (shouldn't remain after the above, but any
        # stray 0x.... that isn't a kept immediate is an address -> flatten).
        ops = re.sub(r'\b0x[0-9a-f]+\b', 'ADDR', ops)

    ops = re.sub(r'\s+', ' ', ops).strip()
    return f"{mnem} {ops}".strip()

def normalize(text):
    """Semantic normalization: strip encoding noise, keep structural signal."""
    result = []
    for line in text.strip().split('\n'):
        stripped = line.strip()
        if not stripped:
            continue
        # Skip directives, labels, function headers
        if re.match(r'^\s*(\.(ident|size|thumb_func|align|section|global|type|file|cpu|fpu|eabi_attribute|thumb|syntax|text|bss|data)\s|Disassembly|file format|^\s*[0-9a-f]+\s*<.*>:\s*$)', stripped):
            continue
        instr = _parse_instr(line)
        if instr is None:
            continue
        norm = _norm_instr(instr)
        if norm:
            result.append(norm)

    # PIC/GOT literal-pool relocation words are reloc-model noise (rule 2), NOT
    # data constants. In the unlinked port .o they're tiny unresolved offsets
    # (.word 0x8 / 0x0); in the linked binary they're real GOT/anchor addresses
    # (.word 0x186138). They pair with the PC-relative base idiom
    # `add GREG, pc, GREG`. If a function uses that idiom, flatten its `.word`
    # pool entries to `.word PICOFF` so the offset mismatch doesn't mask the real
    # instruction-stream comparison. Functions WITHOUT the idiom (e.g. a leaf
    # clamp like Bomb::Chuck) keep `.word 0x3e4ccccd` strict (rule 9) -- those
    # are genuine float/int data constants and a real bug signal.
    is_pic = any(re.match(r'add GREG, pc, GREG\b', l) for l in result)
    if is_pic:
        result = [re.sub(r'^\.word .*$', '.word PICOFF', l) for l in result]
    return result


def disasm_port_symbol(obj_path: pathlib.Path, mangled: str) -> str:
    """Disassemble one symbol from a cross-build .o.

    The bada SDK 4.5.3 binutils' objdump doesn't accept `--disassemble=<sym>`,
    so we lean on `-ffunction-sections` (set by the toolchain) which puts each
    function in its own `.text.<mangled>` section, and dump just that section.
    """
    if not obj_path.exists():
        raise FileNotFoundError(obj_path)
    section = f".text.{mangled}"
    res = subprocess.run(
        [str(OBJDUMP), "-d", f"--section={section}", str(obj_path)],
        capture_output=True, text=True, check=True,
    )
    return res.stdout


def classify_lcs(port_lines, bin_lines):
    """Classify based on normalized LCS similarity.
    Returns (verdict, reason, score, max_score, diff_lines).

    Thresholds (operand-level baseline, task #56): MATCH >=95, COSMETIC >=85,
    SUSPICIOUS >=60, else DIVERGE. These were RE-VALIDATED after the move to
    operand-level normalization (_norm_instr) -- a byte-identical function
    (Bomb::Chuck, #55) lands MATCH 100%, and a single wrong-struct-offset line
    in a short function (WaveManager::GetCriticalChance #116-vs-#112) lands
    COSMETIC, so the cutoffs still separate faithful from divergent at the new
    granularity. KNOWN residual noise: global-heavy functions diverge on the
    linked-binary-vs-unlinked-.o GOT idiom (linked: `add GREG,pc,GREG` +
    `ldr GREG,[GREG,GREG]`; unlinked -fpic: a longer GOT-address build). That's
    a multi-instruction reloc-model mismatch rule 2 only partially absorbs, so
    such functions over-weight toward DIVERGE -- but they were already escalated
    (SUSPICIOUS) at mnemonic-level, never MATCH, so no real bug is hidden."""
    p_count = len(port_lines)
    b_count = len(bin_lines)

    # LCS scoring
    m, n = p_count, b_count
    if max(m, n) == 0:
        sim = 100.0
    else:
        dp = [[0] * (n + 1) for _ in range(m + 1)]
        for i in range(m):
            for j in range(n):
                if port_lines[i] == bin_lines[j]:
                    dp[i+1][j+1] = dp[i][j] + 1
                else:
                    dp[i+1][j+1] = max(dp[i+1][j], dp[i][j+1])
        sim = dp[m][n] / max(m, n) * 100

    score = int(1000 * (1.0 - sim / 100.0) * max(b_count, 1))
    max_score = max(b_count, 1)

    if sim >= 95:
        verdict = "MATCH"
    elif sim >= 85:
        verdict = "COSMETIC"
    elif sim >= 60:
        verdict = "SUSPICIOUS"
    else:
        verdict = "DIVERGE"
    reason = f"{sim:.1f}% LCS ({p_count}p vs {b_count}b)"

    # Aligned diff for the report. A naive positional zip (port[i] vs bin[i])
    # mis-renders: one inserted line shifts every following line and shows them
    # all as changed even on a high-LCS MATCH, which misleads triage. Use the
    # same longest-common-subsequence alignment the SCORE is based on
    # (difflib.SequenceMatcher) so the displayed -/+ hunks match the verdict.
    # `- ` = binary-only, `+ ` = port-only, `  ` = common.
    diff_lines = []
    sm = difflib.SequenceMatcher(a=bin_lines, b=port_lines, autojunk=False)
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal":
            for line in bin_lines[i1:i2]:
                diff_lines.append(f"  {line}")
        else:  # replace / delete / insert
            for line in bin_lines[i1:i2]:
                diff_lines.append(f"- {line}")
            for line in port_lines[j1:j2]:
                diff_lines.append(f"+ {line}")

    return verdict, reason, score, max_score, diff_lines


def verify_one(s: dict) -> dict:
    name = s["mangled"]
    # Optional symbol alias: the port's real body for this binary symbol lives
    # under a different mangled name (see manifest.toml header, `port_mangled`).
    # ONLY the port-side disassembly uses the alias; the binary side keeps
    # using `mangled` (the binary symbol name / exported .s file).
    port_name = s.get("port_mangled", name)
    # s["addr"] (from the manifest) is RAW nm/LIEF convention. Every returned
    # dict below overrides "addr" with the Ghidra-convention value (report
    # display) and keeps the raw value under "raw_addr" (internal bookkeeping;
    # nothing downstream needs it -- extraction already happened by name in
    # export-binary-symbols.py -- but it's cheap to keep for debugging).
    raw_addr = s["addr"]
    addr = _to_ghidra_addr(raw_addr)
    bin_asm_path = BINARY_SYMBOL_DIR / f"{name}.s"
    # `port` may be project-relative or absolute (Linux container path).
    port_obj_path = pathlib.Path(s["port"])
    if not port_obj_path.is_absolute():
        port_obj_path = PROJECT_ROOT / port_obj_path
    if not bin_asm_path.exists():
        return {**s, "addr": addr, "raw_addr": raw_addr, "verdict": "UNPAIRED",
                "reason": f"binary asm missing: {bin_asm_path.name}", "diff": []}

    # Semantic normalize + LCS scoring (primary path).
    try:
        port_text = disasm_port_symbol(port_obj_path, port_name)
    except Exception as e:
        return {**s, "addr": addr, "raw_addr": raw_addr, "verdict": "UNPAIRED",
                "reason": f"port disasm failed: {e}", "diff": []}
    if not port_text.strip():
        return {**s, "addr": addr, "raw_addr": raw_addr, "verdict": "UNPAIRED",
                "reason": f"port symbol {port_name} not found in .o", "diff": []}

    bin_lines = normalize(bin_asm_path.read_text())
    port_lines = normalize(port_text)

    verdict, reason, score, max_score, diff = classify_lcs(port_lines, bin_lines)
    # Content hash of the NORMALIZED asm -- the triage staleness key. Keying on
    # this (not the score) means a cosmetic scorer/threshold tweak that leaves
    # the normalized instruction streams unchanged preserves human triage; only
    # an actual change to the compared asm invalidates a decision.
    asm_hash = hashlib.sha256(
        ("\n".join(bin_lines) + "\x00" + "\n".join(port_lines)).encode("utf-8")
    ).hexdigest()[:16]
    return {**s, "addr": addr, "raw_addr": raw_addr, "verdict": verdict, "reason": reason,
            "diff": diff, "score": score, "max_score": max_score, "asm_hash": asm_hash,
            "port_norm": port_lines, "bin_norm": bin_lines}


def load_triage() -> dict:
    """Load tools/asm-verify/triage.json. Returns {} if missing or malformed."""
    if not TRIAGE_PATH.exists():
        return {}
    try:
        import json
        return json.loads(TRIAGE_PATH.read_text())
    except Exception:
        return {}


def apply_triage(results: list[dict], triage: dict) -> list[dict]:
    """Replace SUSPICIOUS/DIVERGE verdicts with the agent's sticky decision
    when the normalized asm still hashes to the triaged value.
    """
    for r in results:
        name = r.get("mangled")
        entry = triage.get(name)
        if not entry:
            continue
        # Invalidate if the normalized-asm hash has drifted from the triage
        # record. Entries written before asm_hash existed (only score/max_score)
        # are treated as stale -- re-triage against the current baseline.
        entry_hash = entry.get("asm_hash")
        if entry_hash is None or r.get("asm_hash") != entry_hash:
            r["triage_stale"] = True
            continue
        # Sticky verdict from triage -- prefix verdict with "(t)" so it's clear.
        new_verdict = entry.get("verdict")
        if new_verdict in ("ACCEPT-cosmetic", "ACCEPT-deferred", "ACCEPT-defunct", "FIX-NEEDED"):
            r["verdict"] = new_verdict
            r["reason"] = "triaged: " + entry.get("reason", entry.get("verdict"))
    return results


def write_report(results: list[dict]) -> pathlib.Path:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    out = OUT_DIR / "report.md"
    lines = ["# asm-verify report", ""]
    counts = {}
    for r in results:
        counts[r["verdict"]] = counts.get(r["verdict"], 0) + 1
    lines.append("## Summary")
    for v in ("MATCH", "COSMETIC", "ACCEPT-cosmetic", "ACCEPT-deferred",
              "ACCEPT-defunct", "SUSPICIOUS", "FIX-NEEDED", "DIVERGE", "UNPAIRED"):
        if v in counts:
            lines.append(f"- {v}: {counts[v]}")
    lines.append("")
    lines.append("## Per-symbol verdicts")
    lines.append("")
    lines.append("| Verdict | Symbol | Binary @ | Reason |")
    lines.append("|---|---|---|---|")
    for r in results:
        lines.append(f"| {r['verdict']} | `{r['mangled']}` | `{r['addr']}` | {r['reason']} |")
    lines.append("")
    # Diff bodies for SUSPICIOUS/DIVERGE/FIX-NEEDED only -- cuts noise.
    escalations = [r for r in results
                   if r["verdict"] in ("SUSPICIOUS", "DIVERGE", "FIX-NEEDED")]
    if escalations:
        lines.append("## Escalations (need triage)")
        for r in escalations:
            lines.append("")
            lines.append(f"### `{r['mangled']}` @ `{r['addr']}`")
            stale = r.get("triage_stale", False)
            if stale:
                lines.append("")
                lines.append("WARNING: triage entry stale (normalized asm changed since last triage)")
            lines.append("")
            lines.append("Notes: " + r.get("notes", ""))
            if r.get("score") is not None:
                lines.append(f"Score: {r['score']}/{r.get('max_score', '?')}")
            lines.append("")
            lines.append("```diff")
            lines.extend(r["diff"])
            lines.append("```")
    out.write_text("\n".join(lines) + "\n")

    # Also write a JSON form -- consumed by the asm-triager agent.
    import json
    json_out = OUT_DIR / "report.json"
    json_payload = []
    for r in results:
        json_payload.append({
            "mangled": r.get("mangled"),
            "addr":    r.get("addr"),       # Ghidra convention (raw + 0x10000) -- matches src markers + Ghidra.
            "raw_addr": r.get("raw_addr"),  # raw nm/LIEF convention (objdump/ELF-native).
            "verdict": r.get("verdict"),
            "reason":  r.get("reason"),
            "port_mangled": r.get("port_mangled"),  # set only for aliased symbols
            "score":   r.get("score"),
            "max_score": r.get("max_score"),
            "asm_hash": r.get("asm_hash"),
            "diff":    r.get("diff", []),
            "triage_stale": r.get("triage_stale", False),
        })
    json_out.write_text(json.dumps({"symbols": json_payload}, indent=2))
    return out


def load_symbols(manifest_paths: list[pathlib.Path]) -> list[dict]:
    """Merge multiple manifests. Earlier paths take precedence on duplicates,
    PER KEY: a later manifest fills in keys the earlier entry omitted. This
    lets a hand-written override carry only what it overrides (e.g. just
    `mangled` + `port_mangled` + `notes`) while addr/size/port keep coming
    from the auto-generated manifest -- those are machine-derived and the
    port .o path is environment-specific (container vs host build dir)."""
    seen: dict[str, dict] = {}
    for path in manifest_paths:
        if not path.exists():
            continue
        for s in tomllib.loads(path.read_text()).get("symbol", []):
            name = s["mangled"]
            if name in seen:
                merged = s.copy()
                merged.update(seen[name])  # earlier-manifest keys win
                seen[name] = merged
            else:
                seen[name] = s
    return list(seen.values())


def main():
    import fnmatch
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "manifest",
        nargs="?",
        default=None,
        help="Optional explicit manifest path. Default: load both "
             "tools/asm-verify-manifest.toml (hand-written, takes precedence) "
             "and tools/asm-verify-manifest.generated.toml.",
    )
    ap.add_argument(
        "--filter", default=None,
        help="Glob applied to mangled symbol names (e.g. '_ZN11WaveManager*'). "
             "Useful for verifying only the touched subsystem.",
    )
    ap.add_argument(
        "--jobs", "-j", type=int, default=0,
        help="Parallel workers. 0 = os.cpu_count(). 1 = serial.",
    )
    ap.add_argument(
        "--report-only", action="store_true",
        help="Always exit 0; report unconditionally. Useful in CI fail-soft mode.",
    )
    args = ap.parse_args()

    if args.manifest:
        manifests = [pathlib.Path(args.manifest)]
    else:
        manifests = [
            ASM_VERIFY_DIR / "manifest.toml",
            ASM_VERIFY_DIR / "manifest.generated.toml",
        ]

    syms = load_symbols(manifests)
    if not syms:
        sys.exit("No [[symbol]] entries in any manifest.")

    # A hand-written entry pairs with its generated counterpart per key; if
    # the generated manifest has no row for it (symbol dropped from the binary
    # nm intersection, stale alias, typo), the merged entry lacks addr/port
    # and cannot be verified. Surface it instead of crashing the worker pool.
    incomplete = [s for s in syms if "addr" not in s or "port" not in s]
    if incomplete:
        for s in incomplete:
            print(f"  WARN: skipping {s['mangled']}: no addr/port "
                  f"(no matching entry in the generated manifest)", file=sys.stderr)
        syms = [s for s in syms if "addr" in s and "port" in s]

    if args.filter:
        before = len(syms)
        syms = [s for s in syms if fnmatch.fnmatch(s["mangled"], args.filter)]
        print(f"  filtered {before} -> {len(syms)} symbols matching {args.filter!r}")
        if not syms:
            sys.exit(0)

    triage = load_triage()

    # Parallel batch verify.
    jobs = args.jobs or os.cpu_count() or 1
    if jobs == 1:
        results = [verify_one(s) for s in syms]
    else:
        from concurrent.futures import ProcessPoolExecutor
        with ProcessPoolExecutor(max_workers=jobs) as ex:
            results = list(ex.map(verify_one, syms))

    # Apply sticky triage decisions before rendering the report.
    results = apply_triage(results, triage)

    out = write_report(results)
    try:
        out_disp = out.relative_to(PROJECT_ROOT).as_posix()
    except ValueError:
        out_disp = out.as_posix()
    print(f"\nReport: {out_disp}\n")
    counts: dict[str, int] = {}
    for r in results:
        counts[r["verdict"]] = counts.get(r["verdict"], 0) + 1
    for v in ("MATCH", "COSMETIC", "ACCEPT-cosmetic", "ACCEPT-deferred",
              "ACCEPT-defunct", "SUSPICIOUS", "FIX-NEEDED", "DIVERGE", "UNPAIRED"):
        if v in counts:
            print(f"  {v:16} {counts[v]}")
    # FIX-NEEDED is "user said this is genuinely broken"; treat as failure.
    # DIVERGE / UNPAIRED / SUSPICIOUS escalate by default; ACCEPT-* downgrade.
    bad = [r for r in results
           if r["verdict"] in ("SUSPICIOUS", "DIVERGE", "UNPAIRED", "FIX-NEEDED")]
    if args.report_only:
        sys.exit(0)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
