#!/usr/bin/env python3
"""Inheritance-aware binary-vs-port class-size REFERENCE.

Joins binary ground-truth sizes (infer-class-sizes.py) with the port's declared
sizes (`static_assert(sizeof(X)==N)`) and inheritance (`class X : public Y`),
then flags mismatches. It is a REVIEWED reference, NOT an auto-gate: a human reads
it and places asserts, because (a) operator-new sizes have edge cases (multi-shape
allocs, non-heap classes) and (b) inheritance means the right FIX location is the
BASE, not each derived class.

Inheritance handling: operator new gives the TOTAL most-derived size. So when a
base class size is wrong, every derived class inherits the same delta -- this tool
attributes the root cause to the base and tags the derived rows as "cascade".

Usage:
  layout-reference.py --sizes tmp/binary-class-sizes.json --src src \
      [--out tmp/asm-verify/layout-reference.md]
"""
import argparse, glob, json, os, re


def parse_port(src_dirs):
    """Returns (declared_size{leaf:int}, base{leaf:leaf}, where{leaf:file})."""
    declared, base, where = {}, {}, {}
    sa = re.compile(r"static_assert\s*\(\s*sizeof\s*\(\s*([\w:]+)\s*\)\s*==\s*"
                    r"(0[xX][0-9a-fA-F]+|\d+)")
    inh = re.compile(r"\b(?:class|struct)\s+(\w+)\s*:\s*"
                     r"(?:public\s+|private\s+|protected\s+)?([\w:]+)")
    for d in src_dirs:
        for f in glob.glob(os.path.join(d, "**", "*.h"), recursive=True) + \
                 glob.glob(os.path.join(d, "**", "*.cpp"), recursive=True):
            try:
                txt = open(f, encoding="utf-8", errors="ignore").read()
            except OSError:
                continue
            for m in sa.finditer(txt):
                leaf = m.group(1).split("::")[-1]
                declared[leaf] = int(m.group(2), 0)
                where.setdefault(leaf, os.path.relpath(f))
            for m in inh.finditer(txt):
                child, parent = m.group(1), m.group(2).split("::")[-1]
                base[child] = parent
                where.setdefault(child, os.path.relpath(f))
    return declared, base, where


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sizes", default="tmp/binary-class-sizes.json")
    ap.add_argument("--src", default="src")
    ap.add_argument("--out", default="tmp/asm-verify/layout-reference.md")
    a = ap.parse_args()

    bsizes_q = json.load(open(a.sizes))                 # qualified -> {...}
    bsize = {}                                          # leaf -> (size, qualified)
    lowconf = {}                                        # leaf -> sizes_seen (if >1)
    for q, v in bsizes_q.items():
        leaf = q.split("::")[-1]
        bsize[leaf] = (v["size"], q)
        if len(v.get("sizes_seen", [])) > 1:
            lowconf[leaf] = v["sizes_seen"]
    declared, base, where = parse_port([a.src])

    rows = []  # (category, leaf, qualified, binsize, portsize, note)
    for leaf, (bs, q) in sorted(bsize.items()):
        ps = declared.get(leaf)
        # is a base of this class mismatched? -> cascade
        casc = None
        b = base.get(leaf)
        while b:
            if b in bsize and b in declared and declared[b] != bsize[b][0]:
                casc = b
                break
            b = base.get(b)
        if ps is None:
            cat, note = "NO-ASSERT", ("base %s" % base[leaf]) if leaf in base else "(no port size_assert)"
        elif ps == bs:
            cat, note = "MATCH", ""
        else:
            cat = "MISMATCH"
            note = "delta %+d" % (bs - ps)
            if casc:
                note += "  <- CASCADE from base %s (fix the base)" % casc
            if leaf in lowconf:
                note += "  [LOW-CONF: binary sizes seen %s]" % [hex(x) for x in lowconf[leaf]]
        rows.append((cat, leaf, q, bs, ps, note))

    order = {"MISMATCH": 0, "NO-ASSERT": 1, "MATCH": 2}
    rows.sort(key=lambda r: (order.get(r[0], 9), r[1]))
    counts = {}
    for r in rows:
        counts[r[0]] = counts.get(r[0], 0) + 1

    lines = ["# Binary-vs-port class-size reference",
             "",
             "Binary sizes = `operator new` ground truth. Port sizes = "
             "`static_assert(sizeof(X)==N)`. REVIEW before acting; place asserts on the "
             "BASE class for cascades.", "",
             "counts: " + ", ".join("%s=%d" % (k, counts[k]) for k in sorted(counts)), "",
             "| status | class | binary | port assert | note |",
             "|--------|-------|--------|-------------|------|"]
    for cat, leaf, q, bs, ps, note in rows:
        ps_s = ("0x%x" % ps) if ps is not None else "-"
        lines.append("| %s | %s | 0x%x | %s | %s |"
                     % (cat, q, bs, ps_s, note))
    report = "\n".join(lines) + "\n"

    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    open(a.out, "w").write(report)

    # console summary: lead with the actionable mismatches
    print("counts:", ", ".join("%s=%d" % (k, counts[k]) for k in sorted(counts)))
    mism = [r for r in rows if r[0] == "MISMATCH"]
    print("\n=== MISMATCH (binary size != port assert) -- potential layout bugs ===")
    if not mism:
        print("  (none)")
    for cat, leaf, q, bs, ps, note in mism:
        print("  %-30s binary 0x%-5x port 0x%-5x  %s" % (q[:30], bs, ps, note))
    print("\nfull reference -> %s" % a.out)


if __name__ == "__main__":
    main()
