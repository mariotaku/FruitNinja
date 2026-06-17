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


def normalize(text: str) -> list[str]:
    """Return canonical-form list of mnemonic lines."""
    out = []
    for raw in text.splitlines():
        if DROP_RE.match(raw) or PREAMBLE_RE.match(raw):
            continue
        line = raw
        # -- step 0: strip address prefix so byte stripping works --
        line = LINE_PATTERNS[0][0].sub(LINE_PATTERNS[0][1], line)
        # -- pre-normalization: strip objdump surface noise --
        line = OBJDUMP_BYTES_RE.sub('', line)               # raw hex bytes
        line = OBJDUMP_OFFSET_COMMENT_RE.sub('', line)      # "; 0xNN" comments
        line = CALL_OFFSET_RE.sub(r'\1 <', line)            # bl 0xNN <Name> → bl <Name>
        line = W_SUFFIX_RE.sub('', line)                    # strb.w → strb
        line = re.sub(r'\s+', ' ', line)                    # collapse whitespace
        # -- address/immediate masking (skip [0] already stripped above) --
        for pat, repl in LINE_PATTERNS[1:]:
            line = pat.sub(repl, line)
        # -- mnemonic unification --
        for pat, repl in MNEM_REWRITES:
            line = pat.sub(repl, line)
        # -- register-list collapse --
        line = LDM_RE.sub("ldm rN, {REGS}", line)
        line = STM_RE.sub("stm rN, {REGS}", line)
        line = PUSH_RE.sub("push {REGS}", line)
        line = POP_RE.sub("pop {REGS}", line)
        line = MOVR_RE.sub("mov rN, rN", line)
        line = line.strip()
        if not line:
            continue
        out.append(line)
    return out


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
        [str(OBJDUMP), "-d", "--no-show-raw-insn", f"--section={section}", str(obj_path)],
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


def classify(diff_lines: list[str]) -> tuple[str, str]:
    """Classify a unified diff hunk into a verdict + 1-line reason."""
    if not diff_lines:
        return "MATCH", "0 normalized lines differ"
    add_remove = [l for l in diff_lines if l.startswith(("+", "-")) and not l.startswith(("+++", "---"))]
    if not add_remove:
        return "MATCH", "all hunks were context-only"
    # Promote to SUSPICIOUS if any major opcode appears in adds/removes.
    for l in add_remove:
        if MAJOR_TOKENS.search(l):
            return "SUSPICIOUS", f"major opcode delta ({len(add_remove)} +/- lines)"
    return "COSMETIC", f"non-control-flow lines only ({len(add_remove)} +/- lines)"


def verify_one(s: dict) -> dict:
    name = s["mangled"]
    bin_asm_path = BINARY_SYMBOL_DIR / f"{name}.s"
    # `port` may be project-relative or absolute (Linux container path).
    port_obj_path = pathlib.Path(s["port"])
    if not port_obj_path.is_absolute():
        port_obj_path = PROJECT_ROOT / port_obj_path
    if not bin_asm_path.exists():
        return {**s, "verdict": "UNPAIRED", "reason": f"binary asm missing: {bin_asm_path.name}", "diff": []}

    # Preferred path: asm-differ. Better register-rename + reloc handling.
    if USE_ASM_DIFFER and port_obj_path.exists():
        ad = run_asm_differ(port_obj_path, bin_asm_path, name)
        if ad:
            verdict, reason = classify_asm_differ(ad)
            diff = render_asm_differ_text(ad)
            return {**s, "verdict": verdict, "reason": reason, "diff": diff,
                    "score": ad.get("current_score"),
                    "max_score": ad.get("max_score")}

    # Fallback: toy normalizer + difflib.
    try:
        port_text = disasm_port_symbol(port_obj_path, name)
    except Exception as e:
        return {**s, "verdict": "UNPAIRED", "reason": f"port disasm failed: {e}", "diff": []}
    if not port_text.strip():
        return {**s, "verdict": "UNPAIRED", "reason": "port symbol not found in .o", "diff": []}

    bin_lines = normalize(bin_asm_path.read_text())
    port_lines = normalize(port_text)

    diff = list(difflib.unified_diff(
        bin_lines, port_lines,
        fromfile=f"binary:{name}", tofile=f"port:{name}",
        lineterm="",
        n=2,
    ))
    verdict, reason = classify(diff)
    return {**s, "verdict": verdict, "reason": reason, "diff": diff,
            "bin_norm": bin_lines, "port_norm": port_lines}


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
