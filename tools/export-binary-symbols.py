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
import pathlib
import subprocess
import sys

try:
    import tomllib  # Python 3.11+
except ImportError:
    import tomli as tomllib  # type: ignore

PROJECT_ROOT = pathlib.Path(__file__).resolve().parent.parent
BINARY = PROJECT_ROOT / "FruitNinjaBada" / "Bin" / "FruitNinja.exe"
OBJDUMP = PROJECT_ROOT / "bada_SDK" / "Tools" / "Toolchains" / "ARM" / "bin" / "arm-bada-eabi-objdump.exe"
OUT_DIR = PROJECT_ROOT / "bada-binary" / "symbols"


def export_one(name: str, addr_hex: str, size_bytes: int) -> pathlib.Path:
    """Disassemble bytes [addr, addr+size) and write to out_dir/<safe>.s."""
    addr = int(addr_hex, 16)
    end = addr + size_bytes
    out = OUT_DIR / f"{_safe(name)}.s"
    out.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(OBJDUMP),
        "-d",
        "--no-show-raw-insn",
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
    """Trim objdump preamble; drop line numbers asm-differ doesn't care about."""
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
        nargs="?",
        default=str(PROJECT_ROOT / "tools" / "asm-verify-manifest.toml"),
    )
    args = ap.parse_args()

    if not BINARY.exists():
        sys.exit(f"binary missing: {BINARY}")
    if not OBJDUMP.exists():
        sys.exit(f"objdump missing: {OBJDUMP}")

    manifest_path = pathlib.Path(args.manifest)
    if not manifest_path.exists():
        sys.exit(f"manifest missing: {manifest_path}")

    data = tomllib.loads(manifest_path.read_text())
    syms = data.get("symbol", [])
    if not syms:
        sys.exit("manifest has no [[symbol]] entries")

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for s in syms:
        try:
            out = export_one(s["mangled"], s["addr"], s["size"])
            print(f"  exported {s['mangled']:50} -> {out.relative_to(PROJECT_ROOT)}")
        except Exception as e:
            print(f"  FAILED  {s['mangled']:50}: {e}", file=sys.stderr)


if __name__ == "__main__":
    main()
