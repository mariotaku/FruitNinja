# Project: FruitNinja.exe Reverse Engineering & Port

## Port Goal
- **Fidelity first** — match the original game as closely as possible
- Preserve all gameplay mechanics, physics, scoring, timing, and visual behavior
- Preserve same-screen multiplayer (local split-touch)
- Skip only defunct online services (OpenFeint, GameCenter, P2P multiplayer)
- All UI screens and widgets should be ported, not just core gameplay

## Target Platform
- SDL2 + OpenGL ES 2.0
- Build: CMake. Windows: MSYS2/MinGW or MSVC (VS Build Tools 2022). Linux/webOS NDK: GCC.
- Audio: SDL2 raw audio API (no SDL_mixer)
- Language: C++11
- Resolution: 480x320 landscape original, scaled to display
- Assets: convert .tex/.mad/.mmd to standard formats (PNG, OBJ/glTF, etc.)
- Text: keep original .fnt bitmap fonts
- Rendering: replicate original OpenGL ES 1.x fixed-pipeline approach in ES 2.0 shaders

## Building & Running

The project's single configured build dir is `build/`. Configure once with the toolchain you want, then build incrementally with:

```
cmake --build build -j$(nproc)
./build/fruit-ninja.exe
```

Initial configure (one of):

- **MSYS2 / MinGW**: `cmake -G "MSYS Makefiles" -B build`
- **MSVC** (from a Developer Cmd Prompt or after `vcvars64.bat`): `cmake -S . -B build -G "CodeBlocks - NMake Makefiles" -DCMAKE_BUILD_TYPE=Release`

Optional ASAN build setup (clang64 only) is documented in `.claude/agents/implementer.md`.

## Original Binary
- ARM32 Little-Endian ELF (Samsung Bada OS), Halfbrick Mortar Engine
- 480x320 landscape (on portrait 480x800 Bada device, touch/camera rotated 90°), entry point: `OspMain`
- Built with Samsung Sourcery G++ 4.4.1, hard-float ABI (`Tag_ABI_VFP_args: VFP registers`).

## Subagents (in `.claude/agents/`)

Specialised agents handle distinct phases of the RE+port workflow. **Each agent stays in its own lane** — see the "do not do" line in each agent file. Detailed RE rules / GhidraMCP usage live in `re-analyst.md`; detailed implementation rules / coordinate system / fidelity policy live in `implementer.md`.

| Agent | When to use | Outputs |
|-------|-------------|---------|
| `re-analyst`   | Decompile a binary function, resolve a struct, follow GOT pointers, read DAT constants — any task pulling info *out of* `FruitNinja.exe`. | RE reports (struct tables, pseudocode, constants, binary refs); may write to `docs/` |
| `implementer`  | Write or edit C++ from an existing spec in `docs/`. Use after `re-analyst` has produced the spec, or when the spec already exists. | Code in `src/`; build verification |
| `doc-writer`   | Format RE findings into `docs/` markdown when neither re-analyst nor implementer is the right fit (consolidating a conversation into a doc). | Markdown docs only |
| `asm-inspector`| Settle "does the binary really do X?" questions by compiling a minimal C++ test unit with the Bada toolchain and diffing against Ghidra's `disassemble_function` output. Used when the decompiler output is suspicious. | ASM-level verdict + spec for `implementer` to apply |

**Coordination rules:**
- One screen / system at a time. Don't spawn two agents that touch the same files in parallel.
- For new work: spawn `re-analyst` first (RE+spec), then `implementer` (code from spec). Don't ask one agent to do both phases.
- `re-analyst` may write to `docs/` but **must not edit `src/`**.
- `implementer` may read `docs/` but **must not RE new functions** — if the spec is incomplete, return a list of gaps so the user can dispatch `re-analyst` again.
- `doc-writer` writes docs only — does not RE, does not write code.
- `asm-inspector` does not edit `src/`; hands off a precise spec when divergence is found.

## Conventions
- **Only commit when explicitly requested** by the user — do not auto-commit after changes.
- When a value in port code **differs from the original binary**, add a comment explaining the discrepancy: `// DIFFERS: original = 0.01 from DAT_0017633c, using 25.0 as placeholder`.
- Temp/scratch files go in `<project root>/tmp/`, NOT `/tmp` or system temp.
- **`printf` / log strings: ASCII only** — no emoji, no Unicode arrows (`→`/`←`/`↓`/`↑`), no fancy quotes, no en/em dashes, no box-drawing chars. The Windows console codepage mangles non-ASCII bytes regardless of toolchain. Use plain ASCII substitutes (`->`, `--`, `'`, etc.). Comments inside source files can use Unicode freely; this is a runtime-output rule.

## Key Files
- `docs/README.md` — documentation index
- `docs/structs/`  — struct layouts (entities, game, camera, wave, data, hud, screens, ui-widgets)
- `docs/systems/` — systems (state machine, rendering, physics, scoring, wave, menu, save, sound, touch, particles, power-ups, effects, string hash)
- `docs/formats/` — asset formats (.tex, .wav.pcm, .mad/.mmd, .fnt)
- `docs/resources.md` — asset directory structure, XML schemas, loading flow
- `docs/TODO.md` — remaining RE gaps and intentionally skipped items
