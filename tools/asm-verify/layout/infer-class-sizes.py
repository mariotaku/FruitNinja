#!/usr/bin/env python3
"""Infer binary class sizes from operator-new call sites (ground truth).

For `new ClassX`, the compiler emits `mov r0,#SIZE; bl operator new; ...; bl
ClassX::ClassX`. SIZE is the exact allocation size -- more reliable than Ghidra's
declared struct size (per the "class-size via operator new" rule). Read straight
from the BinExport2 instruction stream (raw bytes disassembled by capstone in the
binary's actual ARM/Thumb mode).

The constructor that follows the new is identified by matching the forward call's
BinExport demangled leaf name against the set of constructor class-leaves mined
from the binary's .symtab (_ZN...C[123]E). This avoids PIC thunk -> GOT resolution
(the call target is usually a PLT veneer, not the real ctor address).

NOTE: operator new returns the TOTAL most-derived size INCLUDING base subobjects.
Inheritance is resolved separately (layout-reference.py, from the port headers),
so a base-size delta can be attributed to the base rather than each derived class.

Output JSON: { "Qualified::Class": {size, size_hex, alloc_count, sizes_seen, sample_addr} }

Usage:
  infer-class-sizes.py --binexport tmp/binary.cli.BinExport \
      --binary FruitNinjaBada/Bin/FruitNinja.exe --out tmp/binary-class-sizes.json
"""
import argparse, bisect, json, re, sys
from collections import defaultdict

sys.path.insert(0, "tmp")
try:
    import capstone
    from capstone import arm as cs_arm
    from binexport.binexport2_pb2 import BinExport2
    import lief
except ImportError as e:
    sys.exit("error: need capstone, lief, and binexport pb2 on path (%s)" % e)


def mangled_components(m):
    """_ZN6Mortar4FileC1E... -> ['Mortar','File'] (the qualified name path)."""
    m = m[3:]
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
    return comps


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binexport", default="tmp/binary.cli.BinExport")
    ap.add_argument("--binary", default="FruitNinjaBada/Bin/FruitNinja.exe")
    ap.add_argument("--out", default="tmp/binary-class-sizes.json")
    ap.add_argument("--back", type=int, default=8, help="instrs to scan back for the size")
    ap.add_argument("--fwd", type=int, default=24, help="instrs to scan forward for the ctor")
    a = ap.parse_args()

    p = BinExport2()
    p.ParseFromString(open(a.binexport, "rb").read())
    b = lief.parse(a.binary)

    addr2name = {v.address: v.mangled_name for v in p.call_graph.vertex}
    new_addrs = {v.address for v in p.call_graph.vertex if v.mangled_name == "operator.new"}

    # ctor class-leaves mined from .symtab: leaf -> qualified (note collisions)
    ctor_leaf, leaf_collide = {}, set()
    for s in b.symbols:
        n = s.name or ""
        if n.startswith("_ZN") and re.search(r"C[123]E", n):
            comps = mangled_components(n)
            if not comps:
                continue
            leaf = comps[-1]
            q = "::".join(comps)
            if leaf in ctor_leaf and ctor_leaf[leaf] != q:
                leaf_collide.add(leaf)
            ctor_leaf.setdefault(leaf, q)

    # ARM/Thumb mode per address
    maps = sorted((s.value, s.name) for s in b.symbols if s.name in ("$a", "$t"))
    maddr = [m[0] for m in maps]

    def is_thumb(addr):
        i = bisect.bisect_right(maddr, addr) - 1
        return i >= 0 and maps[i][1] == "$t"

    md_arm = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
    md_thumb = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)
    md_arm.detail = md_thumb.detail = True

    # Reconstruct absolute instruction addresses (BinExport2 delta-codes them).
    instrs = []
    cur = 0
    for ins in p.instruction:
        if ins.HasField("address"):
            cur = ins.address
        instrs.append((cur, ins.raw_bytes, list(ins.call_target)))
        cur += len(ins.raw_bytes)

    def decode(idx):
        addr, raw, _ = instrs[idx]
        md = md_thumb if is_thumb(addr) else md_arm
        for insn in md.disasm(raw, addr):
            return insn
        return None

    def is_r0(insn):
        return (insn.operands and insn.operands[0].type == cs_arm.ARM_OP_REG
                and insn.reg_name(insn.operands[0].reg) == "r0")

    sizes = defaultdict(list)   # qualified class -> [(size, addr)]
    skipped_no_size = skipped_no_ctor = 0
    for i, (addr, raw, cts) in enumerate(instrs):
        if not any(t in new_addrs for t in cts):
            continue
        insn = decode(i)
        if not insn or insn.mnemonic not in ("bl", "blx"):
            continue                                   # only real call sites, not PLT veneers

        # backward: nearest write of an immediate into r0 = the size arg
        size = None
        for j in range(i - 1, max(-1, i - a.back), -1):
            y = decode(j)
            if not y:
                continue
            if y.mnemonic in ("mov", "movw", "movs") and is_r0(y) and \
               len(y.operands) >= 2 and y.operands[1].type == cs_arm.ARM_OP_IMM:
                size = y.operands[1].imm
                break
            if is_r0(y):       # r0 clobbered by something else first
                break
        if size is None:
            skipped_no_size += 1
            continue

        # forward: first call whose demangled leaf is a known ctor -> the class
        cls = None
        for j in range(i + 1, min(len(instrs), i + a.fwd)):
            for t in instrs[j][2]:
                leaf = addr2name.get(t, "")
                if leaf in ctor_leaf:
                    cls = ctor_leaf[leaf]
                    break
            if cls:
                break
        if cls is None:
            skipped_no_ctor += 1
            continue
        sizes[cls].append((size, addr))

    out = {}
    for cls, lst in sizes.items():
        vals = [s for s, _ in lst]
        consensus = max(set(vals), key=vals.count)
        out[cls] = {
            "size": consensus,
            "size_hex": hex(consensus),
            "alloc_count": len(lst),
            "sizes_seen": sorted(set(vals)),
            "sample_addr": hex(lst[0][1]),
        }
    json.dump(out, open(a.out, "w"), indent=1, sort_keys=True)
    print("inferred sizes for %d classes -> %s" % (len(out), a.out))
    print("  (skipped: %d no-size, %d no-ctor; ctor-leaf collisions: %d)"
          % (skipped_no_size, skipped_no_ctor, len(leaf_collide)))
    multi = {c: [hex(x) for x in v["sizes_seen"]] for c, v in out.items() if len(v["sizes_seen"]) > 1}
    if multi:
        print("  WARNING %d classes had inconsistent sizes (took majority):" % len(multi))
        for c, ss in list(multi.items())[:8]:
            print("    %-40s %s" % (c, ss))


if __name__ == "__main__":
    main()
