#!/usr/bin/env python3
"""Mode-matched BinDiff merge.

The binary picks ARM vs Thumb per function (GCC 4.4.1 has no per-function mode,
so the cross-build compiles whole-file ARM or whole-file Thumb). To compare each
port function in the binary's ACTUAL mode, we build two twins (-marm and -mthumb),
BinDiff each against the binary, then for every function take the score from the
twin whose mode matches the binary's $a/$t mapping symbol at that address.

A low score in the merged view can no longer be blamed on a Thumb-vs-ARM encoding
mismatch, so it's a trustworthy real-divergence candidate. (Caveat: pure struct
OFFSET bugs are invisible to BinDiff -- it matches structure, not immediates.)

Usage:
  mode-match-merge.py --binary <FruitNinja.exe> \
      --arm-bindiff <binary_vs_arm.BinDiff> --thumb-bindiff <binary_vs_thumb.BinDiff> \
      [--out <ranked.csv>] [--min-conf 0.95] [--max-sim 0.75] [--min-ins 10]
"""
import argparse, bisect, sqlite3, sys, csv

try:
    import lief
except ImportError:
    sys.exit("error: pip install lief  (needed to read $a/$t mapping symbols)")


def binary_mode_fn(elf_path):
    """Return mode(addr) -> 'ARM'|'THUMB' from $a/$t mapping symbols."""
    b = lief.parse(elf_path)
    maps = sorted((s.value, s.name) for s in b.symbols if s.name in ("$a", "$t"))
    addrs = [m[0] for m in maps]
    names = [m[1] for m in maps]

    def mode(addr):
        i = bisect.bisect_right(addrs, addr) - 1
        return "THUMB" if (i >= 0 and names[i] == "$t") else "ARM"  # default ARM
    return mode


def load_bindiff(path):
    c = sqlite3.connect(path).cursor()
    c.execute("SELECT address1,name1,name2,similarity,confidence,instructions FROM function")
    return {a: (n1, n2, s, cf, ins) for a, n1, n2, s, cf, ins in c.fetchall()}


def dist(d):
    sims = [v[2] for v in d.values()]
    n = len(sims)
    avg = sum(sims) / n if n else 0.0
    return n, avg, sum(1 for s in sims if s >= 0.99), sum(1 for s in sims if s >= 0.90), \
        sum(1 for v in d.values() if v[3] >= 0.9 and v[2] < 0.9)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required=True)
    ap.add_argument("--arm-bindiff", required=True)
    ap.add_argument("--thumb-bindiff", required=True)
    ap.add_argument("--out", default=None, help="ranked divergence CSV")
    ap.add_argument("--min-conf", type=float, default=0.95)
    ap.add_argument("--max-sim", type=float, default=0.75)
    ap.add_argument("--min-ins", type=int, default=10)
    a = ap.parse_args()

    mode = binary_mode_fn(a.binary)
    arm = load_bindiff(a.arm_bindiff)
    thumb = load_bindiff(a.thumb_bindiff)

    merged = {}
    n_thumb = 0
    for addr in set(arm) | set(thumb):
        want_thumb = mode(addr) == "THUMB"
        pref = thumb if want_thumb else arm
        other = arm if want_thumb else thumb
        rec = pref.get(addr) or other.get(addr)
        if rec is None:
            continue
        if want_thumb and addr in thumb:
            n_thumb += 1
        merged[addr] = rec

    print("%-22s %8s %8s %8s %8s %8s" % ("view", "matched", "avg_sim", ">=0.99", ">=0.90", "conf-div"))
    for tag, d in (("-mthumb twin", thumb), ("-marm twin", arm), ("MODE-MATCHED", merged)):
        n, avg, ident, g90, div = dist(d)
        print("%-22s %8d %8.3f %8d %8d %8d" % (tag, n, avg, ident, g90, div))
    print("\n%d functions taken from the Thumb twin (binary is Thumb there)" % n_thumb)

    rows = []
    for addr, (n1, n2, s, cf, ins) in merged.items():
        if cf >= a.min_conf and s < a.max_sim and ins >= a.min_ins:
            rows.append((s, cf, ins, mode(addr), n2 or n1 or "?", addr))
    rows.sort()
    print("\n=== mode-matched confident divergences "
          "(conf>=%.2f, sim<%.2f, ins>=%d): %d ===" % (a.min_conf, a.max_sim, a.min_ins, len(rows)))
    print("  %-34s %5s %5s %4s %-6s %s" % ("fn", "sim", "conf", "ins", "mode", "bin_addr"))
    for s, cf, ins, md, nm, addr in rows[:40]:
        print("  %-34s %.2f  %.2f %4d %-6s %08x" % (nm[:34], s, cf, ins, md, addr))

    if a.out:
        with open(a.out, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["similarity", "confidence", "instructions", "binary_mode", "name", "binary_addr"])
            for s, cf, ins, md, nm, addr in rows:
                w.writerow([f"{s:.4f}", f"{cf:.4f}", ins, md, nm, f"{addr:08x}"])
        print("\nwrote ranked divergences -> %s (%d rows)" % (a.out, len(rows)))


if __name__ == "__main__":
    main()
