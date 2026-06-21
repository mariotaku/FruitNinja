---
name: asm-fix-loop
description: Autonomous find→fix→verify→triage loop. Addresses BOTH positive excess (bloat) and negative excess (missing code) in asm-verify scores. Dispatches implementer and re-analyst agents, commits precise file sets, re-runs asm-verify. Use for batch divergence reduction across the port.
user_invocable: true
---

# ASM Fix Loop

Autonomous loop that chips at asm-verify divergences in **both directions**:
- **Positive excess** (port has MORE code than binary) → mechanical trimming or structural fix
- **Negative excess** (port has LESS code than binary) → RE the gap, port the missing code, or empty-and-rebuild from binary spec

Each round: scan both directions → dispatch → commit → verify → repeat.

> Scoring is operand-level: a surviving divergence is a real operand difference
> (wrong struct offset / immediate / predication / call-graph), not codegen skew
> — reg-alloc and reloc noise are pre-absorbed. Prioritise single-line
> `[GREG,#off]` / `#imm` diffs. Triage staleness keys on `asm_hash`, not score.

- After a fresh `bash tools/asm-verify/run.sh` produces a report with
  actionable divergences (port-only guards, identity bloat, dead code, missing code).
- When you want to run unattended: `/loop asm-fix-loop` self-paces.
- For a fixed-cadence sweep: `/loop 5m asm-fix-loop`.

## Pre-requisites

- `bash tools/asm-verify/run.sh` completed at least once (report.json exists).
- Docker running (fnverify image built).

## Workflow

### 1. Scan targets — both directions

PREFER `tmp/asm-verify/shortlist.md` (written by classify-divergences.py at the
end of run.sh): rows are pre-ranked by real-bug-likelihood (HIGH/MED) with a
cause tag, and ~200 benign rows (std-inline / port-guard / GOT / static-init /
sched) are already demoted out. Dispatch on HIGH first, then MED `wrong-offset`/
`wrong-const`/`call-graph`. Treat `wrong-offset` as MED-confidence (a real
field-offset bug and a benign embedder-shift look identical in the diff — confirm
against the struct layout before fixing). The raw score-delta scans below are the
fallback when no shortlist exists.

**Positive excess (bloat — port has extra code):**
```sh
python -c "
import json
d = json.load(open('tmp/asm-verify/report.json'))
syms = [s for s in d['symbols'] if s.get('score') and s['score'] - s['max_score'] > 1000]
syms.sort(key=lambda s: s['score'] - s['max_score'], reverse=True)
for s in syms[:15]:
    print(f'{s[\"score\"]-s[\"max_score\"]:+6d} {s[\"score\"]:>6}/{s[\"max_score\"]:<6} {s[\"mangled\"][:80]}')
"
```

**Negative excess (missing — port has LESS code than binary):**
```sh
python -c "
import json
d = json.load(open('tmp/asm-verify/report.json'))
syms = [s for s in d['symbols'] if s.get('score') and s['score'] - s['max_score'] < -500]
# filter known false-positives
syms = [s for s in syms if not s['mangled'].startswith('_GLOBAL__I_')]
syms.sort(key=lambda s: s['score'] - s['max_score'])
for s in syms[:15]:
    pct = s['score']/s['max_score']*100 if s['max_score'] > 0 else 0
    print(f'{s[\"score\"]-s[\"max_score\"]:+6d} {s[\"score\"]:>6}/{s[\"max_score\"]:<6} ({pct:.0f}%) {s[\"mangled\"][:80]}')
"
```

### 2. Categorize targets

#### Bloat (positive excess) — priority order:

| Priority | Pattern | Why |
|----------|---------|-----|
| 1 | Port-only `if (!X::GetInstance()) return;` guards | Binary never null-checks singletons |
| 2 | `LOG_INFO`/`LOG_DEBUG` calls in game code | No-ops under cross-build already, just dead code |
| 3 | `Matrix44` Identity() in ctors/loops | Binary treats matrices as POD |
| 4 | Duplicate/dead code blocks | Copy-paste artifacts, unused computations |
| 5 | `Vec3` temporary constructions in hot loops | Binary uses raw float arithmetic |
| 6 | Singleton `GetInstance()` → direct `m_instance` | Removes guard-variable overhead |
| 7 | Container patterns (std::vector → raw arrays) | Structural, needs RE |

#### Missing (negative excess) — severity-driven:

- **Critical** (< 50% of binary): function is seriously incomplete. Dispatch `re-analyst` to RE the gap; if the port implementation is too divergent, **empty it and rebuild from the binary spec**.
- **Moderate** (50-80%): RE what's missing, fill the gap.
- **Minor** (80-99%): likely a few missing branches or statements. Quick RE + patch.

### 3. Dispatch agents

#### For bloat (> 0 excess):

Launch `implementer` agent directly:
```
Fix <ClassName>::<Function> (score <N>/<M>, <ratio>% ratio).
File: <path>. Check for port-only patterns: singleton guards,
LOG calls, redundant field writes. Trim mechanical bloat.
Build: cmake --build build -j$(nproc)
```

#### For missing (< 0 excess):

**Two-phase approach:**
1. **First:** dispatch `re-analyst` to decompile the binary function, diff against port, produce a gap report with:
   - Complete binary pseudocode
   - What the port has vs. what's missing
   - Verdict: patch the gaps, or empty-and-rebuild (if port diverged too far)
2. **Then:** dispatch `implementer` with the RE spec, or if empty-and-rebuild: re-port from scratch matching binary.

For critical cases (port has < 50% of binary code), the `re-analyst` should default to recommending empty-and-rebuild unless the missing pieces are clearly isolated.

- **Run 2-3 agents per round** — mix 2 bloat + 1 missing, or 1 bloat + 2 missing depending on what's available.
- **Run in parallel**: dispatch agents simultaneously for independent files.
- **Background**: set `run_in_background: true` so the loop keeps moving.
- Each agent reports changed files at the end of its response.

### 4. Commit precisely

When an agent completes, read its report to identify the EXACT files changed.
Stage only those files:

```sh
git add src/game/WaveManager.cpp src/game/WaveManager.h
git commit -m "WaveManager: <one-line summary>"
```

- **Never `git add -A`** — bundles unrelated CRLF rewrites and stale triage drift.
- If multiple agents complete simultaneously, commit each one separately.
- Commit message format: `ClassName: <what changed>`

### 5. Re-verify after every round

After all agents in a round complete and are committed, re-run:

```sh
bash tools/asm-verify/run.sh
```

Check for:
- Regressions (score went UP)
- Cross-build breakage (compile errors in verify.sh output)
- Missing gaps that didn't close (may need empty-and-rebuild)

### 6. Triage

Sync triage scores after each verify run. Update `tools/asm-verify/triage.json`
with current scores for any DIVERGE/SUSPICIOUS entries.

### 7. Repeat

Go back to step 1 with fresh scores. Stop when:
- Both bloat and missing targets are exhausted (remaining divergence is structural/compiler artifacts)
- Cross-build breaks and can't be quickly fixed
- User says stop
- Round count reached (if specified)

## Anti-patterns / known false-positives (don't fix)

- **Don't rewrite during mechanical sweeps.** Trimming (guards, LOGs, dead code) is fast/safe; structural rewrites (container/algorithm changes) need dedicated RE sessions.
- **Don't change signatures casually.** Even `const&` → by-value needs binary nm verification first — different mangled names.
- **Don't touch platform files.** `*SDL.cpp` / `*Posix.cpp` / `*Win32.cpp` are excluded from asm-verify.
- **Don't ignore cross-build breakage.** GCC 4.4.1 (`-std=gnu++0x`); C++11 features (`alignas`, `auto`, range-for, lambdas) silently break it.
- `_GLOBAL__I_*` static initializers — always cosmetic (PIC anchor register diff).
- GL 1.x vs GLES2 pipeline in Draw functions — structural, not mechanical.
- Compiler-generated std::vector/std::map destructor code — can't change without changing the container.
- `SmartPtr<T> const&` vs `SmartPtr<T>` — ARM32 ABI identical but different mangling; fix only when binary nm confirms the other form.
- **ARM vs Thumb mode mismatch** — some binary functions are ARM mode (4-byte insns) but the cross-build compiles everything Thumb-2; the diff shows different encoding/scheduling for identical semantics. Before dispatching on a negative-excess target, check whether the binary fn ends with Thumb `bx lr` (`0x4770`) or ARM `bx lr` (`0xe12fff1e`); if ARM mode and instruction counts match (~37 vs ~37), it's a false positive → `ACCEPT-cosmetic`.
