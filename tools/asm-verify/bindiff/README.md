# asm-verify/bindiff/

Whole-program BinDiff track — **stage 9** of [the pipeline](../README.md#pipeline) (rare, architecture-confidence deep dives). See that stage for context; this is just the script index.

Entry: `bash tools/asm-verify/bindiff/bindiff-pipeline.sh [--twins-only] [--skip-build]`.

- **`bindiff-pipeline.sh`** — one-command driver: build ARM+Thumb twin `.so`s, BinExport each, BinDiff vs the binary, mode-matched merge → ranked CSV.
- **`build-so.sh`** — link `fnverify.so` from the cross-compiled `.o`s (in the `fnverify-bada` Docker container).
- **`mode-match-merge.py`** — merge the ARM-twin and Thumb-twin diffs per-function by the binary's actual `$a`/`$t` mode, so Thumb-vs-ARM encoding can't cause false divergence.
- **`discover-arm-mode.py`** — find which port source files hold ARM-mode binary functions → CMake `-marm` fragment.
- **`resolve-bindiff-names.py`** — recover fully-qualified names by ADDRESS (binexport demangles to bare leaves; pairs are address-joined against both symtabs).
- **`triage-prefilter.py`** — deterministic sim-band + triage-hint filter that shortens the ranked CSV to a real-divergence "investigate" list.
