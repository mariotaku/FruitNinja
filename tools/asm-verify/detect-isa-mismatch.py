#!/usr/bin/env python3
# detect-isa-mismatch.py
#
# ISA BLIND SPOT (#111). The v1.6.1 binary is mixed-ISA: GCC emitted ~94% of it
# as ARM and ~6% as Thumb-2 (7951 `$a` vs 486 `$t` mapping symbols), with
# `___<sym>_veneer` ARM thunks doing the interworking. The cross-build has no
# per-function knob (GCC 4.4.1 rejects both `__attribute__((target("thumb")))`
# and `#pragma GCC target`), so a whole TU is compiled `-marm` by default.
#
# When the binary body is Thumb-2 and the cross-build body is ARM, the two
# instruction streams are INCOMPARABLE BY CONSTRUCTION: different encodings,
# different register-pressure choices, IT blocks vs. predicated ARM,
# `movs/adds/subs` vs `mov/add/sub`. Nothing in asm-verify's normalizer absorbs
# an ISA change, so the LCS score collapses toward zero *even for a byte-faithful
# port*. The resulting number is neither a pass nor a fail -- it is noise, and it
# currently ranks alongside real divergences.
#
# Calibration case: `SystemManager::Update` scores 23.5% LCS purely from this.
# Every field offset matches src/engine/core/SystemManager.h exactly. Somebody
# could burn hours "fixing" a non-bug.
#
# WHAT THIS DOES
#   1. Reads the binary's per-symbol ISA (see "Detection method" below).
#   2. Reads the port-side per-TU ISA: the cross-build default (-marm, or
#      $FN_ARM_MODE) plus the per-TU overrides in isa-overrides.cmake.
#   3. Emits tmp/asm-verify/isa-modes.json -- the machine-readable product.
#      classify-divergences.py reads it and tags affected rows
#      `cause = "isa-mismatch"` (LOW) + `isa_mismatch = true`, so they rank as
#      noise instead of as high-ratio divergences. Absent file => feature off =>
#      the report is bit-identical to before.
#   4. Reports which TUs are HOMOGENEOUSLY Thumb, i.e. which could be flipped to
#      -mthumb in isa-overrides.cmake and actually VERIFIED instead of excluded.
#   5. Cross-references tools/asm-verify/triage.json: a sticky ACCEPT / FIX-NEEDED
#      reached against an ISA-mismatched score was reached against noise and is
#      listed for re-triage (`--flag-triage` writes the flag back into the
#      record; default is report-only).
#
# DETECTION METHOD -- low bit of st_value, cross-checked against $a/$t.
#   Two signals exist. The ARM ELF ABI sets the low bit of a FUNC symbol's
#   st_value for a Thumb entry point, and the `$a`/`$t` mapping symbols partition
#   .text into ARM and Thumb runs.
#   We use the LOW BIT as primary because it is a direct per-symbol property --
#   no address arithmetic, no ordering assumption, no interaction with the 11988
#   interleaved `$d` literal-pool markers -- and because asm-verify already keys
#   every row on the mangled name, which is exactly the low bit's key.
#   The mapping symbols are computed too and used purely as a cross-check
#   (`cross_check` in the JSON). Measured: 2517/2517 paired rows AGREE, 0
#   disagreements, so either would do; the low bit is simply cheaper and has no
#   bisect edge case. A disagreement would be reported loudly, never silently.
#
# No third-party imports: a self-contained ELF32 symtab reader, so this runs
# host-side or in-container with a bare python3.
#
# Usage:
#   python tools/asm-verify/detect-isa-mismatch.py
#   python tools/asm-verify/detect-isa-mismatch.py --report-json tmp/asm-verify/report.scoped.json
#   python tools/asm-verify/detect-isa-mismatch.py --flag-triage

import argparse
import datetime
import io
import json
import os
import re
import struct
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))

DEFAULT_BINARY = os.environ.get(
    "ASM_VERIFY_BINARY",
    os.path.join(PROJECT_ROOT, "FruitNinjaBada", "Bin", "FruitNinja.exe"))
DEFAULT_REPORT = os.path.join(PROJECT_ROOT, "tmp", "asm-verify", "report.json")
DEFAULT_INDEX = os.path.join(PROJECT_ROOT, "tmp", "asm-verify", "symbol-index.json")
TRIAGE_PATH = os.path.join(SCRIPT_DIR, "triage.json")
OVERRIDES_CMAKE = os.path.join(SCRIPT_DIR, "isa-overrides.cmake")

STT_FUNC = 2
SHT_SYMTAB = 2
SHT_DYNSYM = 11


# ---------------------------------------------------------------------------
# Minimal ELF32 little-endian symbol-table reader (no lief / pyelftools dep)
# ---------------------------------------------------------------------------

def read_elf_symbols(path):
    """Yield (name, st_value, st_info) for every symbol in .symtab + .dynsym."""
    with open(path, "rb") as fh:
        data = fh.read()
    if data[:4] != b"\x7fELF":
        raise ValueError("not an ELF file: {}".format(path))
    if data[4] != 1:
        raise ValueError("expected ELF32: {}".format(path))
    if data[5] != 1:
        raise ValueError("expected little-endian ELF: {}".format(path))

    e_shoff, = struct.unpack_from("<I", data, 0x20)
    e_shentsize, e_shnum = struct.unpack_from("<HH", data, 0x2E)
    if e_shoff == 0 or e_shnum == 0:
        raise ValueError("no section headers (stripped?): {}".format(path))

    sections = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        (_name, sh_type, _flags, _addr, sh_offset, sh_size,
         sh_link, _info, _align, sh_entsize) = struct.unpack_from("<10I", data, off)
        sections.append((sh_type, sh_offset, sh_size, sh_link, sh_entsize))

    for sh_type, sh_offset, sh_size, sh_link, sh_entsize in sections:
        if sh_type not in (SHT_SYMTAB, SHT_DYNSYM):
            continue
        if sh_entsize != 16 or sh_link >= len(sections):
            continue
        _st, str_off, str_size, _l, _e = sections[sh_link]
        strtab = data[str_off:str_off + str_size]
        for k in range(sh_size // 16):
            base = sh_offset + k * 16
            st_name, st_value, _st_size, st_info, _other, _shndx = \
                struct.unpack_from("<IIIBBH", data, base)
            if st_name == 0 or st_name >= len(strtab):
                continue
            end = strtab.find(b"\0", st_name)
            name = strtab[st_name:end].decode("utf-8", "replace")
            if not name:
                continue
            yield name, st_value, st_info


def binary_isa_tables(path):
    """Return (func_mode, map_marks).

    func_mode: {mangled_name: 'THUMB'|'ARM'} from the low bit of st_value on
               STT_FUNC symbols -- the PRIMARY signal.
    map_marks: sorted [(addr, '$a'|'$t')] -- the CROSS-CHECK signal.
    """
    func_mode = {}
    marks = []
    for name, value, info in read_elf_symbols(path):
        if name in ("$a", "$t"):
            marks.append((value, name))
            continue
        if (info & 0xF) == STT_FUNC:
            func_mode.setdefault(name, "THUMB" if (value & 1) else "ARM")
    marks.sort()
    return func_mode, marks


def mapping_mode_lookup(marks):
    """Build addr -> 'THUMB'|'ARM' from the $a/$t partition of .text."""
    import bisect
    addrs = [m[0] for m in marks]
    names = [m[1] for m in marks]

    def mode(addr):
        i = bisect.bisect_right(addrs, addr) - 1
        return "THUMB" if (i >= 0 and names[i] == "$t") else "ARM"
    return mode


# ---------------------------------------------------------------------------
# Port-side ISA: cross-build default + per-TU overrides
# ---------------------------------------------------------------------------

_CMAKE_LIST_RE = re.compile(
    r"set\s*\(\s*(FN_ISA_ARM_SOURCES|FN_ISA_THUMB_SOURCES)([^)]*)\)", re.S)


def port_tu_overrides(cmake_path=OVERRIDES_CMAKE):
    """Parse isa-overrides.cmake -> {'<basename>.cpp': 'ARM'|'THUMB'}.

    Same file the cross-build include()s, so there is exactly one source of
    truth for which TU is compiled in which mode.
    """
    out = {}
    if not os.path.exists(cmake_path):
        return out
    text = io.open(cmake_path, encoding="utf-8").read()
    for var, body in _CMAKE_LIST_RE.findall(text):
        mode = "ARM" if var.endswith("ARM_SOURCES") else "THUMB"
        for raw in body.split():
            raw = raw.strip().strip('"')
            if not raw or raw.startswith("#"):
                continue
            out[os.path.basename(raw)] = mode
    return out


def port_default_mode():
    """Cross-build default ISA. toolchain.cmake: -marm unless FN_ARM_MODE."""
    flag = os.environ.get("FN_ARM_MODE", "-marm").strip()
    return "THUMB" if flag == "-mthumb" else "ARM"


def port_mode_for(obj_name, overrides, default_mode):
    """obj_name is symbol-index.json's '<Foo>.cpp.obj'."""
    if obj_name and obj_name.endswith(".obj"):
        cpp = obj_name[:-4]
        if cpp in overrides:
            return overrides[cpp]
    return default_mode


# ---------------------------------------------------------------------------
# Main analysis
# ---------------------------------------------------------------------------

def load_json(path):
    if not os.path.exists(path):
        return None
    try:
        return json.load(io.open(path, encoding="utf-8"))
    except Exception as exc:                                    # noqa: BLE001
        print("  WARN: cannot read {}: {}".format(path, exc), file=sys.stderr)
        return None


def analyse(binary, report_path, index_path):
    func_mode, marks = binary_isa_tables(binary)
    map_mode = mapping_mode_lookup(marks)

    report = load_json(report_path)
    if report is None:
        raise SystemExit("report missing: {} (run asm-verify first)".format(report_path))
    rows = report.get("symbols", [])

    index = load_json(index_path) or {}
    pmap = index.get("port", {})

    overrides = port_tu_overrides()
    default_mode = port_default_mode()

    agree = disagree = 0
    disagreements = []
    thumb_rows = []
    mismatched = []
    matched_by_override = []
    unknown_binary_mode = 0
    per_tu = {}

    for r in rows:
        mangled = r.get("mangled")
        bmode = func_mode.get(mangled)
        if bmode is None:
            unknown_binary_mode += 1
            continue

        # Cross-check the low bit against the $a/$t partition.
        raw = r.get("raw_addr")
        if raw:
            try:
                mmode = map_mode(int(raw, 16))
                if mmode == bmode:
                    agree += 1
                else:
                    disagree += 1
                    if len(disagreements) < 20:
                        disagreements.append({
                            "mangled": mangled, "raw_addr": raw,
                            "low_bit": bmode, "mapping_symbol": mmode})
            except ValueError:
                pass

        obj = pmap.get(r.get("port_mangled") or mangled) or pmap.get(mangled) or ""
        pmode = port_mode_for(obj, overrides, default_mode)

        tu = per_tu.setdefault(obj or "<unmapped>", {
            "tu": obj or "<unmapped>", "port_mode": pmode,
            "binary_arm": 0, "binary_thumb": 0, "thumb_symbols": []})
        if bmode == "THUMB":
            tu["binary_thumb"] += 1
            tu["thumb_symbols"].append(mangled)
            thumb_rows.append(r)
        else:
            tu["binary_arm"] += 1

        if bmode != pmode:
            mismatched.append(mangled)
        elif bmode == "THUMB":
            matched_by_override.append(mangled)

    # A TU is flip-eligible when every paired row in it is Thumb on the binary
    # side: flipping it to -mthumb recovers those rows at zero cost. A MIXED TU
    # is NOT eligible -- flipping trades its ARM rows for its Thumb ones.
    tu_report = []
    for tu in per_tu.values():
        if tu["binary_thumb"] == 0:
            continue
        tu["homogeneous_thumb"] = tu["binary_arm"] == 0
        tu["flip_verdict"] = (
            "already -mthumb" if tu["port_mode"] == "THUMB" else
            "FLIP-ELIGIBLE (all paired rows are Thumb)" if tu["homogeneous_thumb"] else
            "MIXED -- flipping trades {} ARM rows for {} Thumb rows".format(
                tu["binary_arm"], tu["binary_thumb"]))
        tu_report.append(tu)
    tu_report.sort(key=lambda t: (-t["binary_thumb"], t["tu"]))

    return {
        "generated": datetime.datetime.now(datetime.timezone.utc)
                     .strftime("%Y-%m-%dT%H:%M:%SZ"),
        "binary": os.path.relpath(binary, PROJECT_ROOT).replace(os.sep, "/"),
        "report": os.path.relpath(report_path, PROJECT_ROOT).replace(os.sep, "/"),
        "method": "st_value low bit on STT_FUNC (primary); "
                  "$a/$t mapping symbols (cross-check)",
        "cross_check": {"agree": agree, "disagree": disagree,
                        "disagreements": disagreements},
        "binary_mapping_symbols": {
            "arm": sum(1 for _, n in marks if n == "$a"),
            "thumb": sum(1 for _, n in marks if n == "$t")},
        "port_default_mode": default_mode,
        "port_tu_overrides": overrides,
        "counts": {
            "paired_rows": len(rows),
            "binary_thumb_rows": len(thumb_rows),
            "isa_mismatched_rows": len(mismatched),
            "thumb_rows_mode_matched": len(matched_by_override),
            "rows_without_binary_mode": unknown_binary_mode},
        "mismatched": sorted(mismatched),
        "mode_matched_thumb": sorted(matched_by_override),
        "tu_breakdown": tu_report,
    }


# ---------------------------------------------------------------------------
# triage.json cross-reference
# ---------------------------------------------------------------------------

STICKY = ("ACCEPT-cosmetic", "ACCEPT-deferred", "ACCEPT-defunct", "FIX-NEEDED")


def triage_crossref(mismatched, triage_path=TRIAGE_PATH):
    triage = load_json(triage_path) or {}
    hits = []
    for name in sorted(mismatched):
        entry = triage.get(name)
        if not entry:
            continue
        verdict = entry.get("verdict")
        if verdict not in STICKY:
            continue
        hits.append({
            "mangled": name,
            "verdict": verdict,
            "decided_at": entry.get("decided_at"),
            "reason_head": (entry.get("reason") or "")[:160].replace("\n", " "),
        })
    return hits


RETRIAGE_NOTE = (" [isa-mismatch #111: the binary body is Thumb-2 while the "
                 "cross-build emitted ARM, so the score this verdict was "
                 "reached against is ISA noise -- RE-TRIAGE]")


def flag_triage(hits, triage_path=TRIAGE_PATH):
    """Write `isa_retriage: true` + a note into the affected triage entries."""
    triage = load_json(triage_path)
    if triage is None:
        return 0
    n = 0
    for h in hits:
        entry = triage.get(h["mangled"])
        if not entry or entry.get("isa_retriage"):
            continue
        entry["isa_retriage"] = True
        reason = entry.get("reason") or ""
        if "isa-mismatch #111" not in reason:
            entry["reason"] = reason + RETRIAGE_NOTE
        n += 1
    if n:
        with io.open(triage_path, "w", encoding="utf-8") as fh:
            json.dump(triage, fh, indent=2, sort_keys=True, ensure_ascii=False)
            fh.write("\n")
    return n


# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--binary", default=DEFAULT_BINARY)
    ap.add_argument("--report-json", default=DEFAULT_REPORT)
    ap.add_argument("--symbol-index", default=DEFAULT_INDEX)
    ap.add_argument("--out", default=None,
                    help="default: <report dir>/isa-modes.json")
    ap.add_argument("--flag-triage", action="store_true",
                    help="write isa_retriage=true into affected triage.json entries")
    ap.add_argument("--top", type=int, default=20)
    args = ap.parse_args()

    if not os.path.exists(args.binary):
        print("isa-mismatch: binary missing ({}) -- skipped.".format(args.binary))
        return 0

    data = analyse(args.binary, args.report_json, args.symbol_index)
    hits = triage_crossref(data["mismatched"])
    data["triage_needs_retriage"] = hits

    out = args.out or os.path.join(
        os.path.dirname(os.path.abspath(args.report_json)), "isa-modes.json")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with io.open(out, "w", encoding="utf-8") as fh:
        json.dump(data, fh, indent=2, ensure_ascii=False)
        fh.write("\n")

    c = data["counts"]
    cc = data["cross_check"]
    ms = data["binary_mapping_symbols"]
    print("=" * 78)
    print("ISA mismatch (Thumb-2 binary vs ARM cross-build)  --  #111 blind spot")
    print("=" * 78)
    print("binary is mixed-ISA: {} $a (ARM) / {} $t (Thumb) mapping symbols"
          .format(ms["arm"], ms["thumb"]))
    print("port default mode: {}   per-TU overrides: {}"
          .format(data["port_default_mode"],
                  data["port_tu_overrides"] or "(none)"))
    print("detection: st_value low bit; $a/$t cross-check {} agree / {} disagree"
          .format(cc["agree"], cc["disagree"]))
    if cc["disagree"]:
        print("  !! METHODS DISAGREE -- do not trust the tagging until resolved:")
        for d in cc["disagreements"][:10]:
            print("     {} @ {} lowbit={} mapping={}".format(
                d["mangled"][:56], d["raw_addr"], d["low_bit"], d["mapping_symbol"]))
    print()
    print("paired rows                 : {}".format(c["paired_rows"]))
    print("  binary body is Thumb-2    : {}".format(c["binary_thumb_rows"]))
    print("    mode-matched (verified) : {}".format(c["thumb_rows_mode_matched"]))
    print("    ISA-MISMATCHED (noise)  : {}".format(c["isa_mismatched_rows"]))
    if c["rows_without_binary_mode"]:
        print("  no binary FUNC symbol     : {}".format(c["rows_without_binary_mode"]))

    if data["tu_breakdown"]:
        print()
        print("per-TU breakdown (TUs holding any Thumb binary body):")
        for tu in data["tu_breakdown"][:args.top]:
            print("  {:<28} port={:<5} bin: arm={:<3} thumb={:<3} {}".format(
                tu["tu"][:28], tu["port_mode"], tu["binary_arm"],
                tu["binary_thumb"], tu["flip_verdict"]))

    if data["mismatched"]:
        print()
        print("ISA-mismatched symbols (score is noise, not a verdict):")
        for m in data["mismatched"][:args.top]:
            print("  {}".format(m))
        if len(data["mismatched"]) > args.top:
            print("  ... (+{} more -- see isa-modes.json)".format(
                len(data["mismatched"]) - args.top))

    print()
    if hits:
        print("STICKY TRIAGE VERDICTS REACHED AGAINST ISA NOISE -- {} row(s):"
              .format(len(hits)))
        for h in hits:
            print("  {:<16} {}".format(h["verdict"], h["mangled"]))
        if args.flag_triage:
            n = flag_triage(hits)
            print("  -> flagged {} triage.json entr{} with isa_retriage=true"
                  .format(n, "y" if n == 1 else "ies"))
        else:
            print("  (re-run with --flag-triage to mark them in triage.json)")
    else:
        print("no sticky triage verdict rests on an ISA-mismatched score.")

    print()
    print("-> {}".format(os.path.relpath(out, PROJECT_ROOT).replace(os.sep, "/")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
