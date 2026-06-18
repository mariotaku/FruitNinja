---
name: asm-fix-loop
description: Autonomous find→fix→verify→triage loop. Iterates over top asm-verify excess scores, dispatches implementer agents in parallel, commits precise file sets, re-runs asm-verify, syncs triage. Use for batch mechanical/structural divergence reduction.
user_invocable: true
---

# ASM Fix Loop

Autonomous loop that chips at asm-verify divergences by dispatching
implementer agents in parallel, committing their changes precisely,
re-running asm-verify, and syncing triage. Each round: pick top excess
targets → dispatch → commit → verify → repeat.

## When to invoke

- After a fresh `bash tools/asm-verify/run.sh` produces a report with
  actionable divergences (port-only guards, identity bloat, dead code).
- When you want to run unattended: `/loop asm-fix-loop` self-paces.
- For a fixed-cadence sweep: `/loop 5m asm-fix-loop`.

## Pre-requisites

- `bash tools/asm-verify/run.sh` completed at least once (report.json exists).
- Docker running (fnverify image built).

## Workflow

### 1. Scan targets

```sh
python -c "
import json
d = json.load(open('tmp/asm-verify/report.json'))
syms = [s for s in d['symbols'] if s.get('score')]
syms.sort(key=lambda s: s['score'] - s['max_score'], reverse=True)
for s in syms[:15]:
    print(f'{s[\"score\"]-s[\"max_score\"]:+6d} {s[\"score\"]:>6}/{s[\"max_score\"]:<6} {s[\"mangled\"][:70]}')
"
```

### 2. Categorize targets

Pick targets by pattern (most effective to least):

| Priority | Pattern | Why |
|----------|---------|-----|
| 1 | Port-only `if (!X::GetInstance()) return;` guards | Binary never null-checks singletons |
| 2 | `LOG_INFO`/`LOG_DEBUG` calls in game code | No-ops under cross-build already, just dead code |
| 3 | `Matrix44` Identity() in ctors/loops | Binary treats matrices as POD |
| 4 | Duplicate/dead code blocks | Copy-paste artifacts, unused computations |
| 5 | `Vec3` temporary constructions in hot loops | Binary uses raw float arithmetic |
| 6 | Singleton `GetInstance()` → direct `m_instance` | Removes guard-variable overhead |
| 7 | Container patterns (std::vector → raw arrays) | Structural, needs RE |

### 3. Dispatch implementer agents

For each target, launch an `implementer` agent with a specific prompt:

```
Fix <ClassName>::<Function> (score <N>/<M>, <ratio>% ratio).
File: <path>. Check for port-only patterns: singleton guards,
LOG calls, redundant field writes. Trim mechanical bloat.
Build: cmake --build build -j$(nproc)
```

- **Run in parallel**: dispatch 2-4 agents simultaneously for independent files.
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

### 5. Re-verify periodically

After each batch of 3-5 commits, run a checkpoint:

```sh
bash tools/asm-verify/run.sh
python -c "..."  # sync triage scores
```

Check for:
- Regressions (score went UP)
- New DIVERGE entries
- Cross-build breakage (compile errors in verify.sh output)

### 6. Triage

Sync triage scores after each verify run. Update `tools/asm-verify/triage.json`
with current scores for any DIVERGE/SUSPICIOUS entries.

### 7. Repeat

Go back to step 1 with fresh scores. Stop when:
- Mechanical wins are exhausted (remaining excess is structural/compiler artifacts)
- Cross-build breaks and can't be quickly fixed
- User says stop

## Anti-patterns

- **Don't rewrite during mechanical sweeps.** Mechanical trimming (guards, LOGs, dead code)
  is fast and safe. Structural rewrites (container changes, algorithm changes) need
  dedicated RE sessions.
- **Don't change signatures casually.** Even `const&` → by-value changes need binary
  nm verification first — they produce different mangled names.
- **Don't touch platform files.** `*SDL.cpp`, `*Posix.cpp`, `*Win32.cpp` are excluded
  from asm-verify and won't affect scores.
- **Don't ignore cross-build breakage.** The cross-build uses GCC 4.4.1 (`-std=gnu++0x`).
  C++11 features (`alignas`, `auto`, range-for, lambdas) silently break it.

## Known false-positive patterns (don't fix)

- `_GLOBAL__I_*` static initializers — always cosmetic (PIC anchor register diff)
- GL 1.x vs GLES2 pipeline in Draw functions — structural, not mechanical
- Compiler-generated std::vector/std::map destructor code — can't change without
  changing the container
- `SmartPtr<T> const&` vs `SmartPtr<T>` — ARM32 ABI identical but different mangling;
  fix only when confirmed by binary nm that binary uses the other form
