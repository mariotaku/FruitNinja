#!/usr/bin/env python3
"""Detect "same demangled name, different mangled signature" symbol pairs that
the per-symbol asm-verify pipeline SILENTLY SKIPS.

run.sh pairs binary<->port on EXACT MANGLED name. A function whose
fully-qualified demangled name (Namespace::Class::Method) matches on BOTH sides
but whose MANGLED name differs (different param types/count/const/ref => a
different ABI signature) is NEVER paired and never diffed -- the port's
signature can diverge invisibly (cf. GameExit_Handler vs GameExit).

Unlike checks/check-signatures.py (which reads the port side from report.json,
i.e. only the ALREADY-PAIRED symbols), this reads the FULL port symbol set from
a cross-build nm dump (tools/asm-verify/dump-port-nm.sh) so it can see the
unpaired overloads -- which is exactly where the silent divergence lives.

Inputs (all under tmp/asm-verify/, produced by the orchestrator):
  --binary-json  binary-func-symbols.json  [{mangled,addr,size}]  (LIEF, STT_FUNC)
  --port-nm      port-nm.txt               (nm --print-size --defined-only dump)
  --demangled    demangle-map.tsv          (mangled<TAB>demangled, c++filt)

Output:
  tmp/asm-verify/signature-mismatch.json   (machine-readable array)
  ranked markdown summary -> stdout

Step 0 (--emit-mangled-list FILE): write the unique mangled names that need
demangling, so the caller can run c++filt once and feed back --demangled.
"""
import argparse
import json
import pathlib
import re
import sys
from collections import defaultdict

PROJECT = pathlib.Path(__file__).resolve().parent.parent.parent
TMP = PROJECT / "tmp" / "asm-verify"


# ---- port nm parsing -------------------------------------------------------

def parse_port_nm(path):
    """Return {mangled: set(obj)} for defined text symbols (T/t/W/w)."""
    out = defaultdict(set)
    cur_obj = "?"
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("## OBJ "):
            cur_obj = line[len("## OBJ "):].strip()
            continue
        parts = line.split()
        if len(parts) < 3:
            continue
        # "ADDR SIZE TYPE NAME"  or  "ADDR TYPE NAME"
        if len(parts) >= 4:
            type_, name = parts[2], parts[3]
        else:
            type_, name = parts[1], parts[2]
        if type_ not in ("T", "t", "W", "w"):
            continue
        if not name.startswith("_Z"):
            continue
        out[name].add(cur_obj)
    return out


# ---- demangled-name -> base identity --------------------------------------

CLONE_RE = re.compile(r"\s*\[clone[^\]]*\]\s*$")
ABITAG_RE = re.compile(r"\[abi:[^\]]*\]")


def find_signature_paren(s):
    """Index of the '(' that opens the function parameter list, angle/bracket
    aware. Returns -1 if there is no top-level param list (data-like)."""
    angle = 0
    i = 0
    n = len(s)
    while i < n:
        c = s[i]
        if c == "<":
            angle += 1
        elif c == ">":
            if angle:
                angle -= 1
        elif c == "(" and angle == 0:
            # skip operator() / operator()( and conversion noise: a '(' that is
            # part of "operator()" is immediately preceded by "operator".
            if s[max(0, i - 8):i].endswith("operator"):
                # operator() -- the REAL param list is the next top-level '('
                i += 1
                continue
            return i
        i += 1
    return -1


def strip_return_type(name_part):
    """Drop a leading return type if present (templates/thunks show one).
    Most class members have none. Conservative: only strip when a top-level
    space exists that is not inside <> or ()."""
    angle = 0
    paren = 0
    last_space = -1
    for i, c in enumerate(name_part):
        if c == "<":
            angle += 1
        elif c == ">":
            angle = max(0, angle - 1)
        elif c == "(":
            paren += 1
        elif c == ")":
            paren = max(0, paren - 1)
        elif c == " " and angle == 0 and paren == 0:
            last_space = i
    if last_space < 0:
        return name_part
    head = name_part[:last_space]
    tail = name_part[last_space + 1:]
    # Don't strip "operator new"/"operator delete" style (the space is internal
    # to the name) or when the tail has no '::' (likely a plain free fn whose
    # "return type" is actually nothing -- keep as is).
    if "operator" in head:
        return name_part
    return tail if tail else name_part


def base_identity(demangled):
    """Fully-qualified name without the parameter list / cv-ref qualifiers."""
    s = ABITAG_RE.sub("", demangled)
    s = CLONE_RE.sub("", s).strip()
    p = find_signature_paren(s)
    name_part = s[:p].rstrip() if p >= 0 else s
    return strip_return_type(name_part).strip()


# ---- ranking ---------------------------------------------------------------

LOW_MARKERS = ("std::", "__gnu_cxx::", "__cxxabiv1::", "operator new",
               "operator delete", "__gnu_debug")


def is_thunk(dem):
    return dem.startswith(("non-virtual thunk to ", "virtual thunk to ",
                           "construction vtable", "VTT for", "vtable for",
                           "typeinfo"))


# Established // Port-specific: refactors -- the engine threads a Renderer&/
# float* matrix where the binary used a global. Same as check-signatures.py's
# KNOWN_SIGNATURE_ALIASES. Real-but-intentional, so they rank LOW.
KNOWN_DRAW_PORTS = ("Renderer&", "float*", "_Vector3<float> const&")


def _clean(dem):
    return ABITAG_RE.sub("", CLONE_RE.sub("", dem)).strip()


def split_params(dem):
    """Return (inner_param_str, trailing_qualifiers) or (None, '')."""
    s = _clean(dem)
    p = find_signature_paren(s)
    if p < 0:
        return None, ""
    depth = 0
    for i in range(p, len(s)):
        if s[i] == "(":
            depth += 1
        elif s[i] == ")":
            depth -= 1
            if depth == 0:
                return s[p + 1:i], s[i + 1:].strip()
    return s[p + 1:], ""


def _top_level_commas(inner):
    """Split a param list on top-level commas (angle/paren aware)."""
    out, depth, cur = [], 0, ""
    for c in inner:
        if c in "<([":
            depth += 1
        elif c in ">)]":
            depth = max(0, depth - 1)
        if c == "," and depth == 0:
            out.append(cur.strip()); cur = ""
        else:
            cur += c
    if cur.strip():
        out.append(cur.strip())
    return out


# ARM32: int==long==unsigned==4 bytes; unify so width-equal types compare equal.
_WIDTH = [
    (r"\bunsigned long long\b", "u64"), (r"\blong long\b", "i64"),
    (r"\bunsigned long\b", "u32"), (r"\bunsigned int\b", "u32"),
    (r"\bunsigned short\b", "u16"), (r"\bshort unsigned int\b", "u16"),
    (r"\blong\b", "i32"), (r"\bint\b", "i32"),
]


def _norm_type(t, strip_constref):
    for pat, rep in _WIDTH:
        t = re.sub(pat, rep, t)
    if strip_constref:
        t = re.sub(r"\bconst\b", "", t)
        t = t.replace("&&", "").replace("&", "")
    t = re.sub(r"\s+", "", t)
    return t


def rank_finding(base, bin_dems, port_dems):
    """HIGH / MED / LOW likelihood of being a real port bug."""
    alldem = bin_dems + port_dems
    if any(is_thunk(d) for d in alldem):
        return "LOW", "thunk/RTTI"
    if any(m in base for m in LOW_MARKERS):
        return "LOW", "std/compiler helper"
    if "<" in base:
        return "LOW", "templated instantiation"
    is_member = base.count("::") >= 1

    if len(bin_dems) == 1 and len(port_dems) == 1:
        ib, qb = split_params(bin_dems[0])
        ip, qp = split_params(port_dems[0])
        pb = _top_level_commas(ib) if ib is not None else []
        pp = _top_level_commas(ip) if ip is not None else []
        # known Draw(Renderer&)/PreDraw(float*) port refactor
        meth = base.rsplit("::", 1)[-1]
        if meth in ("Draw", "PreDraw", "NewDraw", "DrawSlice") and not pb and \
                any(k in (ip or "") for k in KNOWN_DRAW_PORTS):
            return "LOW", "known port Draw(Renderer&/float*) refactor"
        # const/ref-only drift: same underlying types, differ only by const/&
        loose_b = "|".join(_norm_type(t, True) for t in pb)
        loose_p = "|".join(_norm_type(t, True) for t in pp)
        tight_b = "|".join(_norm_type(t, False) for t in pb) + "@" + qb
        tight_p = "|".join(_norm_type(t, False) for t in pp) + "@" + qp
        if tight_b == tight_p:
            return "LOW", "width-equal only (ARM32-identical)"
        if loose_b == loose_p:
            return "MED", "const/ref/qualifier-only (ABI-identical on ARM32)"
        if len(pb) != len(pp):
            # port grew a leading own-class pointer => static-vs-member shape
            own = base.rsplit("::", 1)[0].rsplit("::", 1)[-1] if is_member else ""
            if pp and own and pp[0].rstrip(" *") == own and len(pp) == len(pb) + 1:
                return "HIGH", "static-vs-member (port adds explicit this*)"
            return "HIGH", "param COUNT differs (added/dropped arg)"
        return "HIGH", "param TYPE differs"

    if is_member:
        return "MED", "member fn, multiple overloads"
    return "MED", "free function"


def params_of(dem):
    inner, qual = split_params(dem)
    if inner is None:
        return ""
    return "(" + inner + ")" + (" " + qual if qual else "")


RANK_ORDER = {"HIGH": 0, "MED": 1, "LOW": 2}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary-json", default=str(TMP / "binary-func-symbols.json"))
    ap.add_argument("--port-nm", default=str(TMP / "port-nm.txt"))
    ap.add_argument("--demangled", default=str(TMP / "demangle-map.tsv"))
    ap.add_argument("--out", default=str(TMP / "signature-mismatch.json"))
    ap.add_argument("--emit-mangled-list", default=None,
                    help="Write unique mangled names needing demangle, then exit.")
    args = ap.parse_args()

    bin_syms = json.loads(pathlib.Path(args.binary_json).read_text())
    bin_by_mangled = {}
    for s in bin_syms:
        bin_by_mangled.setdefault(s["mangled"], s)  # first wins
    port_objs = parse_port_nm(pathlib.Path(args.port_nm))

    if args.emit_mangled_list:
        names = sorted(set(bin_by_mangled) | set(port_objs))
        pathlib.Path(args.emit_mangled_list).write_text("\n".join(names) + "\n")
        print(f"wrote {len(names)} mangled names -> {args.emit_mangled_list}")
        return 0

    # demangle map
    dem = {}
    # Read RAW bytes (not read_text): universal-newline mode would translate a
    # bare \r mid-line into \n. The map can contain MANGLED\r\tDEMANGLED\r\n
    # (host CRLF leaked through `paste`), so we split on \n and strip stray \r.
    raw = pathlib.Path(args.demangled).read_bytes().decode("utf-8", "replace")
    for line in raw.split("\n"):
        if "\t" not in line:
            continue
        m, d = line.split("\t", 1)
        dem[m.strip()] = d.strip()

    def demof(m):
        return dem.get(m, m)

    # base identity -> mangled sets per side
    bin_base = defaultdict(set)
    port_base = defaultdict(set)
    for m in bin_by_mangled:
        bin_base[base_identity(demof(m))].add(m)
    for m in port_objs:
        port_base[base_identity(demof(m))].add(m)

    common_bases = set(bin_base) & set(port_base)

    findings = []
    n_paired_exact = 0
    n_partial = 0
    for base in common_bases:
        if not base:
            continue
        Mb = bin_base[base]
        Mp = port_base[base]
        inter = Mb & Mp
        if inter and Mb == Mp:
            n_paired_exact += 1
            continue  # fully paired, asm-verify sees it
        # base has at least one unpaired overload on some side
        if inter:
            n_partial += 1
            kind = "partial (some overloads paired, some not)"
        else:
            kind = "fully-unpaired (base never diffed)"
        # only report ones with BOTH a binary-side and port-side UNPAIRED member
        bin_unpaired = sorted(Mb - inter)
        port_unpaired = sorted(Mp - inter)
        if not bin_unpaired or not port_unpaired:
            # e.g. extra port overload with no binary counterpart, or vice versa
            # -> not a binary<->port signature divergence of the SAME identity
            continue
        bin_dems = [demof(m) for m in bin_unpaired]
        port_dems = [demof(m) for m in port_unpaired]
        rank, why = rank_finding(base, bin_dems, port_dems)
        objs = sorted({o for m in port_unpaired for o in port_objs[m]})
        findings.append({
            "base_identity": base,
            "binary_mangled": bin_unpaired,
            "binary_demangled": bin_dems,
            "binary_params": [params_of(d) for d in bin_dems],
            "port_mangled": port_unpaired,
            "port_demangled": port_dems,
            "port_params": [params_of(d) for d in port_dems],
            "binary_addr": "0x%08x" % bin_by_mangled[bin_unpaired[0]]["addr"],
            "port_objs": objs,
            "rank": rank,
            "kind": kind,
            "note": why,
        })

    findings.sort(key=lambda f: (RANK_ORDER[f["rank"]],
                                 "<" in f["base_identity"],
                                 f["base_identity"]))

    pathlib.Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    pathlib.Path(args.out).write_text(json.dumps(findings, indent=2))

    n_high = sum(1 for f in findings if f["rank"] == "HIGH")
    n_med = sum(1 for f in findings if f["rank"] == "MED")
    n_low = sum(1 for f in findings if f["rank"] == "LOW")

    print(f"# Signature-mismatch (demangled-name matches, mangled differs -> "
          f"silently UNPAIRED by run.sh)\n")
    print(f"binary FUNC symbols: {len(bin_by_mangled)}   "
          f"port text symbols: {len(port_objs)}")
    print(f"fully-paired bases (asm-verify sees these): {n_paired_exact}")
    print(f"SIGNATURE-MISMATCH findings: {len(findings)}  "
          f"(HIGH={n_high} MED={n_med} LOW={n_low})\n")
    print("| # | rank | base identity | binary params | port params | note |")
    print("|---|------|---------------|---------------|-------------|------|")
    for i, f in enumerate(findings[:30], 1):
        bp = " ; ".join(f["binary_params"])[:46]
        pp = " ; ".join(f["port_params"])[:46]
        print(f"| {i} | {f['rank']} | `{f['base_identity'][:48]}` | "
              f"`{bp}` | `{pp}` | {f['note']} |")
    print(f"\nFull JSON: {pathlib.Path(args.out).relative_to(PROJECT).as_posix()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
