#!/usr/bin/env python3
"""Recover CORRECT, fully-qualified function names for BinDiff rows by ADDRESS.

binexport-cli demangles function names down to the bare leaf (no namespace, no
class, no signature), so the BinDiff `function` table is full of collisions
(`Update` x47, `Init` x35, ...). You cannot reliably identify a function by that
name. But BinDiff pairs functions STRUCTURALLY by address (address1<->address2),
and both binaries are symboled -- so we recover the real name by address:

    binary_name = binary.symtab.lookup( address1 - IMAGE_BASE )   # full mangled
    port_name   = port_so.symtab.lookup( address2 - IMAGE_BASE )

IMAGE_BASE is Ghidra's ELF image base (0x10000 for this binary -- the binexport
addresses are .symtab link-time addr + 0x10000; verified empirically). Mangled
names are demangled in bulk via the toolchain c++filt inside the fnverify-bada
image (one docker call); falls back to mangled if --image is 'none'.

Output (sorted by similarity, deduped by binary link-time addr):
  similarity,confidence,instructions,binary_mode,binary_addr,binary_name,port_addr,port_name

Usage:
  resolve-bindiff-names.py --binary tmp/FruitNinja_v1_6_1.exe \
      --port-so tmp/fnverify.arm.so \
      --dbs 'tmp/bindiff-out/binary_vs_*.BinDiff' \
      --out tmp/bindiff-out/divergences-named.csv
"""
import argparse, glob, os, sqlite3, subprocess, sys

try:
    import lief
except ImportError:
    sys.exit("error: pip install lief")

IMAGE_BASE = 0x10000


def addr2name(path):
    """addr (thumb-bit masked) -> mangled symbol name, for a symboled ELF."""
    b = lief.parse(path)
    if b is None:
        return {}
    m = {}
    for s in b.symbols:
        n = s.name or ""
        # skip ARM mapping symbols ($a/$t/$d) and empty names
        if n and s.value and not (len(n) == 2 and n[0] == "$"):
            m.setdefault(s.value & ~1, n)
    return m


def bulk_demangle(names, image):
    """mangled -> demangled via the toolchain c++filt in `image` (one docker run)."""
    uniq = sorted({n for n in names if n})
    if not uniq or image == "none":
        return {n: n for n in uniq}
    payload = "\n".join(uniq)
    try:
        # feed names on stdin, read demangled lines back in the same order
        p = subprocess.run(
            ["docker", "run", "--rm", "-i", image, "bash", "-c",
             "export PATH=/opt/codesourcery/bin:$PATH; "
             "arm-samsung-nucleuseabi-c++filt"],
            input=payload, capture_output=True, text=True, timeout=120)
        out = p.stdout.splitlines()
        if len(out) == len(uniq):
            return dict(zip(uniq, out))
        sys.stderr.write("warn: c++filt line count mismatch (%d vs %d); using mangled\n"
                         % (len(out), len(uniq)))
    except Exception as e:
        sys.stderr.write("warn: demangle failed (%s); using mangled\n" % e)
    return {n: n for n in uniq}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default="tmp/FruitNinja_v1_6_1.exe")
    ap.add_argument("--port-so", default="tmp/fnverify.arm.so")
    ap.add_argument("--dbs", default="tmp/bindiff-out/binary_vs_*.BinDiff",
                    help="glob of .BinDiff sqlite DBs")
    ap.add_argument("--image-base", default=hex(IMAGE_BASE))
    ap.add_argument("--image", default="fnverify-bada",
                    help="docker image with c++filt, or 'none' to keep mangled")
    ap.add_argument("--max-sim", type=float, default=1.01,
                    help="only emit rows with similarity <= this (default: all)")
    ap.add_argument("--out", default="tmp/bindiff-out/divergences-named.csv")
    a = ap.parse_args()
    base = int(a.image_base, 0)

    bsym = addr2name(a.binary)
    psym = addr2name(a.port_so) if os.path.exists(a.port_so) else {}
    print("binary symbols: %d   port symbols: %d" % (len(bsym), len(psym)))

    # gather rows from all DBs; keep the lowest-similarity row per binary addr
    best = {}   # link-time binary addr -> (sim, conf, ins, a1, a2)
    dbs = glob.glob(a.dbs)
    if not dbs:
        sys.exit("error: no DBs matched %s" % a.dbs)
    for db in dbs:
        try:
            c = sqlite3.connect(db)
            for a1, a2, sim, conf, ins in c.execute(
                    "select address1,address2,similarity,confidence,instructions from function"):
                la = (a1 - base) & ~1                      # binary link-time addr
                cur = best.get(la)
                if cur is None or sim < cur[0]:
                    best[la] = (sim, conf, ins, a1, a2)
            c.close()
        except Exception as e:
            sys.stderr.write("warn: %s: %s\n" % (db, e))

    # resolve names by address
    rows = []
    bresolved = presolved = 0
    wanted_names = set()
    for la, (sim, conf, ins, a1, a2) in best.items():
        bn = bsym.get(la) or bsym.get(la + 1) or ""
        pn = psym.get((a2 - base) & ~1) or psym.get(((a2 - base) & ~1) + 1) or ""
        if bn:
            bresolved += 1
        if pn:
            presolved += 1
        wanted_names.update((bn, pn))
        rows.append([sim, conf, ins, la, bn, a2, pn])

    dm = bulk_demangle(wanted_names, a.image)
    rows.sort(key=lambda r: r[0])

    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    import csv
    with open(a.out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["similarity", "confidence", "instructions",
                    "binary_addr", "binary_name", "port_addr", "port_name"])
        for sim, conf, ins, la, bn, a2, pn in rows:
            if sim > a.max_sim:
                continue
            w.writerow(["%.4f" % sim, "%.4f" % conf, ins, "0x%x" % la,
                        dm.get(bn, bn), "0x%x" % ((a2 - base) & ~1), dm.get(pn, pn)])

    print("rows: %d   binary names resolved: %d (%.0f%%)   port names resolved: %d (%.0f%%)"
          % (len(rows), bresolved, 100.0 * bresolved / max(len(rows), 1),
             presolved, 100.0 * presolved / max(len(rows), 1)))
    print("wrote -> %s" % a.out)

    # The actionable view: rows where BOTH sides resolve to the SAME function
    # (BinDiff matched correctly) but similarity < 1. Mismatched-name rows are
    # BinDiff mis-pairings (usually conf ~0.01) -- not real divergences. Static
    # init thunks ("global constructors keyed to") are excluded (init-order noise).
    def normname(n):
        return (n or "").replace(" ", "")
    same = [(sim, conf, ins, la, bn, a2, pn) for (sim, conf, ins, la, bn, a2, pn) in rows
            if bn and pn and normname(bn) == normname(pn) and sim < 0.999
            and not bn.startswith(("_GLOBAL__", "_Z41__static_initialization"))]
    same_out = a.out.replace(".csv", "-samefunc.csv")
    import csv as _csv
    with open(same_out, "w", newline="") as f:
        w = _csv.writer(f)
        w.writerow(["similarity", "confidence", "instructions", "binary_addr", "function"])
        for sim, conf, ins, la, bn, a2, pn in same:
            w.writerow(["%.4f" % sim, "%.4f" % conf, ins, "0x%x" % la, dm.get(bn, bn)])
    print("same-function divergences (sim<1, names agree): %d -> %s" % (len(same), same_out))
    print("\n=== ACTIONABLE: correctly-matched same-function divergences (lowest sim) ===")
    for sim, conf, ins, la, bn, a2, pn in same[:30]:
        print("  %.3f i%-3d 0x%-7x %s" % (sim, ins, la, dm.get(bn, bn)[:64]))


if __name__ == "__main__":
    main()
