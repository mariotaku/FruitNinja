# Session Handover (2026-05-18)

## Repo state

- **Branch**: `bomb-offscreen-fix` (active feature branch, clean).
- **Build**: clean. Both MSYS2/MinGW and MSVC (VS Build Tools 2022) configurations work via the host build dir `build/host` (`build/` is a gitignored container for all build trees: `build/host`, `build/web`, `build/asan`, `build/bada-cross`).
  - MSYS2 configure: `cmake -G "MSYS Makefiles" -B build/host`
  - MSVC configure: `cmake -S . -B build/host -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Debug` (in Developer Cmd Prompt or after `vcvars64.bat`)
  - Build (any toolchain): `cmake --build build/host -j$(nproc)`
  - Run: `./build/host/fruit-ninja.exe` (MSYS2) or `./build/host/Debug/fruit-ninja.exe` (MSVC)

## Recent commits (newest first, last ~10)

```
430705b docs/structs: commit items.md (referenced by source); gitignore scheduler lock
8ea8ec6 src/entities/Fruit: shared-global preloaded models, drop per-Init load
1c17ef9 src/entities/Fruit: binary-faithful struct layout (sizeof = 0x118)
2c125c3 tools/asm-verify: triage 3 new DIVERGE -> ACCEPT-cosmetic
bf7142e src/screens/PauseScreen: UnpauseGame writes GameTaskState, not Game
d7f4d34 src/hud: GameModeScreen + ScreenButton (2 NEW + 1 port landing)
9dd81e4 asm-verify: Bomb divergences (all cosmetic; triage passing)
1503f21 src/entities: Bomb struct (binary-faithful); added Bomb.cpp
1c8e0e2 src/hud: BaseScreen, Screen.cpp stubs + 5 dependent screens ported
13881df src/platform: DisplayManagerSDL minimal stub + GlesFormSDL skeleton
```

## Documentation Index

The canonical RE record lives in **source code**, not docs. Find it via:
- `docs/README.md` — documentation index + policy statement.
- Source-side comments: `// TODO:`, `// ASM-verified:`, `// DIFFERS:`, `// Defunct:`.
- Grep for inventory: `grep -rn 'ASM-verified:\|TODO:\|DIFFERS:\|Defunct:' src/`.

**Load-bearing reference docs** (do NOT add new narrative docs):
- `docs/port-plan.md` — high-level port intent.
- `docs/resources.md` — asset directory layout + XML schemas.
- `docs/source-files.md` — port file → binary symbol cross-reference.
- `docs/engine/*` — file formats, init order, coordinate system, toolchain provenance.
- `docs/gallery/` — extracted asset viewers.

**For RE backlog**: Claude tasks in `TaskList`, not markdown files.

## Active Branch State

Current work: bomb offscreen-fix (bomb fuse particle lifecycle issue). See `tmp/` for session notes.
