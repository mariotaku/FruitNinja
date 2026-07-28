#!/usr/bin/env python3
"""resolve-eval: measure what --resolve-operands adds, and what it costs.

Compares two asm-verify report.json snapshots of the SAME tree -- one produced
with the flag off, one with it on -- and classifies every divergence line the
flag introduced. Both runs must be made with the same triage setting (use
ASM_VERIFY_TRIAGE_PATH=/nonexistent to compare RAW verdicts).

A line the flag introduced is one whose text, with the annotation stripped, is
identical on both sides. Those split three ways:

  CONFLICT-NAME   both sides resolved to a real symbol, and the symbols differ
                  -> SIGNAL. This is the class the tool was structurally blind
                     to (wrong call target, wrong global).
  CONFLICT-VALUE  both sides read a non-relocated read-only literal-pool word,
                  and the bit patterns differ -> SIGNAL (wrong magic constant
                  in a PIC function, previously masked by `.word PICOFF`).
  ASYMMETRIC      one side resolved, the other did not -> NOISE candidate. The
                  resolver saw through the relocation on one side only, so the
                  divergence says nothing about the port.

Everything else in the diff was already diverging with the flag off.

Machine-readable output: tmp/asm-verify/resolve-eval/eval.json (source of truth).
Stdout: a short ranked summary.

    python3 tools/asm-verify/resolve-eval.py <off.json> <on.json>
"""
import argparse
import collections
import json
import os
import pathlib
import re
import sys

ASM_VERIFY_DIR = pathlib.Path(__file__).resolve().parent
PROJECT_ROOT = ASM_VERIFY_DIR.parent.parent
OUT_DIR = pathlib.Path(os.environ.get(
    "ASM_VERIFY_REPORT_DIR", PROJECT_ROOT / "tmp" / "asm-verify")) / "resolve-eval"

# " CALL =_ZN4Math6Random6Rand32Em"  /  " ldr GREG, [pc, #POOL] {=game_work}"
# Annotations always sit at the END of a normalized line, and a DEMANGLED name
# contains spaces ("void const*, unsigned long"), so the call form has to be
# anchored rather than \S+.
_ANNOT_RE = re.compile(r"(\s\{=[^}]*\}$|\s\{#0x[0-9a-f]+\}$|(?<=CALL)\s=.+$)")
_SIM_RE = re.compile(r"([0-9.]+)% LCS")


def strip_annot(line):
    """Line with any --resolve-operands annotation removed."""
    return _ANNOT_RE.sub(lambda m: " <SYM>" if m.group(0).lstrip().startswith("=")
                         and "{" not in m.group(0) else "", line).rstrip()


def annot_of(line):
    m = _ANNOT_RE.search(line)
    return m.group(0).strip() if m else ""


def load(path):
    blob = json.loads(pathlib.Path(path).read_text())
    return dict((s["mangled"], s) for s in blob["symbols"])


def classify_symbol(row_on):
    """Split the ON-diff into flag-introduced lines and pre-existing ones."""
    bin_only = [l[2:] for l in row_on.get("diff", []) if l.startswith("- ")]
    port_only = [l[2:] for l in row_on.get("diff", []) if l.startswith("+ ")]
    b_by_key = collections.defaultdict(list)
    for l in bin_only:
        b_by_key[strip_annot(l)].append(annot_of(l))
    p_by_key = collections.defaultdict(list)
    for l in port_only:
        p_by_key[strip_annot(l)].append(annot_of(l))

    findings = []
    for key in set(b_by_key) & set(p_by_key):
        ba, pa = b_by_key[key], p_by_key[key]
        for b, p in zip(sorted(ba), sorted(pa)):
            if b == p:
                continue        # same annotation: the line diverges for another reason
            if not b or not p:
                kind = "ASYMMETRIC"
            elif b.startswith("{#") or p.startswith("{#"):
                kind = ("CONFLICT-VALUE" if b.startswith("{#") and p.startswith("{#")
                        else "ASYMMETRIC")
            else:
                kind = "CONFLICT-NAME"
            findings.append({"kind": kind, "insn": key, "binary": b, "port": p})
    return findings


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("off_json")
    ap.add_argument("on_json")
    ap.add_argument("--top", type=int, default=15)
    args = ap.parse_args()

    off, on = load(args.off_json), load(args.on_json)
    shared = sorted(set(off) & set(on))

    rows = []
    totals = collections.Counter()
    verdict_shift = collections.Counter()
    for name in shared:
        o, n = off[name], on[name]
        so = _SIM_RE.search(o.get("reason") or "")
        sn = _SIM_RE.search(n.get("reason") or "")
        sim_off = float(so.group(1)) if so else None
        sim_on = float(sn.group(1)) if sn else None
        findings = classify_symbol(n)
        kinds = collections.Counter(f["kind"] for f in findings)
        totals.update(kinds)
        if o["verdict"] != n["verdict"]:
            verdict_shift[(o["verdict"], n["verdict"])] += 1
        if findings:
            rows.append({
                "mangled": name, "addr": n.get("addr"),
                "verdict_off": o["verdict"], "verdict_on": n["verdict"],
                "sim_off": sim_off, "sim_on": sim_on,
                "max_score": n.get("max_score"),
                "counts": dict(kinds), "findings": findings,
            })

    # A symbol whose ONLY flag-introduced findings are CONFLICT-* and that was
    # MATCH/COSMETIC before is a NEW discovery -- previously invisible.
    new_finds = [r for r in rows
                 if r["verdict_off"] in ("MATCH", "COSMETIC")
                 and (r["counts"].get("CONFLICT-NAME", 0)
                      or r["counts"].get("CONFLICT-VALUE", 0))]

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    out = OUT_DIR / "eval.json"
    out.write_text(json.dumps({
        "symbols_compared": len(shared),
        "totals": dict(totals),
        "verdict_shift": dict(("%s->%s" % k, v) for k, v in verdict_shift.items()),
        "new_findings_in_previously_clean": [r["mangled"] for r in new_finds],
        "rows": rows,
    }, indent=2))

    n_sig = totals["CONFLICT-NAME"] + totals["CONFLICT-VALUE"]
    n_noise = totals["ASYMMETRIC"]
    n_all = n_sig + n_noise
    print("resolve-eval: %d symbols compared" % len(shared))
    print("  CONFLICT-NAME   %5d  (signal: both sides named, names differ)"
          % totals["CONFLICT-NAME"])
    print("  CONFLICT-VALUE  %5d  (signal: both sides literal, values differ)"
          % totals["CONFLICT-VALUE"])
    print("  ASYMMETRIC      %5d  (noise: resolved on one side only)" % n_noise)
    if n_all:
        print("  asymmetric rate  %.1f%% of flag-introduced lines"
              % (100.0 * n_noise / n_all))
    print("  symbols with any flag-introduced line: %d" % len(rows))
    print("  NEW findings inside previously MATCH/COSMETIC symbols: %d"
          % len(new_finds))
    print()
    print("verdict shifts (raw, triage disabled):")
    for (a, b), c in sorted(verdict_shift.items(), key=lambda kv: -kv[1]):
        print("  %-12s -> %-12s %4d" % (a, b, c))
    print()
    print("top symbols by signal count:")
    rows.sort(key=lambda r: -(r["counts"].get("CONFLICT-NAME", 0)
                              + r["counts"].get("CONFLICT-VALUE", 0)))
    for r in rows[:args.top]:
        print("  %-4s %-4s %-58s %s"
              % (r["counts"].get("CONFLICT-NAME", 0),
                 r["counts"].get("CONFLICT-VALUE", 0),
                 r["mangled"][:58], r["addr"]))
    print()
    print("JSON: %s" % out)


if __name__ == "__main__":
    main()
