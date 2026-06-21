#!/usr/bin/env python3
"""Detect function signature mismatches between binary and port.

Extracts symbols directly from the binary ELF (via Docker nm) and from
the asm-verify cross-build manifest. No pre-computed symbol-diff data needed.

Usage:
    python tools/asm-verify/checks/check-signatures.py
"""
import pathlib
import re
import subprocess
import sys
from collections import defaultdict

try:
    import tomllib
except ImportError:
    import tomli as tomllib

PROJECT = pathlib.Path(__file__).resolve().parent.parent.parent.parent
BINARY   = "FruitNinjaBada/Bin/FruitNinja.exe"  # rel to project, at /work in container
OUTPUT   = PROJECT / "tmp" / "asm-verify" / "signature-mismatches.md"
IMAGE    = "fnverify"


def to_docker_path(p: pathlib.Path) -> str:
    """Convert Windows/msys path to Docker-compatible C:/... form."""
    s = str(p.resolve())
    if s[0] == '/' and s[2] == '/':
        return s[1].upper() + ':' + s[2:]
    if s[1] == ':':
        return s[0].upper() + ':' + s[2:].replace('\\', '/')
    return s


def run(cmd: list[str], **kw) -> str:
    return subprocess.run(cmd, capture_output=True, text=True, **kw).stdout


def extract_binary_demangled() -> set[str]:
    """Extract demangled text symbols from the binary ELF via Docker nm."""
    project_docker = to_docker_path(PROJECT)
    cmd = [
        "docker", "run", "--rm",
        "-v", f"{project_docker}:/work",
        IMAGE, "-c",
        f"arm-none-eabi-nm --demangle /work/{BINARY} "
        f"| awk '$2 ~ /^[Tt]$/ {{ $1=\"\"; $2=\"\"; sub(/^  */,\"\"); print }}' "
        f"| sort -u"
    ]
    try:
        out = run(cmd, timeout=60)
    except Exception as e:
        print(f"ERROR: docker nm failed: {e}", file=sys.stderr)
        print("Is Docker running? Is the fnverify image built?", file=sys.stderr)
        sys.exit(1)
    return {line.strip() for line in out.splitlines() if line.strip()}


def extract_port_demangled() -> set[str]:
    """Extract demangled port symbols from the asm-verify report JSON."""
    import json
    report_path = PROJECT / "tmp" / "asm-verify" / "report.json"
    if not report_path.exists():
        print(f"ERROR: {report_path} not found.", file=sys.stderr)
        print("Run asm-verify first: bash tools/asm-verify/run.sh", file=sys.stderr)
        sys.exit(1)

    with open(report_path, encoding='utf-8') as f:
        report = json.load(f)

    mangled = [s['mangled'] for s in report.get('symbols', [])]
    if not mangled:
        print("ERROR: report.json has no symbols", file=sys.stderr)
        sys.exit(1)

    # Demangle via Docker c++filt (batch for speed)
    project_docker = to_docker_path(PROJECT)
    batch = '\n'.join(mangled)
    cmd = [
        "docker", "run", "--rm", "-i",
        "-v", f"{project_docker}:/work",
        IMAGE, "-c", "arm-none-eabi-c++filt"
    ]
    try:
        out = run(cmd, input=batch, timeout=30)
    except Exception as e:
        print(f"ERROR: docker c++filt failed: {e}", file=sys.stderr)
        sys.exit(1)

    return {line.strip() for line in out.splitlines() if line.strip()}


KNOWN_SIGNATURE_ALIASES = {
    ('::Draw',    'float*', '_Vector3<float> const&, int'),
    ('::Draw',    'float*', 'Renderer&'),
    ('::PreDraw', 'float*', '_Vector3<float> const&'),
}


def method_key(demangled: str) -> str | None:
    m = re.match(r'^(?:.*\s+)?(\w+(?:::[\w~]+)+)\s*\(', demangled)
    return m.group(1) if m else None


def _arm32_normalize(params: str) -> str:
    """Canonicalize ARM32-equivalent types and C++ surface variations."""
    # ARM32 integer sizes: int=long=unsigned=4 bytes
    for pat, repl in [
        (r'\bunsigned long long\b', 'uint64'),
        (r'\blong long\b', 'int64'),
        (r'\bunsigned long\b', 'uint32'),
        (r'\bunsigned int\b', 'uint32'),
        (r'\bunsigned short\b', 'uint16'),
        (r'\blong\b', 'int32'),
        (r'\bint\b', 'int32'),
        # Collapse std::map allocator/comparator noise
        (r'(std::map<[^,]+),\s*std::less[^>]+>,\s*std::allocator[^>]+>>', r'\1>'),
    ]:
    # NOTE: const and const& are NOT normalized — they produce real mangling
    # differences. They are ABI-identical on ARM32 (both pass a pointer) but
    # should be fixed in the port to match binary signatures.
        params = re.sub(pat, repl, params)
    params = re.sub(r'\s+', '', params)  # collapse all whitespace
    return params


def param_sig(demangled: str) -> str | None:
    m = re.search(r'\((.*)\)', demangled)
    return _arm32_normalize(m.group(1)) if m else None


def _is_known_alias(method: str, bin_params: str, port_params: str) -> bool:
    for suffix, bin_pat, port_pat in KNOWN_SIGNATURE_ALIASES:
        if method.endswith(suffix) and bin_pat in bin_params and port_pat in port_params:
            return True
    return False


def main():
    print("Extracting binary symbols...", file=sys.stderr)
    binary = extract_binary_demangled()
    print(f"  {len(binary)} binary text symbols", file=sys.stderr)

    print("Extracting port symbols...", file=sys.stderr)
    port = extract_port_demangled()
    print(f"  {len(port)} port text symbols", file=sys.stderr)

    port_keys: dict[str, list[str]] = defaultdict(list)
    for s in port:
        k = method_key(s)
        if k:
            port_keys[k].append(s)

    mismatches = []
    filtered_count = 0
    binary_keys = set()

    # Build binary-key -> params list index
    binary_by_key: dict[str, list[str]] = defaultdict(list)
    for s in binary:
        k = method_key(s)
        if k:
            bp = param_sig(s) or "?"
            binary_by_key[k].append(bp)
            binary_keys.add(k)

    # Stable overload comparison: sort both sides by (param_count, alphabetical),
    # then 1:1 compare. Unpaired leftovers become mismatches.
    for k in binary_keys & set(port_keys.keys()):
        b_sigs = sorted(binary_by_key[k], key=lambda p: (p.count(',') + 1, p))
        p_sigs = sorted([param_sig(ps) or "?" for ps in port_keys[k]],
                        key=lambda p: (p.count(',') + 1, p))

        for i in range(max(len(b_sigs), len(p_sigs))):
            bp = b_sigs[i] if i < len(b_sigs) else "?"
            pp = p_sigs[i] if i < len(p_sigs) else "?"
            if bp != pp:
                if _is_known_alias(k, bp, pp):
                    filtered_count += 1
                    continue
                # Only flag if it's a real difference (not just missing on one side
                # due to different overload count — those go through second pass)
                if bp != "?" and pp != "?":
                    mismatches.append((k, bp, pp))

    # Second pass: unmatched overloads (different counts on either side)
    for k in binary_keys & set(port_keys.keys()):
        b_count = len(binary_by_key[k])
        p_count = len(port_keys[k])
        if b_count != p_count:
            # Already flagged any pairwise mismatches above.
            # Flag the unpaired extras.
            b_sigs = sorted(binary_by_key[k], key=lambda p: (p.count(',') + 1, p))
            p_sigs = sorted([param_sig(ps) or "?" for ps in port_keys[k]],
                            key=lambda p: (p.count(',') + 1, p))
            extra = b_sigs[min(b_count, p_count):] + p_sigs[min(b_count, p_count):]
            for ep in extra:
                if not any(k == mk and ep == mp for mk, mp, _ in mismatches):
                    mismatches.append((k, f"binary:{b_count}", f"port:{p_count}"))

    # Group by class
    by_class: dict[str, list] = defaultdict(list)
    for key, b_params, p_params in mismatches:
        cls = key.rsplit('::', 1)[0] if '::' in key else key
        by_class[cls].append((key, b_params, p_params))

    # Write report
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    with open(OUTPUT, 'w', encoding='utf-8') as f:
        f.write("# Signature Mismatches: Binary vs Port\n\n")
        f.write(f"- Binary symbols: **{len(binary)}**\n")
        f.write(f"- Port symbols: **{len(port)}**\n")
        f.write(f"- Signature mismatches: **{len(mismatches)}**\n")
        f.write(f"- Filtered (known intentional): **{filtered_count}**\n\n")
        f.write("Functions where Class::Method names match but parameter types differ.\n")
        f.write("These are INVISIBLE to asm-verify (different mangled names).\n")
        f.write("Filtered entries are known // Port-specific: refactorings.\n\n")

        for cls in sorted(by_class):
            items = by_class[cls]
            f.write(f"## {cls} ({len(items)} mismatch{'es' if len(items) > 1 else ''})\n\n")
            f.write("| Method | Binary params | Port params |\n")
            f.write("|--------|--------------|-------------|\n")
            for method, bp, pp in sorted(items):
                f.write(f"| `{method}` | `{bp[:80]}` | `{pp[:80]}` |\n")
            f.write("\n")

    print(f"\nFound {len(mismatches)} signature mismatches in {len(by_class)} classes "
          f"({filtered_count} filtered as known intentional)")
    for cls in sorted(by_class)[:15]:
        print(f"  {cls}: {len(by_class[cls])} mismatch(es)")
    print(f"\nFull report: {OUTPUT}")


if __name__ == '__main__':
    main()
