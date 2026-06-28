# asm-verify/coverage

Standalone coverage-analysis utilities for the asm-verify pipeline. These read
existing pipeline outputs (`report.json`, `FruitNinja.exe`) and do not
require Docker or a cross-build run.

## Scripts

### `catC-survey.py`

Binary FUNC coverage analysis: enumerates all `FUNC` symbols in `FruitNinja.exe`
via [lief](https://lief.re/), cross-references `asm-verify/report.json` to find
symbols matched by the cross-build, and prints a breakdown by subsystem bucket
(screens / hud / entities / game / engine-mortar / engine-other / bada-osp / stdlib).

```sh
python tools/asm-verify/coverage/catC-survey.py [--project <root>]
```

Writes `tmp/asm-verify/coverage/catC.json` (machine-readable) and prints a ranked
human summary to stdout.

Requires `pip install lief`; `pip install itanium_demangler` for cleaner names.

Run after a full `bash tools/asm-verify/run.sh` to get the current
unmatched-class list for coverage planning.
