"""Binary FUNC coverage analysis: lief vs asm-verify report.

Reads all FUNC symbols from FruitNinja.exe (via lief), cross-references
asm-verify's report.json to identify which symbols were matched by the
cross-build, and prints a coverage breakdown by subsystem bucket.

Usage:
    python tools/asm-verify/coverage/catC-survey.py [--project <root>]

    --project  Path to project root (default: three dirs above this script,
               i.e. the fruit-ninja repo root).

Reads:  <project>/FruitNinjaBada/Bin/FruitNinja.exe
        <project>/tmp/asm-verify/report.json
Writes: <project>/tmp/asm-verify/coverage/catC.json

Requires: lief (pip install lief)
Optional: itanium_demangler (pip install itanium_demangler) for cleaner names
"""
import argparse
import json
import re
import pathlib
from collections import Counter, defaultdict

try:
    import lief
except ImportError:
    raise SystemExit("ERROR: lief not installed -- run: pip install lief")

try:
    import itanium_demangler as dm
except ImportError:
    dm = None

parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
parser.add_argument("--project", default=None,
                    help="Project root (default: inferred from script location)")
args = parser.parse_args()

root = pathlib.Path(args.project).resolve() if args.project else pathlib.Path(__file__).resolve().parent.parent.parent.parent
binp = root / "FruitNinjaBada" / "Bin" / "FruitNinja.exe"
rep  = json.load(open(root / "tmp" / "asm-verify" / "report.json"))
out_dir = root / "tmp" / "asm-verify" / "coverage"
out_dir.mkdir(parents=True, exist_ok=True)

port_matched = {s["mangled"] for s in rep["symbols"]}  # symbols in the diff = compiled+matched

b = lief.parse(str(binp))
func_syms = {}
for sym in b.symbols:
    if sym.value == 0:
        continue
    n = sym.name
    if not n or n[0] in "$.":
        continue
    t = str(sym.type)
    if t != "TYPE.FUNC":
        continue
    func_syms.setdefault(n, sym.value)


def demangle(m):
    if dm is None:
        return m
    try:
        node = dm.parse(m)
        if node is not None:
            return str(node)
    except Exception:
        pass
    return m


# Skip prefixes (engine helpers we don't intend to verify per discover-symbols)
SKIP = ("_ZTI", "_ZTS", "_ZTV", "_ZTT", "_ZN3Osp", "_ZTVN3Osp", "_ZGVZ",
        "_ZNSt", "_ZNSs", "_ZSt", "_ZNKSt", "__", "_ZSt")


def bucket(dn, mangled):
    if mangled.startswith("_ZN3Osp") or "::Osp" in dn or dn.startswith("Osp"):
        return "bada-osp"
    if mangled.startswith(("_ZNSt", "_ZNSs", "_ZSt", "_ZNKSt")) or dn.startswith("std::"):
        return "stdlib"
    if re.search(r"\bScreen\b", dn):
        return "screens"
    if re.search(r"FruitFact", dn):
        return "screens"
    if re.search(r"\b(HUD|Control|Counter|Button|Menu|Scroller|Slider|CheckBox|ListItem)\b", dn):
        return "hud"
    if re.search(r"\b(Fruit|Bomb|Coin|Entity|Slash|Splat|Jiblet|SuperFruit|Actor)\b", dn):
        return "entities"
    if re.search(r"\b(Packet|Modifier|Wave|Score|Bonus|PowerUp|Game|Combo|Spawn|Coin|DRM|Highscore|Profanity|Achievement|News)\b", dn):
        return "game"
    if mangled.startswith("_ZN6Mortar") or "Mortar::" in dn:
        return "engine-mortar"
    return "engine-other"


rows = []
for m, addr in func_syms.items():
    if any(m.startswith(p.replace(" ", "")) for p in SKIP):
        continue
    dn = demangle(m)
    if dn.startswith("std::") or dn.startswith("__"):
        continue
    ported = m in port_matched
    rows.append({"mangled": m, "demangled": dn, "addr": addr,
                 "ported": ported, "bucket": bucket(dn, m)})

total  = len(rows)
ported = sum(1 for r in rows if r["ported"])
unported = [r for r in rows if not r["ported"]]
print(f"Binary FUNC symbols (excl Osp/std/typeinfo/vtable): {total}")
print(f"  matched in asm-diff (port covered): {ported}")
print(f"  NOT matched (unported OR excluded OR unmatched): {len(unported)}")
print()
bc = Counter(r["bucket"] for r in unported)
print("=== Unmatched by bucket ===")
for k, v in bc.most_common():
    pc = sum(1 for r in rows if r["bucket"] == k and r["ported"])
    tc = sum(1 for r in rows if r["bucket"] == k)
    print(f"  {k:16s}: {v:4d} unmatched / {tc:4d} total ({pc} matched)")

# Non-engine game/screen/hud/entity clusters: group by class
print()
print("=== Top unmatched NON-engine clusters (game/screens/hud/entities) by class ===")
cls = defaultdict(int)
for r in unported:
    if r["bucket"] in ("game", "screens", "hud", "entities"):
        dn = r["demangled"]
        mm = re.match(r"([A-Za-z_][A-Za-z0-9_]*)::", dn)
        key = mm.group(1) if mm else dn.split("(")[0][:40]
        cls[key] += 1
for k, v in sorted(cls.items(), key=lambda x: -x[1])[:50]:
    print(f"  {v:3d}  {k}")

out_file = out_dir / "catC.json"
json.dump(
    {"total": total, "ported": ported, "unported": len(unported),
     "by_bucket": dict(bc),
     "nonengine_clusters": dict(sorted(cls.items(), key=lambda x: -x[1]))},
    open(out_file, "w"), indent=2)
print(f"\nWrote {out_file}")
