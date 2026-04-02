# Project: FruitNinja.exe Reverse Engineering

## Binary
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
