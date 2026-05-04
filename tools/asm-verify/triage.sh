#!/usr/bin/env bash
# Scope an asm-verify report to a class / symbol / filter, then prepare
# a slice that asm-triager can chew on without choking on 700+ rows.
#
#   bash tools/asm-verify/triage.sh --class WaveManager
#   bash tools/asm-verify/triage.sh --symbol GetNextWave
#   bash tools/asm-verify/triage.sh --filter '_ZN11WaveManager*'
#
# What it does:
#   1. Reads tmp/asm-verify/report.md (must already exist; run.sh first).
#   2. Filters the per-symbol verdict table + per-symbol escalation
#      sections to rows whose mangled name matches the filter.
#   3. Writes the slice to tmp/asm-verify/report.scoped.md.
#   4. Prints the scoped scoreboard + a ready-to-use asm-triager prompt
#      that points at the scoped file.
#
# Why: the all-symbols triage pass keeps choking on 700+ untriaged rows.
# Scoping to one class at a time gives the triager a tractable slice
# (typically 10-50 rows) and lets you iterate one subsystem to clean.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
REPORT_MD="$PROJECT_ROOT/tmp/asm-verify/report.md"
SCOPED_MD="$PROJECT_ROOT/tmp/asm-verify/report.scoped.md"

if [[ ! -f "$REPORT_MD" ]]; then
    echo "ERROR: $REPORT_MD missing. Run tools/asm-verify/run.sh first." >&2
    exit 1
fi

FILTER=""
LABEL=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --filter)  FILTER="$2"; LABEL="filter '$2'"; shift 2 ;;
        --class)   FILTER="*${2}*"; LABEL="class '$2'"; shift 2 ;;
        --symbol)  FILTER="*${2}*"; LABEL="symbol '$2'"; shift 2 ;;
        --help|-h)
            cat <<USAGE
Usage: bash tools/asm-verify/triage.sh [options]

Options:
  --filter <glob>    Match mangled symbol names against the glob.
                     Example: --filter '_ZN11WaveManager*'
  --class <Foo>      Shortcut for --filter '*Foo*' (loose substring).
                     Example: --class WaveManager
  --symbol <name>    Loose substring match on a function name.
                     Example: --symbol GetNextWave

Reads tmp/asm-verify/report.md (must exist; run tools/asm-verify/run.sh
first), writes the matching slice to tmp/asm-verify/report.scoped.md,
and prints a ready-to-use asm-triager dispatch prompt.
USAGE
            exit 0
            ;;
        *) echo "Unknown arg: $1 (try --help)" >&2; exit 2 ;;
    esac
done

if [[ -z "$FILTER" ]]; then
    echo "ERROR: must specify --class / --symbol / --filter (try --help)" >&2
    exit 2
fi

# Scope via Python (glob matching is annoying in pure bash).
PY="${PYTHON:-python}"
if ! command -v "$PY" > /dev/null; then
    echo "ERROR: python not on PATH." >&2
    exit 1
fi

REPORT_MD_PYPATH="$REPORT_MD"
SCOPED_MD_PYPATH="$SCOPED_MD"
if command -v cygpath > /dev/null; then
    REPORT_MD_PYPATH="$(cygpath -w "$REPORT_MD")"
    SCOPED_MD_PYPATH="$(cygpath -w "$SCOPED_MD")"
fi

"$PY" - "$REPORT_MD_PYPATH" "$SCOPED_MD_PYPATH" "$FILTER" "$LABEL" <<'PY'
import fnmatch
import re
import sys

report_path, scoped_path, glob, label = sys.argv[1:5]
text = open(report_path, "r", encoding="utf-8", errors="ignore").read()

# Per-symbol escalation blocks start with `### \`<mangled>\` @ \`<addr>\``.
# Verdict lines in the per-symbol table use `| <verdict> | \`<mangled>\` | ...`.
# Filter both sections to rows where <mangled> matches the glob.

verdict_re = re.compile(r"^\|\s*(MATCH|COSMETIC|ACCEPT-\w+|SUSPICIOUS|DIVERGE)\s*\|\s*`([^`]+)`")
section_re = re.compile(r"^### `([^`]+)` @", re.MULTILINE)

# Collect verdict-table lines.
matched_verdicts = []
for line in text.splitlines():
    m = verdict_re.match(line)
    if m and fnmatch.fnmatchcase(m.group(2), glob):
        matched_verdicts.append((m.group(1), m.group(2), line))

# Slice escalation sections by `### ...` boundaries.
section_starts = [(m.start(), m.group(1)) for m in section_re.finditer(text)]
section_starts.append((len(text), None))
matched_sections = []
for i, (start, mangled) in enumerate(section_starts[:-1]):
    if mangled and fnmatch.fnmatchcase(mangled, glob):
        end = section_starts[i + 1][0]
        matched_sections.append(text[start:end])

# Per-verdict count for the slice.
counts = {}
for v, _, _ in matched_verdicts:
    counts[v] = counts.get(v, 0) + 1

with open(scoped_path, "w", encoding="utf-8") as f:
    f.write("# asm-verify scoped report\n\n")
    f.write(f"Scope: {label}  (glob `{glob}`)\n\n")
    if not matched_verdicts:
        f.write("**No matches.**\n")
    else:
        f.write(f"## Verdict tally ({len(matched_verdicts)} symbols)\n\n")
        for v in ("MATCH", "COSMETIC", "ACCEPT-cosmetic", "ACCEPT-deferred",
                  "ACCEPT-defunct", "SUSPICIOUS", "DIVERGE"):
            n = counts.get(v, 0)
            if n:
                f.write(f"- {v}: {n}\n")
        f.write("\n## Per-symbol verdicts\n\n")
        f.write("| Verdict | Symbol | (further columns omitted in slice) |\n")
        f.write("|---|---|---|\n")
        for _, _, line in matched_verdicts:
            f.write(line + "\n")
        if matched_sections:
            f.write("\n## Escalations (need triage)\n\n")
            for sec in matched_sections:
                f.write(sec)
                if not sec.endswith("\n"):
                    f.write("\n")

print(f"\nScoped slice: {scoped_path}")
print(f"Matched: {len(matched_verdicts)} symbols  ({len(matched_sections)} escalations)\n")
print("Tally:")
for v in ("MATCH", "COSMETIC", "ACCEPT-cosmetic", "ACCEPT-deferred",
          "ACCEPT-defunct", "SUSPICIOUS", "DIVERGE"):
    n = counts.get(v, 0)
    if n:
        print(f"  {v:18s} {n}")

print("\n--- Ready-to-paste asm-triager prompt ---")
print(f"""
Triage the asm-verify slice at `tmp/asm-verify/report.scoped.md` (scope:
{label}). Read the existing `tools/asm-verify/triage.json` for prior
verdicts on these symbols; refresh stale scores and add new entries.

For each row, classify as ACCEPT-cosmetic / ACCEPT-deferred /
ACCEPT-defunct / FIX-NEEDED. Update triage.json with verdict + reason +
score + max_score + decided_at = today.

Default to ACCEPT-deferred when the divergence is plausibly Tier-2,
container-choice (std::list/map/vector vs binary intrusive), or
Defunct-stubbed; reserve FIX-NEEDED for real semantic bugs.

Read-only on src/. Output: refreshed triage.json + a short summary of
verdicts + any FIX-NEEDED finds.
""")
PY
