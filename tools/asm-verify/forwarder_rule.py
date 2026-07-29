#!/usr/bin/env python3
"""INVERTED-PAIRING detector (task #133) -- one rule, two consumers.

The disease
-----------
Worse than an UNPAIRED symbol, because the row LOOKS handled: the binary's
mangled name lands on a port FORWARDER instead of the port's real body, so the
sweep diffs a 1-instruction forwarder against a 140-instruction binary body
AND STILL PRODUCES A SCORE. Nothing about the row says "this comparison is
meaningless" -- it just reads as a big DIVERGE, which is indistinguishable
from an honestly-divergent port and gets triaged as one.

Proven instances: GameModifier::ApplyModifier (port forwards to
OnDeferComplete, which holds the real body), ActorManager::GetEntityFirst /
GetEntityNext (binary's (long,&) overload lands on the port's int-narrowing
forwarder), WaveModifier::ApplyModifier.

The rule
--------
Applied to the NORMALIZED instruction streams the sweep already compares:

    port_instrs / binary_instrs < max_port_ratio
    AND binary_instrs >= min_binary_instrs

...produces a size-suspect row, which is then SUB-CLASSIFIED by the SHAPE of
the port body, because "small" has three very different causes:

  FORWARDER   -- <= max_forwarder_instrs and the last instruction is an
                 unconditional branch: a tail call. The body is somewhere
                 else. This is the inverted pairing; it is the finding.
  EMPTY-STUB  -- the body is only return scaffolding (`bx lr`). Not a pairing
                 bug: the port genuinely has no implementation (defunct stub,
                 or unported). Belongs to detect-gutted-bada.py; surfaced here
                 separately so it is never mistaken for a forwarder.
  SMALLER     -- a real, non-trivial port body that is simply shorter than the
                 binary's. Inlining, a container swap, or a terser
                 implementation all do this HONESTLY -- so these need a human
                 call and are never auto-actioned.

Nothing here auto-suppresses or auto-aliases. A wrong auto-fix would recreate
exactly the problem it is meant to find: a row that looks handled.
"""
import re

try:
    import audit_config
except ImportError:                                   # pragma: no cover
    import importlib.util
    import pathlib
    _spec = importlib.util.spec_from_file_location(
        "audit_config", pathlib.Path(__file__).resolve().parent / "audit_config.py")
    audit_config = importlib.util.module_from_spec(_spec)
    _spec.loader.exec_module(audit_config)

# Sub-classes, most-actionable first.
FORWARDER  = "FORWARDER"
EMPTY_STUB = "EMPTY-STUB"
SMALLER    = "SMALLER"

# The verdict a size-suspect row gets INSTEAD of a similarity score. Only
# FORWARDER rows earn it -- EMPTY-STUB / SMALLER keep their normal verdict and
# are merely annotated, since their small port side is not evidence of a
# mis-pairing.
VERDICT = "SUSPICIOUS-FORWARDER"


def sides_from_diff(diff_lines):
    """Reconstruct the two normalized instruction streams from a report diff.

    asm-verify.py writes '  ' = common, '- ' = binary-only, '+ ' = port-only,
    so both original streams are recoverable exactly. This lets the rule run
    post-hoc over an existing report.json without a (multi-minute) re-sweep.
    """
    port, binary = [], []
    for line in diff_lines or []:
        if line.startswith("  "):
            port.append(line[2:].strip())
            binary.append(line[2:].strip())
        elif line.startswith("+ "):
            port.append(line[2:].strip())
        elif line.startswith("- "):
            binary.append(line[2:].strip())
    return port, binary


class ForwarderRule(object):
    def __init__(self, cfg=None):
        c = (cfg or audit_config.load()).forwarder
        self.max_port_ratio       = float(c.max_port_ratio)
        self.min_binary_instrs    = int(c.min_binary_instrs)
        self.max_forwarder_instrs = int(c.max_forwarder_instrs)
        self._tail_call = c.regex("tail_call_re")
        self._call      = c.regex("call_re")
        self._ret_only  = c.regex("return_only_re")
        self._skip      = c.regexes("skip_name_res")

    # -- shape ------------------------------------------------------------
    def _shape(self, port):
        if not port or all(self._ret_only.match(i) for i in port):
            return EMPTY_STUB
        if (len(port) <= self.max_forwarder_instrs
                and self._tail_call.match(port[-1])
                and not any(self._call.match(i) for i in port)):
            return FORWARDER
        return SMALLER

    def skipped(self, mangled):
        return any(r.search(mangled or "") for r in self._skip)

    # -- main entry -------------------------------------------------------
    def classify(self, mangled, port_lines, bin_lines):
        """Return a `pairing_suspect` dict, or None when the row is fine.

        Keys: shape, port_instrs, binary_instrs, ratio, reason.
        Only shape == FORWARDER should override the row's verdict.
        """
        p, b = len(port_lines), len(bin_lines)
        if b < self.min_binary_instrs:
            return None
        if p >= self.max_port_ratio * b:
            return None
        if self.skipped(mangled):
            return None
        shape = self._shape([l.strip() for l in port_lines])
        ratio = (p / b) if b else 0.0
        if shape == FORWARDER:
            reason = ("port side is a %d-instruction tail-call forwarder vs a "
                      "%d-instruction binary body -- the real port body is under "
                      "another symbol; this pairing scores nothing meaningful"
                      % (p, b))
        elif shape == EMPTY_STUB:
            reason = ("port side is an empty stub (%d instr) vs %d binary instr "
                      "-- unported or defunct, not a mis-pairing" % (p, b))
        else:
            reason = ("port side is %.0f%% the size of the binary body (%dp vs "
                      "%db) -- could be inlining / container swap / terser code; "
                      "needs a human call" % (ratio * 100, p, b))
        return {
            "shape":         shape,
            "port_instrs":   p,
            "binary_instrs": b,
            "ratio":         round(ratio, 4),
            "reason":        reason,
        }

    def classify_row(self, row):
        """Same, for a report.json symbol row (uses its stored diff)."""
        port, binary = sides_from_diff(row.get("diff"))
        if not binary:
            return None
        return self.classify(row.get("mangled"), port, binary)
