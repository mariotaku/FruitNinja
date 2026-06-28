#!/usr/bin/env python3
"""Diff global/static DATA symbols (STT_OBJECT) binary-vs-port.

The function-level BinDiff can't see DATA. This catches a different bug class:
  - PORT-ONLY globals  -> port invented a global with NO binary counterpart
       (e.g. the fabricated g_HitLatch/g_HitResetCounter, or a file-static the
        binary actually keeps as a struct field). Prime "fix at root" candidates.
  - SIZE-MISMATCH      -> a global present in BOTH but with a different st_size
       (a wrong-sized global array/table/struct instance -- a layout bug the
        operator-new size net misses because it's static, not heap-allocated).
  - BINARY-ONLY        -> a binary global the port never declares. NOISY here:
       the cross-build .so only links the ported subset, so most binary-only
       symbols are just "not ported yet". Shown last, filtered, for reference.

Match is by MANGLED name (same global -> same mangling in both builds). Demangle
for readability via the toolchain c++filt in the fnverify-bada image. Noise
(vtables/typeinfo/guards, std::, __gnu_cxx, __cxxabi, SDL, ARM mapping) filtered.

Usage:
  compare-globals.py --binary FruitNinjaBada/Bin/FruitNinja.exe \
      --port-so tmp/fnverify.arm.so --out tmp/asm-verify/globals-diff.csv
"""
import argparse, os, subprocess, sys, csv

try:
    import lief
except ImportError:
    sys.exit("error: pip install lief")

# mangled-name prefixes that are compiler-generated / std noise -> drop pre-demangle
NOISE_PREFIX = (
    "_ZTV", "_ZTI", "_ZTS", "_ZGV", "_ZTT", "_ZTC",   # vtable/typeinfo/guard/VTT/construction-vtable
    "_ZSt", "_ZNSt", "_ZNKSt",                          # std::
    "_ZN9__gnu_cxx", "_ZNK9__gnu_cxx",                  # __gnu_cxx
    "_ZN10__cxxabiv", "_ZTVN10__cxxabiv",               # __cxxabiv1
)
NOISE_SUBSTR_DEMANGLED = ("std::", "__gnu_cxx", "__cxxabiv", "_GLOBAL__")
PLATFORM_SUBSTR = ("SDL_", "_SDL", "GL_", "gl_", "emscripten", "dlmalloc")


def object_symbols(path):
    """mangled name -> st_size, for STT_OBJECT named symbols (noise pre-filtered)."""
    b = lief.parse(path)
    if b is None:
        return {}
    out = {}
    for s in b.symbols:
        n = s.name or ""
        if not n or (len(n) == 2 and n[0] == "$"):          # empty / ARM mapping
            continue
        if "OBJECT" not in str(getattr(s, "type", "")):       # data symbols only
            continue
        if n.startswith(NOISE_PREFIX):
            continue
        # keep the largest size seen for a name (weak/dup symbols)
        sz = int(getattr(s, "size", 0) or 0)
        if n not in out or sz > out[n]:
            out[n] = sz
    return out


def bulk_demangle(names, image):
    uniq = sorted(n for n in names if n)
    if not uniq or image == "none":
        return {n: n for n in uniq}
    try:
        p = subprocess.run(
            ["docker", "run", "--rm", "-i", image, "bash", "-c",
             "export PATH=/opt/codesourcery/bin:$PATH; arm-samsung-nucleuseabi-c++filt"],
            input="\n".join(uniq), capture_output=True, text=True, timeout=180)
        out = p.stdout.splitlines()
        if len(out) == len(uniq):
            return dict(zip(uniq, out))
    except Exception as e:
        sys.stderr.write("warn: demangle failed (%s); using mangled\n" % e)
    return {n: n for n in uniq}


def looks_interesting(dm):
    """port-only filter: drop std/platform/compiler noise; keep real globals."""
    if any(s in dm for s in NOISE_SUBSTR_DEMANGLED):
        return False
    if any(s in dm for s in PLATFORM_SUBSTR):
        return False
    return True


# legit port statics that are NOT behavioral state (textures, singletons, const
# lookup tables / const Vec3s). Filtering these leaves the mutable-state globals
# where fabrications (g_HitLatch-style) hide.
def is_behavioral_candidate(dm):
    leaf = dm.split("::")[-1].split("(")[0]
    if "s_Tex" in dm or "s_instance" in dm or "CSWTCH" in dm:
        return False
    if leaf and leaf.isupper():                         # CONST_NAMES (COIN_SCALE, BORDER_POS)
        return False
    if leaf[:1] == "k" and leaf[1:2].isupper():          # kModeNames, kIdentityTint
        return False
    if any(t in leaf for t in ("Names", "Colour", "Color", "Table", "Types",
                               "Tint", "fallback", "Init", "Pos", "Scale", "Gravity")):
        return False
    if "()::" in dm and leaf[:1].islower() and leaf[:2] != "s_" and leaf[:2] != "g_":
        return False                                     # misc function-local const scratch
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default="FruitNinjaBada/Bin/FruitNinja.exe")
    ap.add_argument("--port-so", default="tmp/fnverify.arm.so")
    ap.add_argument("--image", default="fnverify-bada", help="c++filt image, or 'none'")
    ap.add_argument("--out", default="tmp/asm-verify/globals-diff.csv")
    a = ap.parse_args()

    bsym = object_symbols(a.binary)
    psym = object_symbols(a.port_so) if os.path.exists(a.port_so) else {}
    print("binary OBJECT symbols: %d   port OBJECT symbols: %d" % (len(bsym), len(psym)))

    both = set(bsym) & set(psym)
    port_only = set(psym) - set(bsym)
    binary_only = set(bsym) - set(psym)

    mismatch = [(n, bsym[n], psym[n]) for n in both if bsym[n] != psym[n] and bsym[n] and psym[n]]

    dm = bulk_demangle(both | port_only | binary_only, a.image)

    # port-only that look like real (non-std/platform) globals = fabrication candidates
    po = [(n, psym[n]) for n in port_only if looks_interesting(dm.get(n, n))]
    bo = [(n, bsym[n]) for n in binary_only if looks_interesting(dm.get(n, n))]

    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    with open(a.out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["category", "size_binary", "size_port", "symbol"])
        for n, bs, ps in sorted(mismatch, key=lambda x: -abs(x[1] - x[2])):
            w.writerow(["SIZE-MISMATCH", bs, ps, dm.get(n, n)])
        for n, ps in sorted(po, key=lambda x: dm.get(x[0], x[0])):
            w.writerow(["PORT-ONLY", "", ps, dm.get(n, n)])
        for n, bs in sorted(bo, key=lambda x: dm.get(x[0], x[0])):
            w.writerow(["BINARY-ONLY", bs, "", dm.get(n, n)])

    print("\n=== SIZE-MISMATCH (same global, different st_size -> layout bug) : %d ===" % len(mismatch))
    for n, bs, ps in sorted(mismatch, key=lambda x: -abs(x[1] - x[2]))[:30]:
        print("  bin 0x%-5x port 0x%-5x  %s" % (bs, ps, dm.get(n, n)[:60]))
    behav = [(n, ps) for n, ps in po if is_behavioral_candidate(dm.get(n, n))]
    print("\n=== PORT-ONLY mutable-state globals (fabrication / struct-field candidates) : %d ==="
          % len(behav))
    print("    (filtered out %d legit texture/singleton/const-table statics)" % (len(po) - len(behav)))
    for n, ps in sorted(behav, key=lambda x: dm.get(x[0], x[0]))[:50]:
        print("  0x%-5x  %s" % (ps, dm.get(n, n)[:64]))
    print("\nbinary-only (mostly just-not-ported, reference): %d  -> %s" % (len(bo), a.out))


if __name__ == "__main__":
    main()
