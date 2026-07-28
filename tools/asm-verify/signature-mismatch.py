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

Input (preferred, and what run.sh uses):
  --symbol-index tmp/asm-verify/symbol-index.json
      Written by discover-symbols.py during the in-container sweep. Carries the
      binary symbol set, the FULL cross-build port symbol set (not just the
      paired ones), and the c++filt demangle map -- so this script needs no
      cross toolchain and runs host-side next to the other run.sh checks.

Legacy inputs (kept for ad-hoc runs against hand-made dumps):
  --binary-json  binary-func-symbols.json  [{mangled,addr,size}]
  --port-nm      port-nm.txt               (nm --print-size --defined-only dump)
  --demangled    demangle-map.tsv          (mangled<TAB>demangled, c++filt)

Output:
  tmp/asm-verify/signature-mismatch.json   (machine-readable array)
  ranked markdown summary -> stdout

Write-back (--write-back): append a `port_mangled =` alias to
tools/asm-verify/manifest.toml for the unambiguous 1:1 findings, which is what
actually turns a finding into a diffed symbol on the next sweep. Aliasing is
preferred over renaming port code: it is a few TOML lines, causes no ABI churn,
and states the honest assertion "this port symbol IS the port of that binary
symbol". Rename port code only where the PORT name is wrong (e.g. the port
"corrected" the binary's typo CheckHasGoneOffsceen).

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

# ---- liveness ---------------------------------------------------------------
# Same vocabulary as the pairing-gap bucketing: a finding on dead code hides no
# live divergence, however large it is.
DEFUNCT_MARKERS = (
    "OpenFeint", "GameCenter", "GameSpy", "Leaderboard", "FNHighscore",
    "Highscore", "NetworkManager", "NetworkPacket", "PacketFactory", "P2P",
    "ProfanityFilter", "DRMManager", "Licensing", "NewsControl",
    "NewsRenderer", "SocialNetwork", "Facebook", "Twitter",
)
PLATFORM_MARKERS = (
    "Osp::", "Bada", "MAMAudio", "GlesForm", "MortarAudioMixer", "OspMain",
    "Tizen", "AppUiApp", "MortarApp",
)
THIRD_PARTY_MARKERS = ("TiXml", "tinyxml", "zlib", "png_", "jpeg_")
NOISE_MARKERS = ("std::", "__gnu_cxx::", "__cxxabiv1::", "operator new",
                 "operator delete", "__gnu_debug")


def liveness_of(base, dems):
    """LIVE / DEFUNCT / PLATFORM / THIRD-PARTY / NOISE for a base identity."""
    if any(is_thunk(d) for d in dems):
        return "NOISE"
    for marker_set, label in ((NOISE_MARKERS, "NOISE"),
                              (DEFUNCT_MARKERS, "DEFUNCT"),
                              (PLATFORM_MARKERS, "PLATFORM"),
                              (THIRD_PARTY_MARKERS, "THIRD-PARTY")):
        if any(m in base for m in marker_set):
            return label
    return "LIVE"


# Byte thresholds for the size axis. A 4-byte stub that never pairs hides
# nothing; a 968-byte per-frame Draw hides a lot.
SIZE_HIGH = 256
SIZE_MED = 64


def rank_by_impact(base, dems, live, nbytes, actionable):
    """Final rank = how much LIVE, UNDIFFED code this finding hides.

    Deliberately NOT the signature SHAPE. Shape answers "why is it unpaired",
    which the old ranking conflated with "does it matter" -- and that is how
    Fruit::Draw (live, per-frame, 968 bytes, unpaired only because of the
    known Draw(Renderer&) port refactor) sat at LOW. A well-understood cause is
    a reason the finding is CHEAP TO FIX, not a reason to ignore it.
    """
    if live != "LIVE":
        return "LOW", "not live (%s)" % live.lower()
    if not actionable:
        return "MED", "ambiguous (multiple unpaired overloads on a side)"
    if "<" in base:
        # Template instantiation: real, but weak/inline bodies dominate and
        # they pair in bulk once a TU is added. Cap at MED.
        return ("MED" if nbytes >= SIZE_HIGH else "LOW"), \
               "template instantiation, %d B" % nbytes
    if nbytes >= SIZE_HIGH:
        return "HIGH", "live, %d B undiffed" % nbytes
    if nbytes >= SIZE_MED:
        return "MED", "live, %d B undiffed" % nbytes
    return "LOW", "live but only %d B" % nbytes


# ---- write-back -------------------------------------------------------------

WRITE_BACK_HEADER = """
# ---------------------------------------------------------------------------
# signature-mismatch write-back (tools/asm-verify/signature-mismatch.py
# --write-back). Each entry below is a base identity that exists on BOTH sides
# with the SAME fully-qualified name but a different mangled signature, so the
# exact-mangled-name intersection in discover-symbols.py never paired it and
# asm-verify never diffed it. The alias states that the port symbol IS the port
# of the binary symbol. Re-check any entry you doubt and delete it -- an alias
# onto an UNPORTED body would hide a real gap behind a fake pairing.
# ---------------------------------------------------------------------------
"""


def existing_manifest_mangled(path):
    if not path.exists():
        return set()
    return set(re.findall(r'^\s*mangled\s*=\s*"(.*)"\s*$',
                          path.read_text(encoding="utf-8"), re.M))


def manifest_aliases(path):
    """{binary_mangled: port_mangled} already declared in manifest.toml.

    Symbols with an alias ARE paired and diffed by the sweep, so they must
    drop out of this report -- otherwise every fix stays on the list forever
    and the guard stops distinguishing "still blind" from "already handled".
    """
    if not path.exists():
        return {}
    txt = path.read_text(encoding="utf-8")
    out = {}
    cur = None
    for line in txt.splitlines():
        m = re.match(r'^\s*mangled\s*=\s*"(.*)"\s*$', line)
        if m:
            cur = m.group(1)
            continue
        m = re.match(r'^\s*port_mangled\s*=\s*"(.*)"\s*$', line)
        if m and cur:
            out[cur] = m.group(1)
    return out


def write_back(findings, manifest_path):
    """Append `port_mangled` aliases for the unambiguous LIVE 1:1 findings."""
    have = existing_manifest_mangled(manifest_path)
    out = []
    for f in findings:
        if not f["alias_candidate"]:
            continue
        b = f["binary_mangled"][0]
        if b in have:
            continue
        have.add(b)
        out.append(f)
    if not out:
        return []
    lines = [WRITE_BACK_HEADER]
    for f in out:
        lines.append("# %s @%s (%d B)  [%s]" % (
            f["base_identity"], f["binary_addr"], f["binary_bytes"], f["rank"]))
        lines.append("#   binary: %s" % f["binary_demangled"][0])
        lines.append("#   port:   %s   [%s]" % (
            f["port_demangled"][0], ", ".join(f["port_objs"]) or "?"))
        lines.append("[[symbol]]")
        lines.append('mangled      = "%s"' % f["binary_mangled"][0])
        lines.append('port_mangled = "%s"' % f["port_mangled"][0])
        lines.append('notes        = "signature-mismatch write-back: same '
                     'qualified name, port signature %s"'
                     % f["port_params"][0].replace('"', "'"))
        lines.append("")
    with manifest_path.open("a", encoding="utf-8") as fh:
        fh.write("\n".join(lines))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--symbol-index", default=str(TMP / "symbol-index.json"),
                    help="Preferred input: written by discover-symbols.py.")
    ap.add_argument("--binary-json", default=str(TMP / "binary-func-symbols.json"))
    ap.add_argument("--port-nm", default=str(TMP / "port-nm.txt"))
    ap.add_argument("--demangled", default=str(TMP / "demangle-map.tsv"))
    ap.add_argument("--out", default=str(TMP / "signature-mismatch.json"))
    ap.add_argument("--top", type=int, default=30,
                    help="Rows in the stdout summary (default 30).")
    ap.add_argument("--write-back", action="store_true",
                    help="Append port_mangled aliases for the unambiguous LIVE "
                         "1:1 findings to tools/asm-verify/manifest.toml.")
    ap.add_argument("--manifest", default=str(PROJECT / "tools" / "asm-verify" /
                                              "manifest.toml"))
    ap.add_argument("--emit-mangled-list", default=None,
                    help="Write unique mangled names needing demangle, then exit.")
    args = ap.parse_args()

    dem = {}
    index_path = pathlib.Path(args.symbol_index)
    if index_path.exists():
        idx = json.loads(index_path.read_text())
        bin_by_mangled = {}
        for s in idx["binary"]:
            bin_by_mangled.setdefault(s["mangled"], s)
        port_objs = {m: {o} for m, o in idx["port"].items()}
        dem = idx.get("demangled", {})
    else:
        print(f"NOTE: {index_path} missing -- falling back to the legacy "
              f"binary-json/port-nm/demangled trio.", file=sys.stderr)
        bin_syms = json.loads(pathlib.Path(args.binary_json).read_text())
        bin_by_mangled = {}
        for s in bin_syms:
            bin_by_mangled.setdefault(s["mangled"], s)  # first wins
        port_objs = parse_port_nm(pathlib.Path(args.port_nm))
        # Read RAW bytes (not read_text): universal-newline mode would translate
        # a bare \r mid-line into \n. The map can contain MANGLED\r\tDEMANGLED\r
        # \n (host CRLF leaked through `paste`), so split on \n and strip \r.
        raw = pathlib.Path(args.demangled).read_bytes().decode("utf-8", "replace")
        for line in raw.split("\n"):
            if "\t" not in line:
                continue
            m, d = line.split("\t", 1)
            dem[m.strip()] = d.strip()

    if args.emit_mangled_list:
        names = sorted(set(bin_by_mangled) | set(port_objs))
        pathlib.Path(args.emit_mangled_list).write_text("\n".join(names) + "\n")
        print(f"wrote {len(names)} mangled names -> {args.emit_mangled_list}")
        return 0

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

    # A binary symbol with a manifest `port_mangled` alias is already paired
    # and diffed -- treat the alias exactly like an exact-name match so fixed
    # findings leave the report.
    aliases = manifest_aliases(pathlib.Path(args.manifest))
    aliased = {b for b, p in aliases.items() if p in port_objs}

    findings = []
    n_paired_exact = 0
    n_partial = 0
    n_aliased = 0
    for base in common_bases:
        if not base:
            continue
        Mb = bin_base[base]
        Mp = port_base[base]
        inter = (Mb & Mp) | (Mb & aliased)
        if Mb & aliased:
            n_aliased += len(Mb & aliased)
            # The port side of an alias is spoken for too.
            Mp = Mp - {aliases[b] for b in (Mb & aliased)}
        if inter and Mb <= inter and not Mp - inter:
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
        shape, shape_note = rank_finding(base, bin_dems, port_dems)
        objs = sorted({o for m in port_unpaired for o in port_objs[m]})
        nbytes = max(bin_by_mangled[m].get("size", 0) or 0 for m in bin_unpaired)
        live = liveness_of(base, bin_dems + port_dems)
        actionable = len(bin_unpaired) == 1 and len(port_unpaired) == 1
        rank, why = rank_by_impact(base, bin_dems + port_dems, live, nbytes,
                                   actionable)
        findings.append({
            "base_identity": base,
            "binary_mangled": bin_unpaired,
            "binary_demangled": bin_dems,
            "binary_params": [params_of(d) for d in bin_dems],
            "port_mangled": port_unpaired,
            "port_demangled": port_dems,
            "port_params": [params_of(d) for d in port_dems],
            "binary_addr": "0x%08x" % bin_by_mangled[bin_unpaired[0]]["addr"],
            "binary_bytes": nbytes,
            "port_objs": objs,
            "rank": rank,
            "kind": kind,
            "note": why,
            "liveness": live,
            # Kept so nothing the old ranking knew is lost -- it is the "why is
            # this unpaired" axis, which is orthogonal to "does it matter".
            "shape": shape,
            "shape_note": shape_note,
            # An alias asserts "this port symbol IS the port of that binary
            # symbol", so only propose it where the two really are the same
            # body under a different signature: a threaded-in extra argument
            # (the established Draw(Renderer&) / Render(mtx&) port refactor),
            # a const/ref/width-only difference, or a namespace move.
            # "param TYPE differs" at equal arity is excluded -- unrelated
            # types at the same position mean the port function may be a
            # DIFFERENT function, and aliasing would hide a real gap behind a
            # fake pairing.
            "alias_candidate": bool(actionable and live == "LIVE"
                                    and "<" not in base
                                    and shape_note != "param TYPE differs"),
        })

    # Rank first, then biggest-undiffed-body first inside a rank.
    findings.sort(key=lambda f: (RANK_ORDER[f["rank"]], -f["binary_bytes"],
                                 f["base_identity"]))

    pathlib.Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    pathlib.Path(args.out).write_text(json.dumps(findings, indent=2))

    n_high = sum(1 for f in findings if f["rank"] == "HIGH")
    n_med = sum(1 for f in findings if f["rank"] == "MED")
    n_low = sum(1 for f in findings if f["rank"] == "LOW")

    cands = [f for f in findings if f["alias_candidate"]]
    live_bytes = sum(f["binary_bytes"] for f in findings
                     if f["liveness"] == "LIVE")

    print(f"# Signature-mismatch (demangled-name matches, mangled differs -> "
          f"silently UNPAIRED by run.sh)\n")
    print(f"binary FUNC symbols: {len(bin_by_mangled)}   "
          f"port text symbols: {len(port_objs)}")
    print(f"fully-paired bases (asm-verify sees these): {n_paired_exact}"
          f"   [{n_aliased} via manifest port_mangled aliases]")
    print(f"SIGNATURE-MISMATCH findings: {len(findings)}  "
          f"(HIGH={n_high} MED={n_med} LOW={n_low})  "
          f"live undiffed bytes: {live_bytes}")
    print(f"alias candidates (1:1, live): {len(cands)}"
          + ("   -- rerun with --write-back to apply"
             if cands and not args.write_back else ""))
    print()
    print("| # | rank | bytes | base identity | binary params | port params | why |")
    print("|---|------|-------|---------------|---------------|-------------|-----|")
    for i, f in enumerate(findings[:args.top], 1):
        bp = " ; ".join(f["binary_params"])[:40]
        pp = " ; ".join(f["port_params"])[:40]
        print(f"| {i} | {f['rank']} | {f['binary_bytes']} | "
              f"`{f['base_identity'][:44]}` | "
              f"`{bp}` | `{pp}` | {f['note']} |")

    if args.write_back:
        applied = write_back(cands, pathlib.Path(args.manifest))
        print(f"\nwrite-back: appended {len(applied)} port_mangled aliases to "
              f"{pathlib.Path(args.manifest).relative_to(PROJECT).as_posix()} "
              f"({len(cands) - len(applied)} already present).")
        for f in applied:
            print(f"  + {f['base_identity']}  ({f['binary_bytes']} B)")

    try:
        out_disp = pathlib.Path(args.out).relative_to(PROJECT).as_posix()
    except ValueError:
        out_disp = args.out
    print(f"\nFull JSON: {out_disp}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
