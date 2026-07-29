#!/usr/bin/env python3
"""Flag INVERTED PAIRINGS in an existing asm-verify report (task #133).

An inverted pairing is a row that LOOKS handled: the binary's mangled name
landed on a port FORWARDER instead of the port's real body, so the sweep
diffed a tiny forwarder against a large binary body AND STILL SCORED IT.
See forwarder_rule.py for the rule and its three sub-classes.

Works from the stored report -- no re-sweep needed (the diff hunks let both
normalized instruction streams be reconstructed exactly).

Usage:
    python tools/asm-verify/detect-forwarders.py
    python tools/asm-verify/detect-forwarders.py --report <path>
    python tools/asm-verify/detect-forwarders.py --all      # show every class
    python tools/asm-verify/detect-forwarders.py --check    # exit 1 on FORWARDER

Outputs:
    tmp/asm-verify/forwarders.json   machine-readable, all classes
    report.json enriched in place with per-symbol `pairing_suspect`
                (skip with --no-enrich)
    stdout                           ranked summary, FORWARDER first

NOTHING is auto-suppressed or auto-aliased. A wrong auto-fix here would
recreate the exact problem: a row that looks handled.
"""
import argparse
import json
import pathlib
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import audit_config                      # noqa: E402
import forwarder_rule                    # noqa: E402

PROJECT_ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
OUT_DIR      = PROJECT_ROOT / "tmp" / "asm-verify"


def report_freshness(report_path):
    """(is_stale, message). Compares report mtime to the newest commit that
    touched src/. An unattended run has nobody to notice a thin report."""
    if not report_path.exists():
        return True, "report does not exist: %s" % report_path
    r_mtime = report_path.stat().st_mtime
    try:
        out = subprocess.run(
            ["git", "log", "-1", "--format=%ct", "--", "src"],
            cwd=str(PROJECT_ROOT), capture_output=True, text=True, check=True)
        src_ct = int(out.stdout.strip())
    except Exception as e:
        return False, "could not read git log for src/ (%s); freshness unknown" % e
    if r_mtime < src_ct:
        import datetime
        fmt = lambda t: datetime.datetime.fromtimestamp(t).strftime("%Y-%m-%d %H:%M")
        return True, ("report.json (%s) is OLDER than the newest commit touching "
                      "src/ (%s) -- every verdict below may describe code that no "
                      "longer exists. Re-run tools/asm-verify/run.sh."
                      % (fmt(r_mtime), fmt(src_ct)))
    return False, "report.json is newer than the last src/ commit"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--report", default=None,
                    help="report.json path (default: [sweep_audit].report_path)")
    ap.add_argument("--out", default=str(OUT_DIR / "forwarders.json"))
    ap.add_argument("--all", action="store_true",
                    help="list EMPTY-STUB and SMALLER rows too, not just FORWARDER")
    ap.add_argument("--no-enrich", action="store_true",
                    help="do not write `pairing_suspect` back into report.json")
    ap.add_argument("--check", action="store_true",
                    help="exit 1 when any FORWARDER row exists")
    args = ap.parse_args()

    cfg = audit_config.load()
    report_path = pathlib.Path(args.report) if args.report else \
        PROJECT_ROOT / cfg.sweep_audit.report_path
    stale, msg = report_freshness(report_path)
    if not report_path.exists():
        sys.exit("ERROR: %s" % msg)
    if stale and cfg.sweep_audit.warn_if_report_older_than_src:
        print("!" * 78)
        print("!! STALE INPUT: " + msg)
        print("!" * 78)
        print()

    data = json.loads(report_path.read_text(encoding="utf-8"))
    symbols = data.get("symbols") or []
    if not symbols:
        sys.exit("ERROR: %s has no 'symbols' array -- truncated or wrong file"
                 % report_path)

    rule = forwarder_rule.ForwarderRule(cfg)
    buckets = {forwarder_rule.FORWARDER: [], forwarder_rule.EMPTY_STUB: [],
               forwarder_rule.SMALLER: []}
    n_paired = 0
    for row in symbols:
        if row.get("verdict") == "UNPAIRED":
            continue
        n_paired += 1
        susp = rule.classify_row(row)
        row["pairing_suspect"] = susp        # None clears a previous run's flag
        if susp is None:
            continue
        buckets[susp["shape"]].append({
            "mangled":       row.get("mangled"),
            "addr":          row.get("addr"),
            "port_mangled":  row.get("port_mangled"),
            "verdict":       row.get("verdict"),
            "score":         row.get("score"),
            "max_score":     row.get("max_score"),
            **susp,
        })
    for b in buckets.values():
        b.sort(key=lambda r: r["ratio"])

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    out_path = pathlib.Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps({
        "report":            str(report_path),
        "report_stale":      stale,
        "paired_symbols":    n_paired,
        "thresholds": {
            "max_port_ratio":       rule.max_port_ratio,
            "min_binary_instrs":    rule.min_binary_instrs,
            "max_forwarder_instrs": rule.max_forwarder_instrs,
        },
        "forwarders":  buckets[forwarder_rule.FORWARDER],
        "empty_stubs": buckets[forwarder_rule.EMPTY_STUB],
        "smaller":     buckets[forwarder_rule.SMALLER],
    }, indent=2), encoding="utf-8")

    if not args.no_enrich:
        # Preserve the report's mtime: it is the freshness signal every other
        # audit compares against the newest src/ commit. Enriching it in place
        # must not make a stale sweep look like a fresh one.
        st = report_path.stat()
        report_path.write_text(json.dumps(data, indent=2), encoding="utf-8")
        import os
        os.utime(report_path, (st.st_atime, st.st_mtime))

    # ---- stdout summary -------------------------------------------------
    fw, es, sm = (buckets[forwarder_rule.FORWARDER],
                  buckets[forwarder_rule.EMPTY_STUB],
                  buckets[forwarder_rule.SMALLER])
    print("=" * 74)
    print("INVERTED-PAIRING SCAN  (port < %.0f%% of binary, binary >= %d instr)"
          % (rule.max_port_ratio * 100, rule.min_binary_instrs))
    print("=" * 74)
    print("  paired symbols scanned : %d" % n_paired)
    print("  %-24s: %d   <- inverted pairing; the score is meaningless"
          % ("SUSPICIOUS-FORWARDER", len(fw)))
    print("  %-24s: %d   (unported/defunct -- different disease)"
          % ("EMPTY-STUB", len(es)))
    print("  %-24s: %d   (needs a human call)" % ("SMALLER", len(sm)))
    print()

    def dump(title, rows, note):
        if not rows:
            return
        print("--- %s (%d) -- %s ---" % (title, len(rows), note))
        for r in rows:
            alias = ("  [aliased to %s]" % r["port_mangled"]) if r.get("port_mangled") else ""
            print("  %.3f  %4dp vs %4db  %-16s %s%s"
                  % (r["ratio"], r["port_instrs"], r["binary_instrs"],
                     r["verdict"], r["mangled"], alias))
        print()

    dump("SUSPICIOUS-FORWARDER", fw,
         "port body is a tail call; the real body is under another symbol")
    if args.all:
        dump("EMPTY-STUB", es, "port has no implementation at all")
        dump("SMALLER", sm, "genuinely shorter port body -- judge individually")
    elif es or sm:
        print("(%d EMPTY-STUB + %d SMALLER rows suppressed; --all to list, "
              "full detail in %s)" % (len(es), len(sm), out_path))
        print()

    print("Full detail: %s" % out_path)
    if not args.no_enrich:
        print("Enriched   : %s (per-symbol 'pairing_suspect')" % report_path)

    if args.check and fw:
        print("\nCHECK: FAIL -- %d inverted pairing(s); alias the binary symbol to "
              "the port's REAL body in manifest.toml (by hand, after reading it)"
              % len(fw))
        return 1
    if args.check:
        print("\nCHECK: PASS -- no inverted pairings")
    return 0


if __name__ == "__main__":
    sys.exit(main() or 0)
