#!/usr/bin/env python3
"""asm-verify: per-symbol asm comparison between cross-build .o and binary.

Phase A:
- Reads tools/asm-verify-manifest.toml.
- For each [[symbol]]:
    * Disassembles the symbol from the cross-build .o (port side).
    * Reads the corresponding pre-exported binary asm from bada-binary/symbols/.
    * Normalizes both sides (strips literal addresses, ident strings, .L
      label numbering, branch-target offsets).
    * Diffs and classifies: MATCH / COSMETIC / SUSPICIOUS / DIVERGE / UNPAIRED.
- Writes tmp/asm-verify/report.md.

Phase B will swap the toy normalizer + line differ here for asm-differ proper.
"""
import argparse
import difflib
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
ASM_DIFFER = pathlib.Path(os.environ.get(
    "ASM_VERIFY_ASM_DIFFER",
    "/opt/asm-differ/diff.py"))
USE_ASM_DIFFER = ASM_DIFFER.exists()

# Triage sidecar: sticky decisions that downgrade SUSPICIOUS/DIVERGE rows
# the user (or asm-triager agent) has already classified as "accept". Keyed
# by the symbol's mangled name; an entry is invalidated when the diff content
# changes (we hash the asm-differ score+max as a cheap proxy).
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

# asm-verify is intentionally MNEMONIC-LEVEL: _parse_instr returns only the
# mnemonic column (parts[2]), so classify_lcs compares instruction-MNEMONIC
# sequences -- it is blind to operands (registers, struct offsets, constants).
#
# TODO(operand-level -- blocked on the exact compiler, task #34): making this
#   operand-aware (return mnem + ops, i.e. ' '.join(parts[2:])) would let it catch
#   wrong field-offset and wrong magic-number bugs. It was tried and REVERTED: the
#   cross-build uses Sourcery G++ 4.4.1, but the binary was built with 4.4-261 --
#   different compiler BUILDS schedule and select instructions differently, so
#   operand-level LCS floods with codegen noise that no normalization fixes (a
#   faithful class, Bomb, went 36 -> 40+ DIVERGE even with regalloc-tolerant
#   register abstraction + selective immediate de-norm). The de-norm design (keep
#   struct displacements [r0-r10,#off] + mov/movw constants, flatten the rest) is
#   in git history (commit df6aadd). RE-ENABLE once the cross-build uses the
#   binary's exact 4.4-261 compiler. Until then, operand-level precision comes from
#   the size-net (static_asserts) and the asm-inspector agent (LLM reading ASM),
#   not from this score.
def _parse_instr(line):
    """Extract the instruction MNEMONIC from an objdump line; None for
    non-instructions. (Mnemonic-only by design -- see the TODO above.)"""
    line = line.strip()
    if not line or 'Disassembly' in line or 'file format' in line:
        return None
    if ';' in line:
        line = line.split(';')[0].strip()
    parts = line.split('\t')
    if len(parts) < 2:
        return None
    if len(parts) == 2 and ':' in parts[0] and not parts[1].strip():
        return None
    return parts[2].strip() if len(parts) >= 3 else parts[1].strip()

def _norm_instr(instr):
    """Normalize one instruction to canonical form."""
    if not instr:
        return None
    parts = instr.split(None, 1)
    mnem = parts[0].lower() if parts else ''
    ops = parts[1] if len(parts) > 1 else ''

    # VFP size suffixes
    mnem = re.sub(r'(vldr|vstr)\.32', r'\1', mnem)
    mnem = re.sub(r'(vadd|vsub|vmul|vdiv|vneg|vabs|vsqrt|vcmp|vcmpe|vmov)\.f32', r'\1', mnem)
    mnem = re.sub(r'\.(32|64|f32|f64|s32|s64|u32|u64)\b', '', mnem)

    # Strip ARM condition codes
    mnem = re.sub(ARM_COND_CODES_RE + r'$', '', mnem)
    # Strip Thumb .w/.n
    mnem = re.sub(r'\.w$', '', mnem)
    mnem = re.sub(r'\.n$', '', mnem)
    # ARM->canonical
    mnem = ARM_TO_CANON.get(mnem, mnem)
    # Flag-setting: adds->add etc.
    if mnem.endswith('s') and len(mnem) > 2:
        base = mnem[:-1]
        if base in ARM_TO_CANON or base in ('add','sub','mov','and','orr','eor','bic','mul','lsl','lsr','asr','ror','adc','sbc','rsb','cmp','cmn','tst','teq'):
            mnem = ARM_TO_CANON.get(base, base)

    # Register canonicalization
    ops = re.sub(r'\bs(\d+)\b', r'V\1', ops)
    ops = re.sub(r'\bd(\d+)\b', r'D\1', ops)
    ops = re.sub(r'\br(1[3-5])\b', r'G\1', ops)
    ops = re.sub(r'\br([0-9]|1[0-2])\b', r'G\1', ops)
    ops = ops.replace('sp', 'SP').replace('lr', 'LR').replace('pc', 'PC')
    # Mask immediates and addresses
    ops = re.sub(r'#-?\d+', '#N', ops)
    ops = re.sub(r'#0x[0-9a-f]+', '#N', ops)
    ops = re.sub(r'\b0x[0-9a-f]+\b', 'ADDR', ops)
    ops = re.sub(r'\.L\d+', '.LX', ops)
    ops = re.sub(r'<\S+>', '<SYM>', ops)
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


def run_asm_differ(obj_path: pathlib.Path, bin_asm_path: pathlib.Path,
                   mangled: str) -> dict:
    """Invoke asm-differ in JSON mode for one symbol.

    Returns the parsed JSON dict on success, or {} on failure (lets the
    fall-back toy differ take over).
    """
    if not USE_ASM_DIFFER or not ASM_DIFFER.exists():
        return {}
    settings_src = ASM_VERIFY_DIR / "diff_settings.py"
    work = pathlib.Path("/tmp") / "asm-differ-work"
    work.mkdir(parents=True, exist_ok=True)
    # asm-differ reads diff_settings.py from cwd at startup.
    if not (work / "diff_settings.py").exists():
        try:
            (work / "diff_settings.py").write_text(settings_src.read_text())
        except Exception:
            return {}
    try:
        res = subprocess.run(
            [
                "python3", str(ASM_DIFFER),
                "-o",                                # diff .o files (recommended)
                "--no-pager",
                "--format=json",
                "-B",                                # don't visualise branches in output
                "-R",                                # don't show .rodata refs
                "-I",                                # ignore address differences
                "-i",                                # ignore large immediates
                "-j", f".text.{mangled}",            # restrict to per-fn section
                "--base-asm", str(bin_asm_path),     # pre-extracted binary asm
                "--file", str(obj_path),             # cross-build .o
                mangled,
            ],
            capture_output=True, text=True, cwd=str(work), timeout=30,
        )
    except Exception:
        return {}
    if res.returncode != 0 or not res.stdout.strip():
        return {}
    try:
        import json
        return json.loads(res.stdout)
    except Exception:
        return {}


def classify_asm_differ(d: dict) -> tuple[str, str]:
    """Convert asm-differ JSON result into our verdict + reason.

    Score is roughly proportional to Levenshtein edit cost on the asm token
    sequence -- ~50 score per single-line edit. Use absolute thresholds, not
    percent-of-max: max_score scales with function length, but a function
    with 5 missed lines is "small diff" regardless of total length.
    """
    score = d.get("current_score")
    max_score = d.get("max_score") or 0
    if score is None or max_score == 0:
        return "UNPAIRED", "asm-differ produced no score"
    pct = (score * 100) // max_score if max_score else 0
    if score == 0:
        return "MATCH", f"asm-differ {score}/{max_score} (identical)"
    if score < 50:
        return "COSMETIC", f"asm-differ {score}/{max_score} ({pct}% diff)"
    if score < 1500:
        return "SUSPICIOUS", f"asm-differ {score}/{max_score} ({pct}% diff)"
    return "DIVERGE", f"asm-differ {score}/{max_score} ({pct}% diff)"


def render_asm_differ_text(d: dict) -> list[str]:
    """Render an asm-differ JSON row list as plain-text diff lines."""
    out = []
    for row in d.get("rows", []):
        base = (row.get("base") or {}).get("text") or []
        cur  = (row.get("current") or {}).get("text") or []
        # Each text segment is {"text": "...", "format": "..."}.
        base_text = "".join(s.get("text", "") for s in base).rstrip()
        cur_text  = "".join(s.get("text", "") for s in cur).rstrip()
        # Skip pure-source-display rows (no asm).
        if not base_text and not cur_text:
            continue
        out.append(f"{base_text:<50} | {cur_text}")
    return out


def classify_lcs(port_lines, bin_lines):
    """Classify based on normalized LCS similarity.
    Returns (verdict, reason, score, max_score, diff_lines)."""
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

    # Pseudo-diff for report
    diff_lines = []
    for i in range(max(p_count, b_count)):
        p = port_lines[i] if i < p_count else ""
        b = bin_lines[i] if i < b_count else ""
        if p == b:
            diff_lines.append(f"  {p}")
        else:
            if i < b_count:
                diff_lines.append(f"- {b}")
            if i < p_count:
                diff_lines.append(f"+ {p}")

    return verdict, reason, score, max_score, diff_lines


def verify_one(s: dict) -> dict:
    name = s["mangled"]
    bin_asm_path = BINARY_SYMBOL_DIR / f"{name}.s"
    # `port` may be project-relative or absolute (Linux container path).
    port_obj_path = pathlib.Path(s["port"])
    if not port_obj_path.is_absolute():
        port_obj_path = PROJECT_ROOT / port_obj_path
    if not bin_asm_path.exists():
        return {**s, "verdict": "UNPAIRED", "reason": f"binary asm missing: {bin_asm_path.name}", "diff": []}

    # Semantic normalize + LCS scoring (primary path).
    try:
        port_text = disasm_port_symbol(port_obj_path, name)
    except Exception as e:
        return {**s, "verdict": "UNPAIRED", "reason": f"port disasm failed: {e}", "diff": []}
    if not port_text.strip():
        return {**s, "verdict": "UNPAIRED", "reason": "port symbol not found in .o", "diff": []}

    bin_lines = normalize(bin_asm_path.read_text())
    port_lines = normalize(port_text)

    verdict, reason, score, max_score, diff = classify_lcs(port_lines, bin_lines)
    return {**s, "verdict": verdict, "reason": reason, "diff": diff,
            "score": score, "max_score": max_score,
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
    when the asm-differ score still matches the triaged hash.
    """
    for r in results:
        name = r.get("mangled")
        entry = triage.get(name)
        if not entry:
            continue
        # Invalidate if score has drifted from the triage record.
        if r.get("score") != entry.get("score") or r.get("max_score") != entry.get("max_score"):
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
                lines.append("⚠ triage entry stale (score drifted since last triage)")
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
            "addr":    r.get("addr"),
            "verdict": r.get("verdict"),
            "reason":  r.get("reason"),
            "score":   r.get("score"),
            "max_score": r.get("max_score"),
            "diff":    r.get("diff", []),
            "triage_stale": r.get("triage_stale", False),
        })
    json_out.write_text(json.dumps({"symbols": json_payload}, indent=2))
    return out


def load_symbols(manifest_paths: list[pathlib.Path]) -> list[dict]:
    """Merge multiple manifests. Earlier paths take precedence on duplicates."""
    seen: dict[str, dict] = {}
    for path in manifest_paths:
        if not path.exists():
            continue
        for s in tomllib.loads(path.read_text()).get("symbol", []):
            seen.setdefault(s["mangled"], s)
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
