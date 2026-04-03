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

## Conventions
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

Other scripts:
- `FindTextFunctions.java` — Lists all 9,624 non-thunk .text functions
- `ListClasses.java` — Extracts class hierarchy from symbols
- `StructDefinitions.java` — Raw struct definitions (reference only, not runnable)

## Key Files
- `docs/README.md` — Documentation index (22 files, 3500+ lines)
- `docs/structs/` — Struct layouts: entities, game, camera, wave, data, hud, screens, ui-widgets, gameplay-misc, other
- `docs/systems/` — Systems: state machine, rendering, physics, scoring, wave, menu, save, sound, touch, particles, power-ups, effects, string hash
- `docs/formats/` — Asset formats: textures (.tex), audio (.wav.pcm), models (.mad/.mmd), fonts (.fnt)
- `docs/resources.md` — Asset directory structure, XML schemas, loading flow
- `docs/TODO.md` — Remaining RE gaps and intentionally skipped items
