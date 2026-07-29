#!/usr/bin/env python3
"""Loader for tools/asm-verify/audit-config.toml.

Single place that knows where the audit knobs live and what their defaults
are, so neither detect-forwarders.py, asm-verify.py nor stale-marker-lint.py
hardcodes a target-specific fact (instruction idiom, marker vocabulary,
verdict vocabulary, platform-path glob, threshold).

Failure policy -- fail LOUDLY on a broken input, quietly on a missing one:
  * config file ABSENT   -> built-in defaults (identical to pre-config
    behaviour); callers can see this via `cfg.from_defaults`.
  * config file PRESENT but malformed / unknown key type -> raise. A silently
    ignored typo in a threshold is exactly the "looks handled" failure this
    whole audit exists to kill.
"""
import pathlib
import re
import sys

try:
    import tomllib
except ImportError:                                   # pragma: no cover
    import tomli as tomllib                           # type: ignore

SCRIPT_DIR   = pathlib.Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent.parent
CONFIG_PATH  = SCRIPT_DIR / "audit-config.toml"

# Built-in defaults. Mirrors audit-config.toml; see that file for the rationale
# behind each value. Keep the two in sync when changing either.
_DEFAULTS = {
    "forwarder": {
        "max_port_ratio":       0.25,
        "min_binary_instrs":    15,
        "max_forwarder_instrs": 4,
        "tail_call_re":         r"^b(?:\.w|\.n)?\s+<",
        "call_re":              r"^(?:CALL|bl|blx)\b",
        "return_only_re":       r"^(?:bx\s+lr|pop\s+\{[^}]*pc[^}]*\}|nop|mov\s+pc,\s*lr)$",
        "skip_name_res":        [r"^_GLOBAL__I_", r"^_GLOBAL__D_"],
    },
    "sweep_audit": {
        "report_path":            "tmp/asm-verify/report.json",
        "audited_marker_kinds":   ["ASM-verified"],
        "confirming_verdicts":    ["MATCH", "COSMETIC", "ACCEPT-cosmetic",
                                   "ACCEPT-defunct"],
        "contradicting_verdicts": ["DIVERGE", "FIX-NEEDED", "UNPAIRED",
                                   "SUSPICIOUS-FORWARDER"],
        "weak_verdicts":          ["SUSPICIOUS", "ACCEPT-deferred"],
        "verify_sources_cmake":   "tools/asm-verify/verify-sources.cmake",
        "platform_file_res":      [r"SDL\.(?:cpp|h)$", r"Posix\.(?:cpp|h)$",
                                   r"Win32\.(?:cpp|h)$", r"^src/platform/"],
        "warn_if_report_older_than_src": True,
    },
}


class _Section(dict):
    """dict with attribute access and type-checked lookup."""

    def __init__(self, name, data):
        dict.__init__(self, data)
        self._name = name

    def __getattr__(self, key):
        try:
            return self[key]
        except KeyError:
            raise AttributeError(
                "audit-config.toml [%s] has no key %r" % (self._name, key))

    def regexes(self, key):
        """Compile a list-of-patterns key once, failing loudly on a bad regex."""
        out = []
        for pat in self[key]:
            try:
                out.append(re.compile(pat))
            except re.error as e:
                raise SystemExit(
                    "ERROR: audit-config.toml [%s].%s: bad regex %r: %s"
                    % (self._name, key, pat, e))
        return out

    def regex(self, key):
        try:
            return re.compile(self[key])
        except re.error as e:
            raise SystemExit(
                "ERROR: audit-config.toml [%s].%s: bad regex %r: %s"
                % (self._name, key, self[key], e))


class AuditConfig(object):
    def __init__(self, path=None):
        self.path = pathlib.Path(path) if path else CONFIG_PATH
        self.from_defaults = not self.path.exists()
        data = {}
        if not self.from_defaults:
            try:
                data = tomllib.loads(self.path.read_text(encoding="utf-8"))
            except Exception as e:
                raise SystemExit("ERROR: cannot parse %s: %s" % (self.path, e))
        merged = {}
        for sect, defaults in _DEFAULTS.items():
            vals = dict(defaults)
            got = data.get(sect, {})
            if not isinstance(got, dict):
                raise SystemExit("ERROR: %s: [%s] must be a table" % (self.path, sect))
            for k, v in got.items():
                if k not in defaults:
                    raise SystemExit(
                        "ERROR: %s: unknown key [%s].%s -- typo? "
                        "(known: %s)" % (self.path, sect, k,
                                         ", ".join(sorted(defaults))))
                if type(v) is not type(defaults[k]) and not (
                        isinstance(v, (int, float)) and
                        isinstance(defaults[k], (int, float))):
                    raise SystemExit(
                        "ERROR: %s: [%s].%s expects %s, got %s"
                        % (self.path, sect, k, type(defaults[k]).__name__,
                           type(v).__name__))
                vals[k] = v
            merged[sect] = _Section(sect, vals)
        self.forwarder   = merged["forwarder"]
        self.sweep_audit = merged["sweep_audit"]


_CACHE = {}


def load(path=None):
    key = str(path or CONFIG_PATH)
    if key not in _CACHE:
        _CACHE[key] = AuditConfig(path)
    return _CACHE[key]


if __name__ == "__main__":
    cfg = load()
    print("config: %s%s" % (cfg.path,
                            "  (MISSING -- using built-in defaults)"
                            if cfg.from_defaults else ""))
    for name in ("forwarder", "sweep_audit"):
        print("[%s]" % name)
        for k, v in sorted(getattr(cfg, name).items()):
            print("  %-32s = %r" % (k, v))
    sys.exit(0)
