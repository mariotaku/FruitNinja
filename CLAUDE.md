# Project: FruitNinja.exe Reverse Engineering & Port

## Port Goal
- **Fidelity first** — match the original game as closely as possible
- Preserve all gameplay mechanics, physics, scoring, timing, and visual behavior
- Preserve same-screen multiplayer (local split-touch)
- Skip only defunct online services (OpenFeint, GameCenter, P2P multiplayer)
- All UI screens and widgets should be ported, not just core gameplay

## Target Platform
- SDL2 + OpenGL ES 2.0
- Build: CMake, cross-platform (MSYS2 / Linux / webOS NDK)
- Audio: SDL2 raw audio API (no SDL_mixer)
- Language: C++11
- Resolution: 480x320 landscape original, scaled to display
- Assets: convert .tex/.mad/.mmd to standard formats (PNG, OBJ/glTF, etc.)
- Text: keep original .fnt bitmap fonts
- Rendering: replicate original OpenGL ES 1.x fixed-pipeline approach in ES 2.0 shaders

## Original Binary
- ARM32 Little-Endian ELF (Samsung Bada OS), Halfbrick Mortar Engine
- 480x320 landscape (on portrait 480x800 Bada device, touch/camera rotated 90°), entry point: OspMain

## Bada SDK Headers
- `bada_SDK/Include/` contains Samsung Bada OS headers (gitignored, not redistributable)
- Use these to resolve Osp:: struct layouts (Point, String, Timer, Application, Form, etc.)
- Key: `FGrpPoint.h` (Point: vtable+x+y), `FAppApplication.h` (Application lifecycle), `FBaseRtTimer.h` (Timer API)

## GhidraMCP (MCP Server)
- Ghidra must be running with GhidraMCP plugin loaded and FruitNinja.exe open
- Provides 100+ tools: decompile, disassemble, rename, read_memory, run_ghidra_script, create_struct, etc.
- All GhidraMCP tools are auto-approved via `.claude/settings.local.json` (`mcp__GhidraMCP__*`)
- Use `read_memory` to resolve GOT pointers and data constants (little-endian ARM32)
- Use `run_ghidra_script` to execute scripts from `~/ghidra_scripts/` (not project dir — copy if needed)
- Use `rename_data` to name DAT_ symbols with meaningful names based on context
- Use `force_decompile` after renaming to see updated decompilation with named symbols

## Coordinate System
- Use the **original centered ortho** directly: `SetupOrtho(160, -160, -240, 240, 2000, -6000)`
- X: +160 (top of landscape) to -160 (bottom) — 320 units
- Y: -240 (left of landscape) to +240 (right) — 480 units
- **Do NOT convert** original positions — use them as-is from the binary (e.g., Play button at (16, -66))
- No `toScreen()` or `orig_to_port()` conversions — the ortho projection handles it
- HUDControl3d::Draw adds Vec3(480, 320, 0) offset internally (matching original)
- Fruit entities and HUD controls share the same centered coordinate space

## Implementation Rules
- **Always follow RE analysis** — implementation must match the reverse-engineered behavior from `docs/engine/` and `docs/` exactly. Do not optimize, simplify, or "improve" the logic unless explicitly instructed. The goal is a faithful 1:1 port, not a better engine.
- **Use documented struct layouts** — field offsets, sizes, and types from Ghidra analysis are authoritative. If a struct has a seemingly redundant field or odd logic, replicate it anyway.
- **Use documented constants** — all magic numbers, timing values, thresholds, and addresses come from the binary. Do not substitute "cleaner" values.
- **Match original call patterns** — if the binary calls `Reset → Scale → Translate → Upload → DrawQuad`, the port must call the same sequence in the same order. Do not merge or skip steps.
- **GL ES 1.x → 2.0 translation only** — the only allowed deviation from original logic is translating fixed-function GL calls (glMatrixMode, glVertexPointer, etc.) to GLES2 shader equivalents. All other logic stays identical.
- **Prompts for implementation agents** are in `src/engine/prompts/` — use these as task specs.

## Analysis ↔ Implementation Tracking
- **Docs**: Add an `<!-- Analysed: YYYY-MM-DDTHH:MM -->` comment at the top of each major section (## heading) in docs/ when analysis is performed or updated.
- **Source**: Add a `// Analysed: YYYY-MM-DDTHH:MM` comment near the top of each implementation file (after includes), matching the docs section it was based on.
- **Staleness check**: If a doc section's analysis date is **newer** than the corresponding source file's date, the implementation may be outdated and should be reviewed/reimplemented with the new findings.
- Use ISO-8601 format to the minute, UTC: e.g. `2026-04-05T11:30`

## Conventions
- **Only commit when explicitly requested** by the user — do not auto-commit after changes
- When a value in port code **differs from the original binary**, add a comment explaining the discrepancy (e.g. `// DIFFERS: original = 0.01 from DAT_0017633c, using 25.0 as placeholder`). This makes it easy to find and fix incorrect values later.
- Ghidra scripts go in `<project root>/ghidra_scripts/`, NOT in `$HOME/ghidra_scripts/`
- The project ghidra_scripts/ directory is added to Ghidra's Script Manager — no need to copy elsewhere
- RE findings go in `docs/` directory (see `docs/README.md` for index)
- Temp/scratch files go in `<project root>/tmp/`, NOT in `/tmp`
- Use `FN_SCREEN_W` / `FN_SCREEN_H` for screen constants (avoid `SCREEN_W`/`SCREEN_H` — MSYS2 conflict)

## Ghidra Scripts
Run in order: FN01 → FN02 → FN03 → FN04 → FN05

| Script | Purpose |
|--------|---------|
| `FN01_ApplyStructs.java` | Core structs: Entity, Fruit, Bomb, SlashEntity, Game, GameTaskState, Camera, Wave, HUD, FRUIT_INFO |
| `FN02_ApplyStructs2.java` | Particle, effects, PowerUp, GameSound, GlesForm, touch input structs |
| `FN03_ApplyStructs3.java` | Mesh/model: LegacyPSPVertexDecl, IVertexSource, HBR0Header, texture/audio headers |
| `FN04_ReplaceTypes.java` | Replace Demangler auto-generated types with /FruitNinja/ typed versions |
| `FN05_ApplyPrototypes.java` | Apply struct types to 70+ function signatures |

| `FN08_UpdateStructs.java` | Updated structs: OspPoint, FruitNinjaApp, FRUIT_INFO fixes, BadaSound, 4 modifier types |

Other scripts:
- `FindTextFunctions.java` — Lists all 9,624 non-thunk .text functions
- `ListClasses.java` — Extracts class hierarchy from symbols
- `StructDefinitions.java` — Raw struct definitions (reference only, not runnable)
- `FN_CountDATs.java` — Count unnamed DAT_ vs named data symbols

## Key Files
- `docs/README.md` — Documentation index (22 files, 3500+ lines)
- `docs/structs/` — Struct layouts: entities, game, camera, wave, data, hud, screens, ui-widgets, gameplay-misc, other
- `docs/systems/` — Systems: state machine, rendering, physics, scoring, wave, menu, save, sound, touch, particles, power-ups, effects, string hash
- `docs/formats/` — Asset formats: textures (.tex), audio (.wav.pcm), models (.mad/.mmd), fonts (.fnt)
- `docs/resources.md` — Asset directory structure, XML schemas, loading flow
- `docs/TODO.md` — Remaining RE gaps and intentionally skipped items
