#!/usr/bin/env python3
"""Extract the binary's RTTI type hierarchy into a JSON type-tree.

Self-contained (LIEF only -- no Ghidra/MCP). Reads Itanium C++ ABI type_info
(_ZTI*) structures. The binary is PIC, so type_info pointer fields are filled by
SYMBOL relocations (the in-place words are 0/addend), not stored addresses --
so we resolve every field through the relocation map.

Per-class type_info kinds (vptr field's reloc names the abi vtable):
  __class_type_info  -> no bases
  __si_class_type_info -> single public non-virtual base at +0x8
  __vmi_class_type_info -> +0x8 flags, +0xc base_count, then base_array of
       {base _ZTI ptr (reloc), __offset_flags (int)}  (8 bytes each)

Output JSON: { "Qualified::Class": {
    "mangled_zti", "kind", "bases": [ {name, mangled_zti, offset, virtual, public} ] } }

Usage: extract-typeinfo.py --binary tmp/FruitNinja_v1_6_1.exe --out tmp/typeinfo-tree.json
"""
import argparse, json, struct, sys

try:
    import lief
except ImportError:
    sys.exit("error: pip install lief")

ABI = {
    "_ZTVN10__cxxabiv117__class_type_infoE": "class",
    "_ZTVN10__cxxabiv120__si_class_type_infoE": "si",
    "_ZTVN10__cxxabiv121__vmi_class_type_infoE": "vmi",
}
VIRTUAL_MASK = 0x1
PUBLIC_MASK = 0x2


def demangle_type(sym):
    """_ZTS10MainScreen / _ZTIN6Mortar6EntityE -> 'MainScreen' / 'Mortar::Entity'.
    Handles the common nested/length-prefixed forms; leaves templates partially
    raw (good enough for a hierarchy key)."""
    s = sym
    for p in ("_ZTI", "_ZTS", "_ZTV"):
        if s.startswith(p):
            s = s[len(p):]
            break
    if s.startswith("N") and s.endswith("E"):
        s = s[1:-1]
    comps, i = [], 0
    while i < len(s):
        j = i
        while j < len(s) and s[j].isdigit():
            j += 1
        if j == i:
            break
        n = int(s[i:j])
        comps.append(s[j:j + n])
        i = j + n
        # skip template args / substitutions we don't fully decode
        while i < len(s) and not s[i].isdigit() and s[i] not in "E":
            i += 1
    return "::".join(comps) if comps else (sym[4:] if len(sym) > 4 else sym)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default="tmp/FruitNinja_v1_6_1.exe")
    ap.add_argument("--out", default="tmp/typeinfo-tree.json")
    a = ap.parse_args()

    b = lief.parse(a.binary)

    # address -> reloc symbol name (PIC fills type_info pointers by symbol reloc)
    reloc = {}
    for r in b.relocations:
        try:
            if r.has_symbol and r.symbol and r.symbol.name:
                reloc[r.address] = r.symbol.name
        except Exception:
            pass

    name2addr = {s.name: s.value for s in b.symbols if s.name}

    # in-place dword reader (for vmi flags/count/offset_flags)
    secs = [(s.virtual_address, s.virtual_address + s.size, bytes(s.content))
            for s in b.sections if s.size]
    def rd32(addr):
        for lo, hi, data in secs:
            if lo <= addr < hi:
                off = addr - lo
                return struct.unpack_from("<I", data, off)[0]
        return 0

    ztis = [s.name for s in b.symbols if s.name and s.name.startswith("_ZTI")]
    out = {}
    n_class = n_si = n_vmi = n_unknown = 0
    for zti in ztis:
        a0 = name2addr.get(zti)
        if a0 is None:
            continue
        vptr_sym = reloc.get(a0)
        kind = ABI.get(vptr_sym, "unknown")
        name_sym = reloc.get(a0 + 4, "")
        cls = demangle_type(name_sym) if name_sym else demangle_type(zti)

        bases = []
        if kind == "class":
            n_class += 1
        elif kind == "si":
            n_si += 1
            base_sym = reloc.get(a0 + 8)
            if base_sym:
                bases.append({"name": demangle_type(base_sym), "mangled_zti": base_sym,
                              "offset": 0, "virtual": False, "public": True})
        elif kind == "vmi":
            n_vmi += 1
            count = rd32(a0 + 0xc)
            for i in range(min(count, 64)):
                ent = a0 + 0x10 + i * 8
                base_sym = reloc.get(ent)
                flags = rd32(ent + 4)
                if base_sym:
                    bases.append({
                        "name": demangle_type(base_sym), "mangled_zti": base_sym,
                        "offset": flags >> 8,
                        "virtual": bool(flags & VIRTUAL_MASK),
                        "public": bool(flags & PUBLIC_MASK)})
        else:
            n_unknown += 1

        out[cls] = {"mangled_zti": zti, "kind": kind, "bases": bases}

    json.dump(out, open(a.out, "w"), indent=1, sort_keys=True)
    print("type-tree: %d classes -> %s" % (len(out), a.out))
    print("  kinds: class(no-base)=%d si(single)=%d vmi(multi/virtual)=%d unknown=%d"
          % (n_class, n_si, n_vmi, n_unknown))
    # spot-check known chains
    for c in ("MainScreen", "Bomb", "HUDControl3d", "SlashModInfo"):
        if c in out:
            print("  %-14s : %s" % (c, [b["name"] for b in out[c]["bases"]] or "(root)"))


if __name__ == "__main__":
    main()
