# Next Batch — Resume After Context Clear

## Where the project stands (as of HEAD `df38968`, 2026-05-04)

- Cross-build coverage: **98/99 TUs** compile under Sourcery 2010q1. Only `src/Game.cpp` still blocked (CMake-generated `config.h` not in cross-headers).
- Symbol-diff: **968 port / 2916 binary** demangled symbols. After classification (Bada / tinyxml2 / defunct / phantom / false-pos), real gameplay/engine gaps are **160 classes, ~1045 methods**.
- 7 ASM-verified-clean recent landings: AchievementManager, BonusScreen, ScreenEffect, NotificationControl, SpecificOrder, SpeedControl, NotificationControl + popup wiring, Fruit weighted selector field-swap fix, Bomb cleanup chain, MissControl pulse + fall-off + transform.
- 71 `// Defunct:` markers across `src/` covering Network/Leaderboard/OpenFeint/GameCenter classes.
- Policy: `// TODO:` / `// ASM-verified:` / `// DIFFERS:` / `// Defunct:` source-side comment grammar is the canonical RE record. See `CLAUDE.md` and `.claude/agents/*.md`.

## How to resume

Re-run the symbol-diff skill to get a fresh prioritization snapshot before dispatching:

```bash
# Invoke skill defined at .claude/skills/symbol-diff.md
# (cross-compile every src/.cpp, nm, classify, write tmp/symbol-diff/missing_organized.md)
```

Then dispatch one or more of the three pipelines below.

---

## Pipeline A — Gameplay helper-coverage (~150 methods, 6 classes)

**Highest player-visible impact.** Most of these classes already have base implementations in port; the gap is helper / accessor methods that get called from many places. Each method is small (1-20 lines).

### Files & expected method counts (after symbol-diff)

| Class | Missing | Notes |
|---|---:|---|
| `Fruit` | ~23 | accessor surface: `FruitType(name,bool)`, `FruitInfo(int)`, `FruitFactColour`, `FruitFactTexture`, `FruitTypeColour`, etc. |
| `SlashEntity` | ~21 | `SetModColours` full body, `UpdateTouchDown`, `DrawSlice` real, `InitPoints`, `CollideWithEntity` |
| `MenuButton` | ~21 | `AddPeice`, `DeletePeices`, `Shake`, `SetText`, `TouchReleased`, `BeginDraw`, etc. |
| `FruitFactControl` | ~20 | whole class (facts panel on dojo/about) |
| `MissControl` | ~10 | residual draw helpers + transform field_0x14/0x20 (HUDControl3d base fields) |

### Suggested prompt for re-analyst (RE phase)

```
RE the helper / accessor surface of Fruit / SlashEntity / MenuButton /
FruitFactControl / MissControl per the symbol-diff inventory at
tmp/symbol-diff/missing_full_demangled.txt.

For each missing method, give: binary address, signature, brief pseudocode
(1-5 lines), and a // Binary @ <addr> source comment for the implementer.

Group by class; prioritize methods with the most call-graph reach (i.e.
called from already-ported code). Skip MissControl::Draw transform fix
(blocked on HUDControl3d field_0x14/0x20 RE — separate task).

Output: a structured per-class report. Cap at 1500 words. The implementer
will then close the corresponding TODOs across one or more passes.
```

### Suggested prompt for implementer (after RE)

```
Apply RE findings for Pipeline A (gameplay helpers). One implementer pass
per class to keep diffs reviewable:

1. Fruit helpers (~23 methods)
2. SlashEntity helpers (~21 methods)
3. MenuButton helpers (~21 methods)
4. FruitFactControl whole class (~20 methods)
5. MissControl residual helpers (~10 methods)

For each, replace TODO stubs / add missing methods per the spec block
provided. Each method body carries // Binary @ <addr>. Build green is
mandatory (cmake --build build -j4). After each impl pass, dispatch
asm-inspector on a representative function for verification.
```

---

## Pipeline B — Engine plumbing (~80 methods)

Foundational — wires up subsystems many ports already reference but stub.

### Files & expected method counts

| Class | Missing | Notes |
|---|---:|---|
| `InputManager` | ~17 | `LoadConfigFile`, `RegisterInputCallback` bodies (currently stubs) — unblocks GameTaskInput's already-wired callback registers |
| `Touch` | ~12 | `Update` ring-buffer + multi-touch dispatch internals |
| `PSPParticleManager` | ~12 | `AddEmitter` + emitter pool + `EmitterExists` + ClearEmitter — currently partially ported |
| `Mesh` | ~22 | `Draw`, `DrawTriStrip`, `GetNode`, `SetupLighting`, `LoadFromFile` — needed by Fruit::LoadFruitModels |
| `Math` | ~19 | utility free fns (`SinIdx`, `CosIdx`, `Atan2Idx`, `Sqrt`, etc.) |
| `AnimationState` + `AnimationManager` | ~22 | mesh skeletal animation |

### Suggested prompt for re-analyst

```
RE the engine-plumbing subsystems blocking gameplay-helper completion:

- InputManager: full LoadConfigFile + RegisterInputCallback bodies (binary
  addresses cited in GameTaskInitInput RE spec at docs/systems/gameinit-todos.md)
- Touch: Update + ring-buffer flow (binary @ Touch::__UpdateInternal)
- PSPParticleManager: full AddEmitter + EmitterExists + ClearEmitter
- Mesh: Draw + DrawTriStrip + GetNode + SetupLighting + the .mad/.mmd
  file-format-driven LoadFromFile body (binary @ MeshManager::Load)
- Math: SinIdx/CosIdx/Atan2Idx/Sqrt utility free fns (already cited in
  prior misc-unblockers RE)
- AnimationState + AnimationManager: skeletal animation pipeline

For each method: binary address, signature, pseudocode, suggested source
comment. Flag upstream blockers (GLES1->ES2 translations needed for Mesh::
DrawTriStrip; the binary uses fixed-pipeline calls).

Cap at 1500 words. The 3D mesh pipeline (Mesh + AnimationState +
AnimationManager) may be too large for one pass — split if needed.
```

### Suggested prompt for implementer (after RE)

```
Apply Pipeline B engine-plumbing RE findings. Suggested ordering:

1. InputManager + Touch (unblocks GameTaskInput's callback registrations)
2. Math utility free functions (small, used everywhere)
3. PSPParticleManager full impl (unblocks particle emitters in
   ScreenEffect, NotificationControl, BonusScreen reveals)
4. Mesh + AnimationState + AnimationManager 3D pipeline (biggest scope —
   may need separate dispatch). This unblocks Fruit::LoadFruitModels
   (currently a TODO at FruitInfo.cpp:44).

After each impl pass, asm-inspector on a representative function.
```

---

## Pipeline C — HUD widget pack (~62 methods)

Tier-2 widgets needed by mode-progression UI and options/shop screens.

### Files & expected method counts

| Class | Missing | Notes |
|---|---:|---|
| `ProgressionTimerControl` | ~13 | mode-progression bar (Arcade timer) |
| `SliderControl` | ~13 | options / shop sliders |
| `CheckBox` | ~12 | toggle widget |
| `ScreenFadeControl` | ~12 | full-screen fade overlay |
| `VerticalScroller` | ~11 | scroll container for shop / leaderboard list |
| `PowerUpShop` | ~13 | shop screen helpers |

### Suggested prompt for re-analyst

```
RE the HUD widget pack: ProgressionTimerControl, SliderControl, CheckBox,
ScreenFadeControl, VerticalScroller, PowerUpShop.

Each is a HUDControl3d-derived class with: ctor, Update, Draw, OnTouch,
Reset. Per class, give:
  - sizeof + struct layout (vtable + base + subclass fields)
  - vtable slot count + override list
  - Per-method pseudocode (1-5 lines each)
  - DAT-resolved constants (colours, scales, animation timings)

Output: per-class header skeleton + impl spec. Cap at 1200 words.
```

### Suggested prompt for implementer (after RE)

```
Apply Pipeline C HUD widget pack RE findings. One implementer pass per
2-3 classes:

1. CheckBox + SliderControl (smallest, simplest)
2. ScreenFadeControl + VerticalScroller (fade overlays + scroll)
3. ProgressionTimerControl + PowerUpShop (mode-specific)

Each implements a full HUDControl3d subclass with vtable matching binary.
Build green per pass. asm-inspector on one representative method per pass.
```

---

## Coordination notes

- Pipeline A items are in different files — can run all 5 RE/impls in **parallel**.
- Pipeline B has dependency: InputManager+Touch should land first (unblocks GameTaskInput call sites).
- Pipeline C is independent — can run alongside A or B.
- All 3 pipelines combined ≈ 290 methods. Realistically takes 3-4 sessions of multi-agent dispatch.
- After each pipeline lands, **re-run the symbol-diff skill** for an updated gap snapshot.
- Run `bash tools/asm-verify/run.sh` + asm-triager after each pipeline to catch any new FIX-NEEDED divergences.

## Dispatch template (parallel batch start)

```
TaskCreate per pipeline (3 pipelines), then dispatch 3 background re-analysts
with their per-pipeline prompt. After each RE returns, dispatch implementer
foreground (or background — ScreenEffect-style large impls are fine to bg).
After each implementer lands, asm-inspector on a representative function.
```

## Outstanding cross-build hole (low priority)

`src/Game.cpp` cross-build blocked on `#include "config.h"` (CMake-generated). Two-line fix: add stub `config.h` to `tools/asm-verify/cross-headers/` with the project-version macros set to placeholders. Lifts cross-build to 99/99.

## Pre-clear checklist

- [x] Last commit: `df38968` (FileManager cross-build portability)
- [x] Pushed to `main`: `f2c4be5..df38968`
- [x] Working tree clean (Testing/ + bada-binary/ build artifacts only)
- [x] Skill saved: `.claude/skills/symbol-diff.md`
- [x] This resume doc: `tmp/next-batch.md`

After context clear, the user can paste this entire file as the new conversation prompt to resume.
