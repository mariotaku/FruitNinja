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

PROJECT_ROOT = pathlib.Path(__file__).resolve().parent.parent
BINARY  = pathlib.Path(os.environ.get(
    "ASM_VERIFY_BINARY",
    PROJECT_ROOT / "FruitNinjaBada" / "Bin" / "FruitNinja.exe"))
OBJDUMP = pathlib.Path(os.environ.get(
    "ASM_VERIFY_OBJDUMP",
    PROJECT_ROOT / "bada_SDK" / "Tools" / "Toolchains" / "ARM" / "bin" / "arm-bada-eabi-objdump.exe"))
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
        nargs="*",
        default=[
            str(PROJECT_ROOT / "tools" / "asm-verify-manifest.toml"),
            str(PROJECT_ROOT / "tools" / "asm-verify-manifest.generated.toml"),
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

    syms: list[dict] = []
    seen_names: set[str] = set()
    manifest_concat = ""
    for mp in args.manifest:
        path = pathlib.Path(mp)
        if not path.exists():
            continue
        text = path.read_text()
        manifest_concat += text + "\n"
        for s in tomllib.loads(text).get("symbol", []):
            if s["mangled"] in seen_names:
                continue
            seen_names.add(s["mangled"])
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
    print(f"  exported {n_ok} symbols ({n_fail} failed) to {OUT_DIR.relative_to(PROJECT_ROOT)}")
    CACHE_KEY.write_text(new_key + "\n")


if __name__ == "__main__":
    main()
