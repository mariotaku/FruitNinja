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


# Skip symbols we don't care about diffing:
#   - leading underscore-only / weak gcc helpers
#   - typeinfo, vtable references, dynamic symbols
SKIP_PREFIXES = (
    "_ZTI",       # typeinfo
    "_ZTS",       # typeinfo name
    "_ZTV",       # vtable
    "_ZTT",       # VTT (sub-vtable for virtual base init)
    "_ZN3Osp",    # bada Osp:: namespace -- not ported
    "_ZTVN3Osp",
    "_ZGVZ",      # static guard variables for function-local statics
)


def run_nm(target: pathlib.Path) -> dict[str, tuple[int, int]]:
    """Return {mangled: (addr, size)} for `T` (text) symbols in target."""
    out = subprocess.run(
        [str(NM), "--print-size", str(target)],
        capture_output=True, text=True, check=True,
    ).stdout
    syms: dict[str, tuple[int, int]] = {}
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
        if type_ not in ("T", "t"):  # Text-section only.
            continue
        if any(name.startswith(p) for p in SKIP_PREFIXES):
            continue
        try:
            addr = int(addr_s, 16)
        except ValueError:
            continue
        # Keep the first-seen entry per name (handles duplicates).
        syms.setdefault(name, (addr, size))
    return syms


def walk_cross_objs() -> dict[str, pathlib.Path]:
    """Return {mangled: obj_path} for every text symbol in build/bada-cross."""
    obj_files = list(CROSS.rglob("*.obj")) + list(CROSS.rglob("*.o"))
    out: dict[str, pathlib.Path] = {}
    for obj in obj_files:
        try:
            syms = run_nm(obj)
        except Exception as e:
            print(f"  WARN: nm failed on {obj.relative_to(PROJECT_ROOT)}: {e}", file=sys.stderr)
            continue
        for name in syms:
            prev = out.get(name)
            if prev is not None and prev != obj:
                # First-seen-wins over rglob order is arbitrary; if a mangled
                # name is ever defined in two TUs, surface it -- the silently
                # picked object could be the wrong body.
                print(f"  WARN: {name} defined in multiple objects; "
                      f"keeping {prev.name}, ignoring {obj.name}", file=sys.stderr)
                continue
            out.setdefault(name, obj)
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
    aliases = load_port_aliases()
    # A binary symbol pairs either via its own name or via a hand-written
    # port_mangled alias (whose body may carry a port-chosen mangled name
    # that never appears in the binary).
    common = sorted(name for name in bin_syms
                    if name in port_syms or aliases.get(name) in port_syms)
    only_bin = len(set(bin_syms) - set(common))
    only_port = len(set(port_syms) - set(bin_syms))

    rows = []
    for name in common:
        addr, size = bin_syms[name]
        if size == 0:
            # Fall back to next-symbol-addr - this-addr (rough estimate).
            sorted_addrs = sorted(a for (a, _) in bin_syms.values() if a > addr)
            size = (sorted_addrs[0] - addr) if sorted_addrs else 32
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
    print(f"\nWrote {OUT.relative_to(PROJECT_ROOT)} with {len(rows)} symbols.")
    print(f"  binary-only:  {only_bin} symbols  (not in cross-build)")
    print(f"  port-only:    {only_port} symbols  (not in binary -- new helpers)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
