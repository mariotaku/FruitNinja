# RE + Port Pipeline — End-to-End Stage Map

The Fruit Ninja port uses a multi-stage verification pipeline to measure fidelity against the v1.6.1 ARM32 binary. **The core flow** is: binary + source → .o (cross-compiled) → ASM diff (operand-level) → ranked real-bug shortlist. Two parallel entry tracks exist:
1. **Per-symbol asm-verify** (stage-by-stage function testing, weekly iteration loop)
2. **Whole-program BinDiff** (full-program similarity analysis, rare deep dives)

Both feed the same triage bottleneck (`tools/asm-verify/triage.json`). The asm-verify scorer is **operand-level**: the cross-build toolchain (Sourcery 2010q1, GCC 4.4.1) is the open upstream of Samsung's Sourcery G++ **4.4-261** that built the binary, confirmed operand-for-operand identical — so register/immediate divergence is genuine signal, not noise.

## Pipeline Stages

| # | Stage | Script / Agent | Input → Output (data files) | Purpose | Detail |
|---|-------|------|--------|---------|---------|
| 1 | **Binary analysis** | re-analyst (GhidraMCP) | FruitNinja.exe → analysis report | Decompile a function, resolve a struct, read DAT constants | `.claude/agents/re-analyst.md` |
| 2 | **Spec → source** | re-analyst → implementer | report → `// TODO:` / `// ASM-spec:` markers in src/ | Convert findings to source-side comments (the canonical spec) | CLAUDE.md § "Source-side comment grammar" |
| 3 | **Implement** | implementer | `// TODO:` markers → C++ in src/ | Write or refine port code against the spec | `.claude/agents/implementer.md` |
| 4 | **Cross-build** | CMake + Sourcery 4.4.1 (Docker) | src/ → fnverify.a (object files, .o) | Compile port with the *exact* binary toolchain (ensures operand equivalence) | CLAUDE.md § "Original Binary" + `docs/engine/binary-build-evidence.md` |
| 5 | **Symbol discovery** | `discover-symbols.py` + `export-binary-symbols.py` (Docker) | fnverify.a + binary → manifest.generated.toml + symbols/*.s | Extract symbol metadata from both binaries; align by name + demangling | `tools/asm-verify/verify.sh` lines 61-63 |
| 6 | **ASM-level diff** | `asm-verify.py` (Docker) | fnverify.a vs binary (per symbol) → report.json | Compare operand-by-operand; score common%, reason each divergence | `tools/asm-verify/asm-verify.py` line ~27 |
| 7 | **Auto-classify** | `classify-divergences.py` (host) | report.json → shortlist.md + suggested-triage.json | Rank divergences by LIKELIHOOD (HIGH/MED/LOW) + CAUSE (control-flow, register naming, call-site shape, etc.) | `tools/asm-verify/classify-divergences.py` lines 1-24 |
| 8 | **Triage + fix** | asm-triager (agent) → implementer (agent) → re-run stage 4-7 | shortlist.md → `tools/asm-verify/triage.json` (sticky verdicts) → code fixes | Classify each HIGH/MED divergence as ACCEPT-cosmetic / ACCEPT-deferred / FIX-NEEDED; apply root fixes; iterate | `.claude/agents/asm-triager.md` + `.claude/agents/implementer.md` |
| 9 | **Whole-program BinDiff** | `bindiff-pipeline.sh` (rare, for architecture confidence) | binary vs fnverify.{arm,thumb}.so → BinDiff + CSV | Full-program similarity by function; ranks structural divergences | `tools/asm-verify/bindiff/bindiff-pipeline.sh` + `bindiff/resolve-bindiff-names.py` + `bindiff/triage-prefilter.py` |
| 10 | **Layout reference** | `layout/infer-class-sizes.py` / `layout/layout-reference.py` / `layout/extract-typeinfo.py` | binary → tmp/binary-class-sizes.json, tmp/typeinfo-tree.json | Snapshot binary class layouts + typeinfo; used for struct correctness spot-checks | `tools/asm-verify/layout/` (scripts at lines 1-50) |
| 11 | **Single-function verification** | asm-inspector (agent + `compile-one.sh` Docker) | a claim (e.g. "is the loop unrolled?") → ASM-level verdict | Compile a minimal test unit with Bada toolchain; diff assembly to settle decompiler doubts | `.claude/agents/asm-inspector.md` |

## Data Flow Diagram (text)

```
FruitNinja.exe (v1.6.1, ARM32)
    |
    v
[1] re-analyst(GhidraMCP) -----> [2] source-side comments (spec)
                                        |
                                        v
                                   [3] implementer → src/
                                        |
                                        v
                       [4] Cross-compile (Docker)
                       fnverify.a (Sourcery 4.4.1)
                                  |
                   ________________|________________
                   |                               |
                   v                               v
          [5] discover-symbols.py          [5] export-binary-symbols.py
          manifest.generated.toml          binary symbols cache
                   |                               |
                   |_______________ ________________|
                                   |
                                   v
                        [6] asm-verify.py
                       report.json (symbol diffs)
                            |
                            v
                    [7] classify-divergences.py
               shortlist.md (ranked divergences)
                            |
                            v
                    [8] asm-triager (agent)
            tools/asm-verify/triage.json (sticky)
                            |
                 (fix/retest loop back to [3])
```

### Parallel track (whole-program analysis, rare)

```
[9] bindiff-pipeline.sh
    fnverify.arm.so + fnverify.thumb.so
    (both -marm / -mthumb flags, same source)
    |
    v
[9] BinDiff vs binary (mode-matched merge)
    divergences.csv (ranked by function similarity)
```

## File Locations & Persistence

| Item | Location | Purpose | Commits? |
|------|----------|---------|----------|
| Binary v1.6.1 | `tmp/FruitNinja_v1_6_1.exe` | The verification target (read-only) | No; gitignored |
| Cross-build manifest | `tools/asm-verify/manifest.generated.toml` | Updated per build; lists every symbol under verification | Yes; tracks scope changes |
| Triage verdicts | `tools/asm-verify/triage.json` | Sticky vote per asm_hash (ACCEPT-* vs FIX-NEEDED); keyed so re-runs preserve prior verdicts | Yes; fidelity record |
| Transient report | `tmp/asm-verify/report.json` + `.md` | Latest asm-verify output; overwritten each run | No; gitignored |
| Shortlist | `tmp/asm-verify/shortlist.md` | Auto-classified divergences; INPUT to asm-triager, not a commit artifact | No; gitignored |
| BinDiff outputs | `tmp/bindiff-out/` | Whole-program divergence CSV; used for architecture-level confidence only | No; gitignored |
| Type reference | `tmp/binary-class-sizes.json`, `tmp/typeinfo-tree.json` | Snapshot of binary's class layouts; for spot-checking struct definitions | No; gitignored |

## Entry Points

### Quick verification (single class)
```bash
bash tools/asm-verify/run.sh --class Foo
```
Runs stages 4-7 (cross-build + asm-verify + auto-classify) for all symbols matching `*Foo*`. Reports in `tmp/asm-verify/report.md` and `shortlist.md`.

### Full sweep
```bash
bash tools/asm-verify/run.sh
```
Every symbol under `manifest.generated.toml` (several minutes in Docker).

### Whole-program BinDiff (deep architecture review)
```bash
bash tools/asm-verify/bindiff/bindiff-pipeline.sh
```
Stages 4, 9 (build twins, export, BinDiff). ~30–60 min including Ghidra + BinDiff host-side. Output in `tmp/bindiff-out/`.

### Single-function claim verification (rare)
Invoke asm-inspector agent (e.g. via Claude Code `/agent re-analyst` directive with a specific question).

## Triage policy

Each HIGH/MED divergence is classified **ACCEPT-cosmetic** / **ACCEPT-deferred** / **FIX-NEEDED**, sticky per `asm_hash` in `triage.json`. Definitions + classification heuristics: `.claude/agents/asm-triager.md`.

## Why operand-level

Because the cross-build compiler *is* the binary's, operand divergence is real signal: same operands = faithful; different-but-equivalent (reg-alloc / scheduling) = ACCEPT-cosmetic; missing call / inverted gate / wrong constant = bug, fix at root. This is also why a `std::` substitution for a binary structure (e.g. `Mortar::MemoryPool`) inflates divergence — different layout → different offsets and load/store patterns, cascading into callers. See CLAUDE.md § "No band-aid fixes".
