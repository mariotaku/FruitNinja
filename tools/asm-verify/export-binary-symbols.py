#!/usr/bin/env python3
"""Export per-symbol ARM Thumb-2 disassembly from FruitNinja.exe.

Reads a list of (mangled_symbol, binary_address, byte_size) tuples from a
manifest TOML and writes one .s file per symbol under bada-binary/symbols/.

Phase A: small manifest (tools/asm-verify-manifest.toml). Phase B will
auto-discover symbols by matching cross-build mangled names against the
binary's nm output.

Usage:
    python tools/export-binary-symbols.py [manifest.toml]
"""
import argparse
import hashlib
import pathlib
import subprocess
import sys

try:
    import tomllib  # Python 3.11+
except ImportError:
    import tomli as tomllib  # type: ignore

import os

ASM_VERIFY_DIR = pathlib.Path(__file__).resolve().parent
PROJECT_ROOT   = ASM_VERIFY_DIR.parent.parent
BINARY  = pathlib.Path(os.environ.get(
    "ASM_VERIFY_BINARY",
    PROJECT_ROOT / "FruitNinjaBada" / "Bin" / "FruitNinja.exe"))
OBJDUMP = pathlib.Path(os.environ.get(
    "ASM_VERIFY_OBJDUMP",
    PROJECT_ROOT / "tools" / "toolchain" / "sourcery-2010q1" / "bin" / "arm-none-eabi-objdump"))
OUT_DIR = pathlib.Path(os.environ.get(
    "ASM_VERIFY_BIN_SYMBOL_DIR",
    PROJECT_ROOT / "bada-binary" / "symbols"))
CACHE_KEY = OUT_DIR.parent / ".cache-key"


def compute_cache_key(manifest_text: str) -> str:
    """Hash the inputs that could change exported symbols.

    If any of these change we must re-export. If all match a previous run,
    we can skip the (slow) per-symbol objdump pass entirely.
    """
    h = hashlib.sha256()
    h.update(BINARY.read_bytes())
    h.update(b"\x00")
    h.update(OBJDUMP.read_bytes()[:1024])  # first 1KB is enough to fingerprint
    h.update(b"\x00")
    h.update(manifest_text.encode("utf-8"))
    return h.hexdigest()


def export_one(name: str, addr_hex: str, size_bytes: int) -> pathlib.Path:
    """Disassemble bytes [addr, addr+size) and write to out_dir/<safe>.s."""
    addr = int(addr_hex, 16)
    end = addr + size_bytes
    out = OUT_DIR / f"{_safe(name)}.s"
    out.parent.mkdir(parents=True, exist_ok=True)
    # Keep raw hex bytes (objdump default). asm-verify.py normalize() strips
    # them, but they cost nothing and keep the .s files self-documenting.
    cmd = [
        str(OBJDUMP),
        "-d",
        "--start-address=0x{:x}".format(addr),
        "--stop-address=0x{:x}".format(end),
        str(BINARY),
    ]
    res = subprocess.run(cmd, capture_output=True, text=True, check=True)
    out.write_text(_normalize_objdump(res.stdout, name, addr))
    return out


def _safe(name: str) -> str:
    """Filesystem-safe rendering of a mangled symbol."""
    return name.replace("/", "_").replace(":", "_")


def _normalize_objdump(text: str, name: str, addr: int) -> str:
    """Trim objdump preamble; drop line numbers the asm comparison ignores."""
    lines = text.splitlines()
    out = [
        f"# Binary: {BINARY.name}",
        f"# Symbol: {name}",
        f"# Origin: 0x{addr:08x}",
        "",
    ]
    in_text = False
    for line in lines:
        if line.startswith("Disassembly of section"):
            in_text = True
            continue
        if not in_text:
            continue
        out.append(line)
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "manifest",
        nargs="*",
        default=[
            str(ASM_VERIFY_DIR / "manifest.toml"),
            str(ASM_VERIFY_DIR / "manifest.generated.toml"),
        ],
        help="One or more manifest files. Default: hand-written + generated.",
    )
    ap.add_argument(
        "--force", action="store_true",
        help="Re-export even if the cache key matches.",
    )
    args = ap.parse_args()

    if not BINARY.exists():
        sys.exit(f"binary missing: {BINARY}")
    if not OBJDUMP.exists():
        sys.exit(f"objdump missing: {OBJDUMP}")

    # Per-key merge across manifests (earlier manifests take precedence per
    # key) -- mirrors asm-verify.py's load_symbols. A hand-written override
    # may carry only `mangled` + `port_mangled` + `notes`; addr/size then
    # come from the generated manifest's row for the same symbol.
    merged: dict[str, dict] = {}
    order: list[str] = []
    manifest_concat = ""
    for mp in args.manifest:
        path = pathlib.Path(mp)
        if not path.exists():
            continue
        text = path.read_text()
        manifest_concat += text + "\n"
        for s in tomllib.loads(text).get("symbol", []):
            name = s["mangled"]
            if name in merged:
                m = s.copy()
                m.update(merged[name])  # earlier-manifest keys win
                merged[name] = m
            else:
                merged[name] = s
                order.append(name)
    syms = []
    for name in order:
        s = merged[name]
        if "addr" not in s or "size" not in s:
            print(f"  WARN: skipping {name}: no addr/size "
                  f"(no matching entry in the generated manifest)", file=sys.stderr)
            continue
        syms.append(s)
    if not syms:
        sys.exit("No [[symbol]] entries in any manifest.")

    # Cache check.
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    new_key = compute_cache_key(manifest_concat)
    if not args.force and CACHE_KEY.exists() and CACHE_KEY.read_text().strip() == new_key:
        print(f"  cache hit: skipping {len(syms)} symbols (already exported).")
        print(f"  pass --force to re-export anyway.")
        return

    n_ok = 0
    n_fail = 0
    for s in syms:
        try:
            export_one(s["mangled"], s["addr"], s["size"])
            n_ok += 1
        except Exception as e:
            print(f"  FAILED  {s['mangled']:50}: {e}", file=sys.stderr)
            n_fail += 1
    try:
        out_disp = OUT_DIR.relative_to(PROJECT_ROOT).as_posix()
    except ValueError:
        out_disp = OUT_DIR.as_posix()
    print(f"  exported {n_ok} symbols ({n_fail} failed) to {out_disp}")
    CACHE_KEY.write_text(new_key + "\n")


if __name__ == "__main__":
    main()
