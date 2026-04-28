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

## Building & Running (Windows / MSYS2)
- **All build commands must run under MSYS2** — use `C:/msys64/usr/bin/bash.exe` as the shell
- When invoking from the Bash tool, prefix commands with the MSYS2 shell:
  ```
  C:/msys64/usr/bin/bash.exe -lc 'cd /c/Users/Mariotaku/Projects/webosbrew/fruit-ninja && <command>'
  ```
- CMake configure: `cmake -G "MSYS Makefiles" -B build`
- Build: `cmake --build build -j$(nproc)`
- Run: `./build/fruit-ninja.exe`
- Required MSYS2 packages: `mingw-w64-x86_64-gcc`, `mingw-w64-x86_64-cmake`, `mingw-w64-x86_64-SDL2`

## AddressSanitizer build (clang64)

Use this for catching heap UAF, OOB, double-free, stack-buffer-overflow,
and UBSan issues. The default UCRT64 GCC ships sanitizer codegen but no
`libasan` runtime; clang64's `compiler-rt` does ship the ASAN runtime.

Required MSYS2 packages (parallel to UCRT64 — does not conflict):
```
pacman -S mingw-w64-clang-x86_64-clang \
          mingw-w64-clang-x86_64-compiler-rt \
          mingw-w64-clang-x86_64-cmake \
          mingw-w64-clang-x86_64-make \
          mingw-w64-clang-x86_64-SDL2
```

Configure + build:
```
PATH="/clang64/bin:$PATH" /clang64/bin/cmake.exe -G "MSYS Makefiles" \
    -B build-asan -DENABLE_ASAN=ON \
    -DCMAKE_C_COMPILER=/clang64/bin/clang.exe \
    -DCMAKE_CXX_COMPILER=/clang64/bin/clang++.exe \
    -DCMAKE_MAKE_PROGRAM=/clang64/bin/mingw32-make.exe
PATH="/clang64/bin:$PATH" /clang64/bin/cmake.exe --build build-asan -j$(nproc)
```

Run (the ASAN DLL must be on PATH — `/clang64/bin/libclang_rt.asan_dynamic-x86_64.dll`):
```
PATH="/clang64/bin:$PATH" \
ASAN_OPTIONS="halt_on_error=1:abort_on_error=1:detect_stack_use_after_return=1" \
./build-asan/fruit-ninja.exe
```

**Caveats**:
- LeakSanitizer is **not supported** on Windows MinGW — the
  `detect_leaks=1` flag is silently ignored. ASAN catches use-after-free
  and OOB but not pure leaks. Use Dr. Memory if you need leak detection.
- Performance is 2-5× slower than the regular build but still playable.
- Builds into `build-asan/` to keep separate from the regular `build/`.
- Don't mix object files between the two — they have different ABIs
  (clang vs gcc) and different stack layouts.

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
- **`add_struct_field` INSERTS bytes** (shifts subsequent fields) — do NOT use it to fill undefined gaps. Instead, define missing fields manually in Ghidra's Structure Editor. `remove_struct_field` also removes bytes, not just names.

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
- **No shortcuts or abbreviations** — port every binary call as-is. If the binary does `Fruit::FruitType("plum", false)` to resolve a fruit type, the port must call that function with that exact string — NOT `const int FT_PLUM = 7`. Pre-computed constants hide side effects, miss lookup semantics, and drift when FruitInfo data changes. Same applies to `StringHash(...)`, `Math::Abs(...)`, etc. Only inline a call's result if the binary itself inlined a literal.
- **Stub defunct/skipped features, don't remove them** — if a binary feature is defunct (online MP, OpenFeint, GameCenter) or not-yet-ported (ItemManager, FruitSaveData, TutorialControl), keep the SHAPE of the code intact: create the button/struct/call with a no-op callback and a code comment explaining what the binary does and why the port stubs it. Removing the code entirely loses the binary-faithful layout and makes it hard to restore later. Example: GameModeScreen's 4th matchmaker button is still created with stubbed `MatchmakerCallback`, preserving the 4-button layout.
- **Stub un-ported dependencies, don't skip the call** — when implementing a new component that depends on an un-ported helper/manager/singleton, create a header + stub .cpp with the RE'd public API rather than deleting the call site. Preserve the call at the caller; TODO-comment the body. This keeps the binary-faithful control flow so the dependency can light up later for free. Singletons' `GetInstance()` must return a valid (possibly empty) object so the call graph compiles.
- **RE + port the base class first** — if a new component has a base class that isn't ported yet, decompile the base class and implement it (stubs ok) before writing the subclass. Skipping a base class breaks vtable layout: subclass overrides need the right slot indices, and `ActorManager::Update` walks vtable `+0x10` (Update) / `+0x18` (PostUpdate) unconditionally. The bomb fuse-emitter-positioning bug that hid for weeks was caused by skipping `Entity::PostUpdate` (slot 6) in the base class.
- **Preserve binary function boundaries** — if a function exists as its own symbol in the binary, port it as its own function in the port. Don't inline its body at the call site even if it's short. Inlining erases the RE landmark, breaks cross-reference searches (can't `grep` for `FuncName(` anymore), and removes the stub-ability. Example: `Bomb::SetupLighting` is a single `bx lr` no-op in the binary, but the port keeps `SetupLighting(model)` as a standalone function so `Bomb::LoadContent`'s call chain still reads 1:1. Exception: when the binary ITSELF inlined (no symbol in the disassembly).
- **GL ES 1.x → 2.0 translation only** — the only allowed deviation from original logic is translating fixed-function GL calls (glMatrixMode, glVertexPointer, etc.) to GLES2 shader equivalents. All other logic stays identical.
- **No empirical / "looks-right" fixes** — if a port element is visually misplaced, mis-sized, mis-coloured, or behaves differently from the binary, **do NOT add a port-specific offset, multiplier, or hard-coded tweak to compensate**. Empirical fixes are forbidden because they hide the root cause and accumulate drift: each subsequent fix has to reason against ever-more-fudged baselines. Instead, RE the responsible binary function (font baseline math, matrix-stack convention, alignment-flag semantics, etc.) and port it correctly. If RE is incomplete, file a gap with a TODO and revert the visual to "wrong-but-binary-faithful" rather than commit the fudge. The only exceptions are GL ES 1→2 translation (above) and clearly-marked `// Port specific:` workarounds for genuine platform-API differences (SDL audio backend, file I/O paths, etc.) — never for game-logic positioning, sizing, timing, or colours.
- **Fixed timestep** — the original `SystemManager::Update` outputs a hardcoded dt = 1/60 (DAT_0018ae84 = 0x3C888889). The Bada timer fires every 10ms (~100fps). All lerps, physics, and timers are frame-rate-dependent and tuned for this rate. The port uses `SDL_Delay(10ms)` frame pacing. Do NOT compute dt from elapsed time.
- **ARM comparison idioms** — Ghidra decompiles ARM comparisons as `if (-1 < (int)((uint)(A < B) << 0x1f))`. This fires when A >= B (NOT when A < B). A common pitfall: `if (ct < threshold) ct = threshold` in the decompile is actually a MAX clamp (`if ct >= threshold: ct = threshold`), not a MIN clamp.
- **Prompts for implementation agents** are in `src/engine/prompts/` — use these as task specs.

## Analysis ↔ Implementation Tracking
- **Docs**: Add an `<!-- Analysed: YYYY-MM-DDTHH:MM -->` comment at the top of each major section (## heading) in docs/ when analysis is performed or updated.
- **Source**: Add a `// Analysed: YYYY-MM-DDTHH:MM` comment near the top of each implementation file (after includes), matching the docs section it was based on.
- **Staleness check**: If a doc section's analysis date is **newer** than the corresponding source file's date, the implementation may be outdated and should be reviewed/reimplemented with the new findings.
- Use ISO-8601 format to the minute, UTC: e.g. `2026-04-05T11:30`

## Subagents (in `.claude/agents/`)

Three specialised agents handle distinct phases of the RE+port workflow. **Each agent stays in its own lane** — see the "do not do" line in each agent file.

| Agent | When to use | Tools | Outputs |
|-------|-------------|-------|---------|
| `re-analyst` | Decompile a binary function, resolve a struct, follow GOT pointers, read DAT constants — any task that pulls information *out of* `FruitNinja.exe`. | GhidraMCP + Read/Grep | RE report (struct table, pseudocode, constants, binary refs); may write to `docs/` |
| `implementer` | Write or edit C++ from an existing spec in `docs/`. Use after re-analyst has produced the spec, or when the spec already exists. | All tools incl. Bash (MSYS2 build) | Code in `src/`; build verification |
| `doc-writer` | Format RE findings into `docs/` markdown when neither re-analyst nor implementer is the right fit (e.g. consolidating a conversation into a doc). | All tools | Markdown docs only |

**Coordination rules:**
- One screen / system at a time. Don't spawn two agents that touch the same files in parallel.
- For new work: spawn `re-analyst` first (RE+spec), then `implementer` (code from spec). Don't ask one agent to do both phases.
- `re-analyst` may write to `docs/` but **must not edit `src/`**.
- `implementer` may read `docs/` but **must not RE new functions** — if the spec is incomplete, return a list of gaps so the user can dispatch `re-analyst` again.
- `doc-writer` writes docs only — does not RE, does not write code.

## Conventions
- **Only commit when explicitly requested** by the user — do not auto-commit after changes
- When a value in port code **differs from the original binary**, add a comment explaining the discrepancy (e.g. `// DIFFERS: original = 0.01 from DAT_0017633c, using 25.0 as placeholder`). This makes it easy to find and fix incorrect values later.
- Ghidra scripts go in `<project root>/tmp/ghidra_scripts/`, NOT in `$HOME/ghidra_scripts/` and NOT in `<project root>/ghidra_scripts/`. The `tmp/ghidra_scripts/` dir is untracked; scripts there are scratch / one-off (used once to answer a specific RE question, then deleted or swept by routine `tmp/` cleanup). The project does not maintain reusable Ghidra scripts in version control — every analysis run produces fresh ad-hoc helpers.
- RE findings go in `docs/` directory (see `docs/README.md` for index)
- Temp/scratch files go in `<project root>/tmp/`, NOT in `/tmp`
- Use `FN_SCREEN_W` / `FN_SCREEN_H` for screen constants (avoid `SCREEN_W`/`SCREEN_H` — MSYS2 conflict)
- **`printf` / log strings: ASCII only** — no emoji, no Unicode arrows (`→`/`←`/`↓`/`↑`), no fancy quotes, no en/em dashes, no box-drawing chars. The MSYS2 / Windows console codepage mangles non-ASCII bytes (e.g. `→` shows up as `竊・`), making logs unreadable. Use plain ASCII substitutes: `->` instead of `→`, `--` instead of `—`, `'` instead of `'/'`, etc. Comments inside source files can use Unicode freely; this rule is for runtime output only.

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
