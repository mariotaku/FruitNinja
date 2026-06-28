# symbol-diff

Binary-vs-port symbol coverage tools. The primary workflow (Docker cross-compile +
full diff report) lives in `run.sh` and the `classify.py` / `fix_signatures.py`
scripts. The utilities below operate on existing outputs without requiring Docker.

See also the `/symbol-diff` skill (`.claude/skills/symbol-diff.md`) for the full
step-by-step workflow.

## Scripts

### `scan-wraps.py`

Scans `src/**/*.cpp` for `#ifndef __bada__` / `#if !defined(__bada__)` blocks,
classifies each as a whole-function wrap or a partial block, and ranks by LOC.
Primary input for the task-#192 un-wrap plan.

```sh
python tools/symbol-diff/scan-wraps.py [--project <root>]
```

Writes `tmp/symbol-diff/wraps.json` and prints the ranked list to stdout.

### `summarize-unwrap-plan.py`

Readable summary renderer for an unwrap-plan.json file (the task-#192 free-function
un-wrap plan produced by the x64-audit pipeline).

```sh
python tools/symbol-diff/summarize-unwrap-plan.py [<plan-file>]
# default: tmp/x64-audit/unwrap-plan.json
```

Prints CLEAN batches (ordered), NEEDS-REFACTOR/VERIFY items, LEAVE decisions,
DEFUNCT stubs, and the shared-file blast radius to stdout.
