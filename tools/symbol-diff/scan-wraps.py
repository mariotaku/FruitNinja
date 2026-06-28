"""Scan src/ for #ifndef __bada__ whole-function wraps.

Identifies `#ifndef __bada__` / `#if !defined(__bada__)` blocks in all
src/**/*.cpp files, classifies them as whole-function wraps (the guard
encloses an entire function body) vs. partial blocks, and ranks them by
line count.

Primary input for the task-#192 un-wrap plan: whole-function wraps are
candidates for removal once the cross-build unblock lands.

Usage:
    python tools/symbol-diff/scan-wraps.py [--project <root>]

    --project  Path to project root (default: three dirs above this script).

Writes: <project>/tmp/symbol-diff/wraps.json
Prints: whole-function wraps ranked by LOC + large partial blocks (>=15 L).
"""
import argparse
import json
import pathlib
import re

parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
parser.add_argument("--project", default=None,
                    help="Project root (default: inferred from script location)")
args = parser.parse_args()

root    = pathlib.Path(args.project).resolve() if args.project else pathlib.Path(__file__).resolve().parent.parent.parent
src     = root / "src"
out_dir = root / "tmp" / "symbol-diff"
out_dir.mkdir(parents=True, exist_ok=True)

open_re  = re.compile(r"^\s*#\s*if\s*!\s*defined\(__bada__\)|^\s*#\s*ifndef\s+__bada__")
any_if   = re.compile(r"^\s*#\s*if")
endif    = re.compile(r"^\s*#\s*endif")
els      = re.compile(r"^\s*#\s*else")
func_sig = re.compile(r"^\s*[A-Za-z_].*\b([A-Za-z_][A-Za-z0-9_]*)::([~A-Za-z_][A-Za-z0-9_]*)\s*\(")

results = []
for f in sorted(src.rglob("*.cpp")):
    lines = f.read_text(encoding="utf-8", errors="replace").splitlines()
    func_starts = []
    depth   = 0
    pending = None
    for i, l in enumerate(lines):
        m = func_sig.match(l)
        if m and depth == 0:
            pending = (i + 1, f"{m.group(1)}::{m.group(2)}")
        depth += l.count("{") - l.count("}")
        if pending and "{" in l:
            func_starts.append(pending)
            pending = None

    def func_for(lineno):
        best = None
        for ln, name in func_starts:
            if ln <= lineno:
                best = (ln, name)
            else:
                break
        return best

    i = 0
    n = len(lines)
    while i < n:
        if open_re.match(lines[i]):
            start = i
            d = 1
            has_else = False
            j = i + 1
            while j < n and d > 0:
                if any_if.match(lines[j]):
                    d += 1
                elif endif.match(lines[j]):
                    d -= 1
                elif els.match(lines[j]) and d == 1:
                    has_else = True
                j += 1
            end = j - 1
            span = end - start - 1
            fc = func_for(start + 1)
            wf = False
            if fc:
                fl, fn = fc
                ob = fl
                for k in range(fl - 1, min(fl + 8, n)):
                    if "{" in lines[k]:
                        ob = k + 1
                        break
                if start + 1 - ob <= 2:
                    for k in range(end, min(end + 3, n)):
                        if lines[k].strip().startswith("}"):
                            wf = True
                            break
            results.append({
                "file": f.relative_to(root).as_posix(),
                "start": start + 1,
                "end": end + 1,
                "lines": span,
                "has_else": has_else,
                "func": fc[1] if fc else None,
                "whole_func": wf,
            })
            i = j
        else:
            i += 1

results.sort(key=lambda r: -r["lines"])
out_file = out_dir / "wraps.json"
json.dump(results, open(out_file, "w"), indent=2)
print(f"Total #ifndef __bada__ blocks: {len(results)}")
wf = [r for r in results if r["whole_func"]]
print(f"Whole-function wraps: {len(wf)}")
print()
print("=== WHOLE-FUNCTION WRAPS (ranked by LOC) ===")
for r in wf:
    print(f"{r['lines']:4d}L  {r['file']}:{r['start']}-{r['end']}  {r['func']}  else={r['has_else']}")
print()
print("=== LARGEST NON-whole blocks (>=15L) ===")
for r in results:
    if not r["whole_func"] and r["lines"] >= 15:
        print(f"{r['lines']:4d}L  {r['file']}:{r['start']}-{r['end']}  {r['func']}  else={r['has_else']}")
print(f"\nWrote {out_file}")
