# Next Batch — Top-10 Gap Targets (Round 2)

## Where the project stands (as of HEAD `a6ccf4b`, 2026-05-04 — post top10-gap-fill merge)

- Cross-build coverage: **109/109 portable TUs** compile under Sourcery 2010q1.
- Symbol-diff: **1152 port / 2916 binary** demangled symbols (Round 1 baseline was 1048).
- Real gameplay/engine gaps: **158 classes / 957 methods** (was 161 / 997).
- Platform-glue exclusion: `*SDL.cpp` / `*Posix.cpp` / `*Win32.cpp` suffixes + `src/platform/sdl/` + explicit `src/main.cpp` — all wired into `.claude/skills/symbol-diff.md`.
- Polymorphic engine bases now in place: `Mortar::Col` (5-slot vtable, ColSphere/Line/AABB), `Mortar::IFile` + `IFileSystem` (8 / 6 slots, IFile_Direct + FileSystem_Direct + Posix/Win32 split). `Mortar::Message` + `MessageListener` confirmed POD (no vtables in binary).
- Implementer agent has new lanes: **Cross-build portability** (no C++11 lambdas/range-for/auto/enum-class/template-using) and **Binary fidelity for tooling** (signature byte-for-byte match, vtable append-only, out-of-line bodies, polymorphic over `void*`, preserve binary typos).

## Round 2 top-10 candidates (sorted by missing-method count)

| # | Class | Missing | Notes / strategy |
|---|---|---:|---|
| 1 | `ActorManager` | 37 | Most missing per Round 1 RE are signature-mismatch (port-side stubs returned `void` where binary returns `Entity*`). Re-RE the surface that didn't land in Round 1: factory delegates, message listeners' real Delegate2 invokes, full Update/Draw bodies. |
| 2 | `SoundManager` | 29 | Lives mostly in `SoundManagerSDL.cpp` already; binary side is `BadaSound` / `MAMAudioController`. **Likely false-positive** — most binary methods are virtual SoundManager interface that the port's SDL backend overrides. Verify by listing the actual missing names. |
| 3 | `Mesh` | 22 | Full 3D pipeline. Round 1 added Defunct annotations + GetBoneLocalTransform; the 22 still missing are the Effect-property machinery the port replaces with direct GLES2. RE which N of the 22 are genuine vs Defunct. |
| 4 | `Math` | 19 | After Round 1 trig wrappers landed, the 19 remaining are mostly `Random::*`, `Vec3` math, `Matrix44` math — utility free fns. Quick-win candidate: out-of-line whatever's still inline. |
| 5 | `InputManager` | 17 | Round 1 already rebuilt as broadcaster; the 17 remaining are likely caller-side filter helpers (ParseAction, ParseKey table entries). Check if they're dead-code per the LoadConfigFile stub. |
| 6 | `AsciiString` | 16 | **DEFERRED in Round 1** (cascade through every embedded-AsciiString struct). Tackle when a struct-layout audit is scheduled — needs SSO + cached hash + non-lexicographic Compare. |
| 7 | `Entity` | 16 | Round 1 added 3 new vtable slots (10/11/12); the 16 remaining likely need the deferred cascade fixes (pure-virtual conversion of Update/Draw/PostUpdate, Init signature change to (void*, long, const Vec3*), CollisionResponse signature change). High blast radius — every subclass override changes. |
| 8 | `FruitCamera` | 14 | Live class (camera shake, follow-entity, perspective setup, debug fly/tilt/zoom). New-RE work; probably small bodies (matrix math + transform). |
| 9 | `HUD` | 14 | Core HUD lifecycle (HUD ctor/dtor, Init/Release/Update/Draw, AddControl/RemoveControl, OnPause, ResetControls, Save, SetToMultiplayerState, Skip). Many are likely already partially ported — RE to spec the gaps. |
| 10 | `AnimationState` | 14 | Skeletal animation pipeline; pairs with `AnimationManager`. Mesh dependency. RE first, then implementer pass — separate dispatch since the 3D mesh pipeline is large. |

## Tier-2 (next 15 — for Round 3)

`FileManager 13` (registry now in place; remaining are static-init / save root), `GameSound 13`, `ItemManager 13`, `PowerUpManager 13`, `PowerUpShop 13`, `ProgressionTimerControl 13`, `SliderControl 13`, `CheckBox 12`, `PSPParticleManager 12`, `ScreenFadeControl 12`, `SlashEntity 12`, `VerticalScroller 11`, `File 10`, `Geometry 10`, `Model 10`, `Touch 10`, `PowerUp 10`, `SystemManager 10`, `ColAABB 9`, `FreeList 9`.

## Suggested dispatch shape (for the next session)

Three pipelines, similar structure to Round 1:

### Pipeline A — quick wins (symbol emission only)
`Math 19` + `InputManager 17` + small annotations on already-ported classes. Most are out-of-line fixes; no new RE needed. Quick-batch as a single implementer call.

### Pipeline B — new RE + impl per class
`FruitCamera 14` + `HUD 14` + `AnimationState 14`. Each gets its own re-analyst pass + implementer. Run all 3 RE in parallel.

### Pipeline C — cascade-prone (sequenced, not parallel)
`Entity 16` cascade (pure-virtual + Init/CollisionResponse signature) — touches every subclass; needs careful sequencing. `AsciiString 16` — touches every embedded-AsciiString struct's offsetof asserts. `ActorManager 37` re-RE — depends on completing the missing-method audit first.

`SoundManager 29` likely needs no work — verify it's the binary's BadaSound interface that the port's SDL backend overrides.

`Mesh 22` is partly resolved by Defunct annotations — re-RE which methods are actually genuine.

## How to resume

```bash
# Check the current symbol-diff snapshot is fresh:
ls -la tmp/symbol-diff/missing_organized.md

# If stale (>1 day old), re-run via the symbol-diff skill:
# (cross-compile every src/.cpp, nm, classify, write tmp/symbol-diff/missing_organized.md)

# Then dispatch one or more pipelines per above.
```

After each pipeline lands, **re-run symbol-diff** for an updated gap snapshot, and run `bash tools/asm-verify/run.sh` + `asm-triager` to catch any new FIX-NEEDED divergences.

## Pre-clear checklist
- [x] Last commit on main: `a6ccf4b` (top10-gap-fill merge)
- [x] Pushed to origin/main: `de8f41e..a6ccf4b`
- [x] Working tree clean (Testing/ + bada-binary/ build artifacts only)
- [x] Round 2 candidates sized + strategy notes in this doc
- [x] Cross-build portability + binary-fidelity rules saved to `.claude/agents/implementer.md` and feedback memory
