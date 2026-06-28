"""Readable summary renderer for the un-wrap plan JSON (task #192).

Reads an unwrap-plan.json file (produced by the x64-audit classify+build_plan
pipeline) and prints a formatted, ranked summary of CLEAN batches, NEEDS-*
items, LEAVE decisions, DEFUNCT stubs, and the shared-file blast radius.

Usage:
    python tools/symbol-diff/summarize-unwrap-plan.py [<plan-file>]

    <plan-file>  Path to unwrap-plan.json
                 (default: tmp/x64-audit/unwrap-plan.json relative to cwd)
"""
import json
import sys
from collections import Counter, defaultdict

plan_file = sys.argv[1] if len(sys.argv) > 1 else "tmp/x64-audit/unwrap-plan.json"
d = json.load(open(plan_file))
E = d["entries"]

ORDER = [
    "batch1-namespace-FN",
    "batch2-Bomb-statics",
    "batch3-Fruit-FruitFact-PauseScreen",
    "batch4-namespace-Mortar",
    "batch5-input-callbacks",
    "batch-free-other",
    "hidden-mismatch",
]

byfam = defaultdict(list)
for e in E:
    byfam[e["family"]].append(e)

CLEAN_SET = {"CLEAN", "CLEAN-STUB"}

print("=" * 78)
print("UN-WRAP PLAN SUMMARY (task #192) -- CLEAN batches first")
print("=" * 78)
for fam in ORDER:
    es = byfam.get(fam, [])
    cleans = [e for e in es if e["classification"] in CLEAN_SET]
    files = set()
    for e in cleans:
        for f in e["callSiteFiles"]:
            files.add(f)
        if e["defCpp"] not in ("(grep)", "-", "(none)", "(inline)"):
            files.add(e["defCpp"].split(":")[0])
    print("\n### %s -- CLEAN: %d / %d" % (fam, len(cleans), len(es)))
    for e in cleans:
        tag = "" if e["classification"] == "CLEAN" else " [STUB-keep-noop]"
        print("    + %-42s %s%s" % (e["demangled"], e["binarySymbol"], tag))
    if files:
        print("    files touched (union):")
        for f in sorted(files):
            print("        ", f)

print("\n" + "=" * 78)
print("NEEDS-REFACTOR / NEEDS-VERIFY (un-wrap + sig/type/linkage change)")
print("=" * 78)
for e in E:
    if e["classification"] in ("NEEDS-REFACTOR", "NEEDS-VERIFY"):
        print("  [%s] %-42s (%s)" % (e["classification"], e["demangled"], e["family"]))
        print("        ", e["note"])

print("\n" + "=" * 78)
print("LEAVE (false collision / legit-OO / platform -- do NOT un-wrap)")
print("=" * 78)
for e in E:
    if e["classification"] == "LEAVE":
        print("  - %-42s %s" % (e["demangled"], e["note"][:90]))

print("\n" + "=" * 78)
print("DEFUNCT -> STUB (port as no-op with exact binary sig, per stub-don-skip)")
print("=" * 78)
for e in E:
    if e["classification"] in ("STUB", "CLEAN-STUB"):
        print("  * %-42s [%s] %s" % (e["demangled"], e["classification"], e["family"]))

# Blast radius: files shared across batches
print("\n" + "=" * 78)
print("SHARED-FILE BLAST RADIUS (files touched by >1 batch's CLEAN set)")
print("=" * 78)
file2fam = defaultdict(set)
for fam in ORDER:
    for e in byfam.get(fam, []):
        if e["classification"] in CLEAN_SET:
            for f in e["callSiteFiles"]:
                file2fam[f].add(fam)
            if e["defCpp"] not in ("(grep)", "-", "(none)", "(inline)"):
                file2fam[e["defCpp"].split(":")[0]].add(fam)
for f, fams in sorted(file2fam.items()):
    if len(fams) > 1 and not f.startswith("("):
        print("  %-40s <- %s" % (f, ", ".join(sorted(fams))))

c = Counter(e["classification"] for e in E)
print("\nTOTALS:", dict(c))
