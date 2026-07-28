#!/usr/bin/env python3
"""Auto-discover comparable symbols by intersecting binary nm with port nm.

Output: tools/asm-verify-manifest.generated.toml
   List of [[symbol]] entries that exist on BOTH sides.

The hand-written tools/asm-verify-manifest.toml acts as an override:
asm-verify.py reads both, with hand-written entries taking precedence over
generated ones (lets you tune notes / size / address for special cases).

Usage:
    python tools/discover-symbols.py
"""
import bisect
import json
import pathlib
import subprocess
import sys

import os

try:
    import tomllib
except ImportError:
    import tomli as tomllib  # type: ignore

# tools/asm-verify/discover-symbols.py -> project root is two parents up.
ASM_VERIFY_DIR = pathlib.Path(__file__).resolve().parent
PROJECT_ROOT   = ASM_VERIFY_DIR.parent.parent
BINARY = pathlib.Path(os.environ.get(
    "ASM_VERIFY_BINARY",
    PROJECT_ROOT / "FruitNinjaBada" / "Bin" / "FruitNinja.exe"))
NM     = pathlib.Path(os.environ.get(
    "ASM_VERIFY_NM",
    PROJECT_ROOT / "tools" / "toolchain" / "sourcery-2010q1" / "bin" / "arm-none-eabi-nm"))
CROSS  = pathlib.Path(os.environ.get(
    "ASM_VERIFY_BUILD_DIR",
    PROJECT_ROOT / "build" / "bada-cross"))
OUT    = pathlib.Path(os.environ.get(
    "ASM_VERIFY_MANIFEST_OUT",
    ASM_VERIFY_DIR / "manifest.generated.toml"))
HAND_MANIFEST = ASM_VERIFY_DIR / "manifest.toml"
# Side-car consumed by signature-mismatch.py (which runs HOST-side, where the
# cross toolchain's nm/c++filt do not exist). Everything it needs -- both symbol
# sets AND the demangle map -- is captured here, in the container, in one pass.
REPORT_DIR = pathlib.Path(os.environ.get(
    "ASM_VERIFY_REPORT_DIR", PROJECT_ROOT / "tmp" / "asm-verify"))
SYMBOL_INDEX = REPORT_DIR / "symbol-index.json"


# Skip symbols we don't care about diffing:
#   - leading underscore-only / weak gcc helpers
#   - typeinfo, vtable references, dynamic symbols
#   - STL / libstdc++ internals: these are weak template instantiations emitted
#     into BOTH binaries by DIFFERENT libstdc++ headers. Diffing them measures
#     the header versions, not the port, and (before the W/w acceptance below)
#     they never reached the intersection at all. ~2985 binary symbols.
SKIP_PREFIXES = (
    "_ZTI",       # typeinfo
    "_ZTS",       # typeinfo name
    "_ZTV",       # vtable
    "_ZTT",       # VTT (sub-vtable for virtual base init)
    "_ZN3Osp",    # bada Osp:: namespace -- not ported
    "_ZTVN3Osp",
    "_ZGVZ",      # static guard variables for function-local statics
    # --- libstdc++ / compiler-runtime weak instantiations (see note above) ---
    "_ZNSt",          # std::  (non-const member / free)
    "_ZNKSt",         # std::  (const member)
    "_ZSt",           # std::  (free function)
    "_ZN9__gnu_cxx",  # __gnu_cxx::
    "_ZNK9__gnu_cxx",
    "_ZN10__cxxabiv1",  # __cxxabiv1::
    "_ZNK10__cxxabiv1",
)

# nm type letters that denote a DEFINED FUNCTION body we can disassemble.
#   T/t  -- text, global / local
#   W/w  -- weak, "not specifically tagged as an object": inline members, out-of
#           -line template instantiations, implicit ctors/dtors. GCC 4.4 emits
#           the overwhelming majority of C++ bodies this way -- 5880 of the
#           binary's 9619 FUNC symbols are W, and every one of them was being
#           dropped here, i.e. never paired, never diffed, invisible.
# Deliberately NOT accepted:
#   V/v  -- weak OBJECT (data). In this binary all 723 live at 0x26f4d0+, i.e.
#           past the end of .text (0x26f4c0): typeinfo, vtables, guard vars.
#           They have no instruction stream to diff.
TEXT_TYPES = ("T", "t", "W", "w")


def run_nm(target: pathlib.Path) -> dict[str, tuple[int, int, str]]:
    """Return {mangled: (addr, size, nm_type)} for defined function bodies."""
    out = subprocess.run(
        [str(NM), "--print-size", str(target)],
        capture_output=True, text=True, check=True,
    ).stdout
    syms: dict[str, tuple[int, int, str]] = {}
    for line in out.splitlines():
        # Format: ADDR SIZE TYPE NAME    or    ADDR TYPE NAME (no size)
        parts = line.split(None, 3)
        if len(parts) < 3:
            continue
        # Without --print-size, two parts: addr type name. With it, four:
        # addr size type name. We requested it, but some symbols have no size.
        if len(parts) == 3:
            addr_s, type_, name = parts
            size = 0
        else:
            addr_s, size_s, type_, name = parts
            try:
                size = int(size_s, 16)
            except ValueError:
                size = 0
        if type_ not in TEXT_TYPES:  # Defined function bodies only.
            continue
        if any(name.startswith(p) for p in SKIP_PREFIXES):
            continue
        try:
            addr = int(addr_s, 16)
        except ValueError:
            continue
        # Keep the first-seen entry per name (handles duplicates).
        syms.setdefault(name, (addr, size, type_))
    return syms


def walk_cross_objs() -> dict[str, pathlib.Path]:
    """Return {mangled: obj_path} for every text symbol in build/bada-cross."""
    obj_files = list(CROSS.rglob("*.obj")) + list(CROSS.rglob("*.o"))
    out: dict[str, pathlib.Path] = {}
    kinds: dict[str, str] = {}
    for obj in obj_files:
        try:
            syms = run_nm(obj)
        except Exception as e:
            print(f"  WARN: nm failed on {obj.relative_to(PROJECT_ROOT)}: {e}", file=sys.stderr)
            continue
        for name, (_a, _s, type_) in syms.items():
            prev = out.get(name)
            if prev is not None and prev != obj:
                # A WEAK (W/w) symbol legitimately appears in every TU that
                # includes its header -- inline members, template bodies. All
                # copies are the same code and the linker keeps one, so this is
                # not ambiguity and must not be reported (it would print
                # thousands of lines and bury the real finding below).
                if type_ in ("W", "w") and kinds.get(name) in ("W", "w"):
                    continue
                # STRONG (T/t) duplicate: first-seen-wins over rglob order is
                # arbitrary and the two bodies really can differ (e.g. two
                # anonymous-namespace helpers sharing a mangled name), so the
                # silently picked object could be the wrong body -- surface it.
                print(f"  WARN: {name} defined in multiple objects; "
                      f"keeping {prev.name}, ignoring {obj.name}", file=sys.stderr)
                continue
            out.setdefault(name, obj)
            kinds.setdefault(name, type_)
    return out


def load_port_aliases() -> dict[str, str]:
    """{binary_mangled: port_mangled} from the hand-written manifest.

    A `port_mangled` alias says: the real port body for this binary symbol
    lives under a different mangled name (see manifest.toml header). The
    aliased body may live in a DIFFERENT TU than a same-name port forwarder,
    so the generated row's `port` obj must point at the alias's object file.
    """
    if not HAND_MANIFEST.exists():
        return {}
    try:
        data = tomllib.loads(HAND_MANIFEST.read_text())
    except Exception as e:
        print(f"  WARN: cannot parse {HAND_MANIFEST.name}: {e}", file=sys.stderr)
        return {}
    return {s["mangled"]: s["port_mangled"]
            for s in data.get("symbol", []) if "port_mangled" in s}


def demangle(names: list) -> dict:
    """{mangled: demangled} via the cross toolchain's c++filt (one process).

    Lives next to nm in the toolchain bin dir. If it's missing we degrade to an
    identity map rather than failing the sweep -- signature-mismatch.py then
    just sees fewer matches, and says so.
    """
    filt = NM.parent / NM.name.replace("nm", "c++filt")
    if not filt.exists():
        print(f"  WARN: {filt.name} not found; symbol-index demangle map empty",
              file=sys.stderr)
        return {}
    try:
        res = subprocess.run([str(filt)], input="\n".join(names) + "\n",
                             capture_output=True, text=True, check=True)
    except Exception as e:
        print(f"  WARN: c++filt failed: {e}", file=sys.stderr)
        return {}
    out = res.stdout.split("\n")
    return {m: (out[i].strip() if i < len(out) else m)
            for i, m in enumerate(names)}


def write_symbol_index(bin_syms: dict, port_syms: dict) -> None:
    """Dump both symbol sets + a demangle map for the host-side checks."""
    names = sorted(set(bin_syms) | set(port_syms))
    dem = demangle(names)
    payload = {
        "binary": [{"mangled": n, "addr": bin_syms[n][0], "size": bin_syms[n][1]}
                   for n in sorted(bin_syms)],
        "port": {n: port_syms[n].name for n in sorted(port_syms)},
        "demangled": dem,
    }
    SYMBOL_INDEX.parent.mkdir(parents=True, exist_ok=True)
    SYMBOL_INDEX.write_text(json.dumps(payload))


def write_manifest(intersect: list[dict]) -> None:
    OUT.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# AUTO-GENERATED by tools/discover-symbols.py -- DO NOT HAND-EDIT.",
        "# Override individual entries via tools/asm-verify-manifest.toml.",
        "",
    ]
    for s in intersect:
        # Emit relative-to-PROJECT_ROOT when possible (host build), absolute
        # otherwise (Linux verify run where build dir lives in ~/fn-build).
        try:
            port_rel = s["port"].relative_to(PROJECT_ROOT).as_posix()
        except ValueError:
            port_rel = s["port"].as_posix()
        lines.append("[[symbol]]")
        lines.append(f'mangled = "{s["mangled"]}"')
        lines.append(f'addr    = "0x{s["addr"]:08x}"')
        lines.append(f'size    = {s["size"]}')
        lines.append(f'port    = "{port_rel}"')
        lines.append("")
    OUT.write_text("\n".join(lines))


def main() -> int:
    if not BINARY.exists():
        print(f"binary missing: {BINARY}", file=sys.stderr)
        return 2
    if not CROSS.exists():
        print(f"cross-build dir missing: {CROSS} (run cmake --build first)", file=sys.stderr)
        return 2

    print(f"[1/3] nm on binary {BINARY.name}...")
    bin_syms = run_nm(BINARY)
    print(f"      {len(bin_syms)} text symbols")

    print(f"[2/3] nm on cross-build .obj files...")
    port_syms = walk_cross_objs()
    print(f"      {len(port_syms)} text symbols")

    print(f"[3/3] intersect + write manifest...")
    write_symbol_index(bin_syms, port_syms)
    aliases = load_port_aliases()
    # A binary symbol pairs either via its own name or via a hand-written
    # port_mangled alias (whose body may carry a port-chosen mangled name
    # that never appears in the binary).
    common = sorted(name for name in bin_syms
                    if name in port_syms or aliases.get(name) in port_syms)
    only_bin = len(set(bin_syms) - set(common))
    only_port = len(set(port_syms) - set(bin_syms))

    # Precomputed once: O(n log n) instead of a full re-sort per zero-size
    # symbol (the W/w acceptance roughly doubled the symbol count).
    all_addrs = sorted(a for (a, _s, _t) in bin_syms.values())

    rows = []
    for name in common:
        addr, size, _type = bin_syms[name]
        if size == 0:
            # Fall back to next-symbol-addr - this-addr (rough estimate).
            i = bisect.bisect_right(all_addrs, addr)
            size = (all_addrs[i] - addr) if i < len(all_addrs) else 32
        port_name = aliases.get(name, name)
        if port_name not in port_syms:
            # Alias declared but its body isn't in the cross-build (e.g. TU
            # excluded); fall back to the same-name obj so the row stays
            # visible (asm-verify will then report the alias as not found).
            print(f"  WARN: port_mangled alias {port_name} for {name} not in "
                  f"cross-build; using same-name obj", file=sys.stderr)
            port_name = name
        rows.append({"mangled": name, "addr": addr, "size": size,
                     "port": port_syms[port_name]})

    write_manifest(rows)
    try:
        out_disp = OUT.relative_to(PROJECT_ROOT)
    except ValueError:
        out_disp = OUT
    print(f"\nWrote {out_disp} with {len(rows)} symbols.")
    print(f"  binary-only:  {only_bin} symbols  (not in cross-build)")
    print(f"  port-only:    {only_port} symbols  (not in binary -- new helpers)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
