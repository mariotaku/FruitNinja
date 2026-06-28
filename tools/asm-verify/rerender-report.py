"""Re-render report.md from existing report.json + (possibly updated) triage.json.

Used after editing triage.json manually to regenerate report.md without
re-running the full asm-verify Docker pipeline.

Usage:
    python tools/asm-verify/rerender-report.py [--project <root>]

    --project  Path to project root (default: two dirs above this script,
               i.e. the fruit-ninja repo root).

Reads:  <project>/tmp/asm-verify/report.json
        <project>/tools/asm-verify/triage.json
Writes: <project>/tmp/asm-verify/report.md (and updates report.json verdicts in-place)
"""
import argparse
import json
from collections import Counter
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
parser.add_argument("--project", default=None,
                    help="Project root (default: inferred from script location)")
args = parser.parse_args()

PROJECT = Path(args.project).resolve() if args.project else Path(__file__).resolve().parent.parent.parent
REPORT_JSON = PROJECT / "tmp" / "asm-verify" / "report.json"
REPORT_MD   = PROJECT / "tmp" / "asm-verify" / "report.md"
TRIAGE_JSON = PROJECT / "tools" / "asm-verify" / "triage.json"

with open(REPORT_JSON, encoding="utf-8") as f:
    payload = json.load(f)
results = payload["symbols"]
with open(TRIAGE_JSON, encoding="utf-8") as f:
    triage = json.load(f)


def apply_triage(rs, tr):
    for r in rs:
        name = r["mangled"]
        entry = tr.get(name)
        if not entry:
            continue
        if r.get("score") != entry.get("score") or r.get("max_score") != entry.get("max_score"):
            r["triage_stale"] = True
            continue
        new_v = entry.get("verdict")
        if new_v in ("ACCEPT-cosmetic", "ACCEPT-deferred", "ACCEPT-defunct", "FIX-NEEDED"):
            r["verdict"] = new_v
            r["reason"] = "triaged: " + entry.get("reason", entry.get("verdict"))
    return rs


results = apply_triage(results, triage)

lines = ["# asm-verify report", ""]
counts = Counter(r["verdict"] for r in results)
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
escalations = [r for r in results
               if r["verdict"] in ("SUSPICIOUS", "DIVERGE", "FIX-NEEDED")]
if escalations:
    lines.append("## Escalations (need triage)")
    for r in escalations:
        lines.append("")
        lines.append(f"### `{r['mangled']}` @ `{r['addr']}`")
        if r.get("triage_stale"):
            lines.append("")
            lines.append("WARN triage entry stale (score drifted since last triage)")
        lines.append("")
        lines.append("Notes: ")
        if r.get("score") is not None:
            lines.append(f"Score: {r['score']}/{r.get('max_score', '?')}")
        lines.append("")
        lines.append("```diff")
        lines.extend(r.get("diff", []))
        lines.append("```")
REPORT_MD.write_text("\n".join(lines) + "\n", encoding="utf-8")

# Update report.json to reflect re-applied verdicts.
out_json = []
for r in results:
    out_json.append({
        "mangled": r["mangled"],
        "addr": r["addr"],
        "verdict": r["verdict"],
        "reason": r["reason"],
        "score": r.get("score"),
        "max_score": r.get("max_score"),
        "diff": r.get("diff", []),
        "triage_stale": r.get("triage_stale", False),
    })
with open(REPORT_JSON, "w", encoding="utf-8") as f:
    json.dump({"symbols": out_json}, f, indent=2)

print("Verdict tally:")
for v in ("MATCH", "COSMETIC", "ACCEPT-cosmetic", "ACCEPT-deferred",
          "ACCEPT-defunct", "SUSPICIOUS", "FIX-NEEDED", "DIVERGE", "UNPAIRED"):
    if v in counts:
        print(f"  {v}: {counts[v]}")
print(f"\nReport: {REPORT_MD}")
