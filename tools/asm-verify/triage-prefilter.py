#!/usr/bin/env python3
"""Triage pre-filter for the mode-matched BinDiff divergence list.

Turns the raw ranked CSV (mode-match-merge.py --out) into a SHORT "investigate"
list, so an asm-inspector only spends cycles on genuinely-novel real-divergence
candidates instead of re-judging the cosmetic cluster every run.

Two deterministic filters (no LLM):
  1. sim-band: the small-function PIC/GOT noise floor sits at ~0.62-0.72. Rows in
     that band (and not unusually large) are tagged PIC-FLOOR (low signal). Rows
     BELOW the floor (sim < --floor) -- where the real bugs lived (SetDefaults
     0.56, ~SlashModInfo 0.41) -- or unusually large functions at low sim are
     tagged INVESTIGATE.
  2. triage hint: best-effort match of each row's leaf name against triage.json
     (asm-verify's sticky verdicts, keyed by mangled name). A match attaches the
     existing verdict/reason as a HINT (leaf names can collide, so it's advisory,
     not a hard subtract) -- if asm-verify already ACCEPTed it cosmetic, you can
     skip it.

Usage:
  triage-prefilter.py --csv tmp/bindiff-out/mode-matched-divergences.csv \
      [--triage tools/asm-verify/triage.json] [--floor 0.60] [--big-ins 30] \
      [--out tmp/bindiff-out/investigate-candidates.csv]
"""
import argparse, csv, json, os, re


def leaf_of(mangled):
    """Crude Itanium-mangling leaf extractor (enough for hint matching)."""
    m = mangled
    if not m.startswith("_Z"):
        return m
    m = m[2:]
    if m.startswith(("N", "K")):
        m = m.lstrip("NK")
    comps, i = [], 0
    while i < len(m):
        j = i
        while j < len(m) and m[j].isdigit():
            j += 1
        if j == i:
            break
        n = int(m[i:j])
        comps.append(m[j:j + n])
        i = j + n
    if not comps:
        return mangled
    rest = m[i:]
    if re.match(r"D[0-2]", rest):          # destructor
        return "~" + comps[-1]
    if re.match(r"C[1-3]", rest):          # constructor -> class leaf (matches BinDiff)
        return comps[-1]
    return comps[-1]


def load_triage(path):
    """leaf name -> list of (key, verdict, reason)."""
    if not path or not os.path.exists(path):
        return {}
    d = json.load(open(path))
    by_leaf = {}
    for k, v in d.items():
        by_leaf.setdefault(leaf_of(k), []).append(
            (k, v.get("verdict", "?"), (v.get("reason", "") or "")[:80]))
    return by_leaf


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", default="tmp/bindiff-out/mode-matched-divergences.csv")
    ap.add_argument("--triage", default="tools/asm-verify/triage.json")
    ap.add_argument("--floor", type=float, default=0.60,
                    help="sim below this = INVESTIGATE (sub-PIC-floor outlier)")
    ap.add_argument("--big-ins", type=int, default=30,
                    help="ins >= this at low sim = INVESTIGATE regardless of band")
    ap.add_argument("--out", default="tmp/bindiff-out/investigate-candidates.csv")
    a = ap.parse_args()

    by_leaf = load_triage(a.triage)
    rows = list(csv.DictReader(open(a.csv)))

    novel, triaged, pic_floor = [], [], 0
    for r in rows:
        sim = float(r["similarity"]); ins = int(r["instructions"]); name = r["name"]
        if not ((sim < a.floor) or (ins >= a.big_ins)):
            pic_floor += 1
            continue
        hint = by_leaf.get(name) or by_leaf.get(name.lstrip("~"))
        verdicts = sorted({v for _, v, _ in hint}) if hint else []
        row = (sim, ins, r["binary_mode"], name, r["binary_addr"], hint, verdicts)
        # subtract rows already ACCEPTed by asm-verify; keep FIX-NEEDED / unmatched.
        if hint and all(v.startswith("ACCEPT") for v in verdicts):
            triaged.append(row)
        else:
            novel.append(row)
    novel.sort(); triaged.sort()

    print("total CSV divergences  : %d" % len(rows))
    print("PIC-floor (low signal) : %d   (sim in [%.2f,0.75), ins<%d)" % (pic_floor, a.floor, a.big_ins))
    print("already-triaged ACCEPT : %d   (subtracted -- asm-verify sticky verdict)" % len(triaged))
    print("NOVEL / open           : %d   <-- the asm-inspector backlog\n" % len(novel))

    def show(rows_):
        print("  %-26s %5s %4s %-5s %-9s %s" % ("fn", "sim", "ins", "mode", "bin_addr", "triage hint"))
        for sim, ins, md, name, addr, hint, verdicts in rows_:
            h = "[%s] %s" % (",".join(verdicts), hint[0][2]) if hint else ""
            print("  %-26s %.2f %4d %-5s %-9s %s" % (name[:26], sim, ins, md, addr, h))

    print("=== NOVEL / open ===")
    show(novel)
    if triaged:
        print("\n=== already-triaged ACCEPT (subtracted; audit for bad leaf-name matches) ===")
        show(triaged)

    if a.out:
        with open(a.out, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["similarity", "instructions", "binary_mode", "name", "binary_addr"])
            for sim, ins, md, name, addr, hint, verdicts in novel:
                w.writerow([f"{sim:.4f}", ins, md, name, addr])
        print("\nwrote %d NOVEL candidates -> %s" % (len(novel), a.out))


if __name__ == "__main__":
    main()
