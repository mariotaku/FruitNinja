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
- Resolution: 320x480 original, scaled to display
- Assets: convert .tex/.mad/.mmd to standard formats (PNG, OBJ/glTF, etc.)
- Text: keep original .fnt bitmap fonts
- Rendering: replicate original OpenGL ES 1.x fixed-pipeline approach in ES 2.0 shaders

## Original Binary
- ARM32 Little-Endian ELF (Samsung Bada OS), Halfbrick Mortar Engine
- 320x480 portrait, entry point: OspMain

## Conventions
- Ghidra scripts go in `<project root>/ghidra_scripts/`, NOT in `$HOME/ghidra_scripts/`
- RE findings go in `docs/` directory (see `docs/README.md` for index)
- When creating/updating Ghidra scripts via GhidraMCP, also save a copy in `ghidra_scripts/`

## Key Files
- `docs/README.md` — Documentation index
- `docs/classes.md` — Class overview and hierarchy
- `docs/port-plan.md` — Symbol coverage, gap analysis, porting roadmap
- `docs/structs/` — Detailed struct layouts (entities, game, camera, wave, data, hud, other)
- `docs/systems/` — System documentation (state machine, rendering, physics, scoring, wave, menu, save, sound)
- `ghidra_scripts/` — Ghidra analysis scripts and output
- `ghidra_scripts/StructDefinitions.java` — Raw struct definitions (reference only, not runnable)
- `ghidra_scripts/FindTextFunctions_output.txt` — Full function listing (9,624 functions)
