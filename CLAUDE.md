# Project: FruitNinja.exe Reverse Engineering & Port

## Port Goal
- **Fidelity first** — match the original game as closely as possible
- Preserve all gameplay mechanics, physics, scoring, timing, and visual behavior
- Preserve same-screen multiplayer (local split-touch)
- All UI screens and widgets should be ported, not just core gameplay

## Defunct features — stub, never skip

Defunct features (OpenFeint, GameCenter, P2P multiplayer, online news, online leaderboards, etc.) are **stubbed**, never skipped. The shape — class, struct fields, vtable, public-API methods, call sites — is **preserved as if the feature were ported**, with method bodies as no-ops that return safe defaults. Each stub carries `// Defunct: <subsystem> — no-op stub; binary @ 0x<addr>` so it's greppable.

Why: skipping breaks the call graph and causes cascading port-side workarounds at every call site. Stubbing keeps the call graph identical to the binary, isolates the "this is a no-op" decision to one spot, and lets the asm-verify pass treat the call sites as cosmetic-only divergences rather than real bugs.

What "stub" means concretely:
- Class exists, vtable layout matches binary (slot order + count).
- Public methods are declared and have bodies. Bodies typically return `0` / `false` / `nullptr` and do nothing observable.
- Singletons' `GetInstance()` returns a valid (possibly empty) instance so call-graph-walking code doesn't crash.
- Network packet structs / handler interfaces are declared so call sites compile.
- A `// Defunct: ...` comment marks each method body.

What "skip" would have meant (NOT what we do): omitting the class entirely so call sites in surrounding code need to be deleted or guarded with `#ifdef`s. Don't do this.

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
- **`Tag_ABI_enum_size: small`** — enums are sized to the smallest underlying integer type (1/2/4 bytes depending on max value), not always 4. Cross-build must compile with `-fshort-enums`. Affects struct layouts that contain enum members.
- **`Tag_ABI_PCS_wchar_t: 2`** — `wchar_t` is 2 bytes. Cross-build uses `-fshort-wchar`.
- `Tag_ABI_align_needed: 8-byte`.
- **`std::list` is 12 bytes in Samsung Bada's libstdc++ vs 8 bytes in Sourcery 2010q1's libstdc++** — the asm-verify cross-toolchain disagrees with the binary on `std::list`-containing struct layouts by 4 bytes per list. Documented limitation; the port-side struct layouts must match Samsung Bada's 12-byte list (which is what `static_assert(offsetof(...))` enforces against the production build, not the cross-build).

## RE record lives in source code, not docs

The port treats **source code as the canonical RE record**:
- Struct layouts live in headers (`src/**/*.h`).
- Function logic lives in `.cpp`.
- Each unimplemented sub-block carries a `// TODO: <binary addr> — <what's missing>` comment that **is the spec** for that gap.
- `// ASM-verified: <ISO-time> binary @ 0x<addr> (asm-inspector)` markers list functions ASM-checked against the binary.
- `// DIFFERS: original = X from DAT_addr, using Y because <reason>` flags deliberate deviations.

This replaces the prior practice of authoring large `docs/*-deep-re.md` / `docs/structs/*.md` / per-class narratives. Those have been removed; only a small reference set survives — file formats, init order, coordinate convention, intentional-skip lists, toolchain provenance.

## Subagents (in `.claude/agents/`)

Specialised agents handle distinct phases of the RE+port workflow. **Each agent stays in its own lane** — see the "do not do" line in each agent file. Detailed RE rules / GhidraMCP usage live in `re-analyst.md`; detailed implementation rules / coordinate system / fidelity policy live in `implementer.md`.

| Agent | When to use | Outputs |
|-------|-------------|---------|
| `re-analyst`   | Decompile a binary function, resolve a struct, follow GOT pointers, read DAT constants — any task pulling info *out of* `FruitNinja.exe`. | A structured report handed back to the caller. The `implementer` pastes the relevant pieces as source-side comments. Does NOT author standalone narrative docs. |
| `implementer`  | Write or edit C++ against existing source-side specs (`// TODO`, `// ASM-verified`, `// DIFFERS`) and the small load-bearing reference docs. | Code in `src/`; build verification |
| `doc-writer`   | Update one of the load-bearing reference docs (formats, init order, skip-lists, etc.) — see whitelist in `doc-writer.md`. NOT for per-class / per-screen RE narratives. | Markdown docs (whitelist only) |
| `asm-inspector`| Settle "does the binary really do X?" questions by compiling a minimal C++ test unit with the Bada toolchain and diffing against Ghidra's `disassemble_function` output. Used when the decompiler output is suspicious. | ASM-level verdict + a `// ASM-verified:` marker line for `implementer` to paste |
| `asm-triager`  | Read the `tools/asm-verify/run.sh` report and classify SUSPICIOUS / DIVERGE rows as ACCEPT-cosmetic / ACCEPT-deferred / FIX-NEEDED. | Updated `tools/asm-verify/triage.json` |

**Coordination rules:**
- One screen / system at a time. Don't spawn two agents that touch the same files in parallel.
- For new work: spawn `re-analyst` first (RE+spec), then `implementer` (code from spec). Don't ask one agent to do both phases.
- `re-analyst` returns a report; it does NOT edit `src/` and does NOT author narrative docs.
- `implementer` reads source-side specs (`// TODO` / `// ASM-verified` / `// DIFFERS`) plus the small load-bearing reference doc set; **must not RE new functions** — if the source-side spec is incomplete, return a list of gaps so the user can dispatch `re-analyst` again.
- `doc-writer` updates only the load-bearing reference doc whitelist (see `doc-writer.md`). Does not RE, does not write code.
- `asm-inspector` does not edit `src/`; hands off a precise spec + `// ASM-verified:` marker line when verdict is Confirmed.

**Dispatch granularity:**
- When a request involves multiple distinct items (e.g. "verify Fruit, Bomb, MenuButton"; "RE these 5 functions"; "fix these 3 bugs"), prefer **subdividing into multiple agent calls** rather than packing everything into one. Smaller scopes return faster and give the user real progress milestones (1-of-N done is visible) instead of an opaque long-running blob.
- Run independent items **in parallel** — issue multiple `Agent` tool calls in a single message when no dependencies exist. CC supports this and it's strictly faster than serialising. Common examples: verifying N independent classes, RE'ing N unrelated functions, exploring N areas of the codebase.
- Use a single combined agent call only when the items genuinely depend on each other (later steps need earlier findings). Don't combine for ergonomic reasons -- the user gets less feedback that way.

**Analyser agents default to background:**
- `re-analyst` and `asm-inspector` runs are typically multi-minute (decompiling many functions, compiling and diffing ASM). Dispatch them with `run_in_background: true` by default so the user can keep working / steer / interrupt while the analysis runs. The harness notifies on completion.
- Foreground only when the next step strictly depends on the result and the user is idle waiting (e.g. a quick targeted single-function verification before applying a fix).
- `implementer` and `doc-writer` typically run foreground since their results immediately drive subsequent code or doc edits.

## Conventions
- **Commit cadence — milestone-driven, with an interactive-debug exception.** Wait for the user to authorise commits (don't auto-commit on a whim), but once a queued/autonomous task list is in flight that authorisation stands. Commit per milestone — one commit per subsystem / class / pipeline item, splitting along natural seams so each commit answers a specific "what did this milestone do?" question. For a multi-class landing that builds green in one pass, still split per class along file boundaries when the classes are independent. Each commit must build clean. Skip splitting during interactive-debug sessions where the user is iterating live — there, batch until the bug is fixed. Subagents (`implementer`, `doc-writer`, `re-analyst`, `asm-inspector`) do **not** commit themselves; the orchestrating session handles git so commits land at the right boundaries. (See `.claude/agents/*.md` for the per-agent handoff guidance.)
- **Source-side comment grammar** (greppable, load-bearing):
  - `// TODO: <binary addr or descriptor> — <gap>` — unimplemented sub-block; the comment is the canonical spec for that gap. Delete the line when you close the gap.
  - `// ASM-verified: <ISO-time UTC> binary @ 0x<addr> (asm-inspector)` — confirmed by ASM diff. Inventory: `grep -rn 'ASM-verified:' src/`.
  - `// DIFFERS: original = X from DAT_addr, using Y because <reason>` — deliberate deviation.
  - `// Defunct: <subsystem> — no-op stub; binary @ 0x<addr>` — feature is permanently dead (online services, P2P MP, etc.) but the call shape and class layout are preserved per the "stub-don't-skip" policy. Inventory: `grep -rn 'Defunct:' src/`.
- **Platform-specific files: `*SDL.cpp` / `*Posix.cpp` / `*Win32.cpp` suffixes + `src/platform/<backend>/` + explicit name list.** Platform-bound code is identified three ways so the `symbol-diff` cross-build can exclude all of it cleanly:
  1. **Suffix** — mirrors the binary's `*Bada` convention (`InputDeviceBada`, `DisplayManagerBada`, `BadaSound`). Per backend:
     - `*SDL.cpp` — SDL2-bound (`GameSDL.cpp`, `SoundManagerSDL.cpp`, `gl_funcsSDL.cpp`, `DisplayManagerSDL.cpp`, `InputTranslatorSDL.{h,cpp}`).
     - `*Posix.cpp` — POSIX-bound (`FileSystemPosix.cpp`, `FilePosix.cpp`).
     - `*Win32.cpp` — Win32-bound (`FileSystemWin32.cpp`, `FileWin32.cpp`).
     Pick exactly one suffix per file; mixed-platform code uses `#ifdef` inside, not the suffix.
  2. **`src/platform/<backend>/` directory** — for backends that come as a multi-file group, the whole subtree is excluded (`src/platform/sdl/`, future `src/platform/posix/`, `src/platform/win32/`).
  3. **Explicit name exclusions** — for files that don't fit either rule (e.g. `src/main.cpp`, the SDL entry point that has no binary counterpart). Listed verbatim in the symbol-diff filter.
  Public headers (`*.h`) must NOT include `<SDL.h>` / `<windows.h>` / `<dirent.h>` or expose backend-specific types directly — use `void*` (with a comment naming the real type), `uint32_t` for handle-IDs, or polymorphic engine bases (`Mortar::IFile*`, `Mortar::IFileSystem*`) whose concrete subclass is the platform .cpp. Cast at the platform boundary inside the suffix-named .cpp. The `symbol-diff` skill applies all three filters, which keeps the diff focused on portable code and means there's no platform stub header to maintain in `tools/asm-verify/cross-headers/`.
- **Temp / scratch / intermediate files go in `<project root>/tmp/`** — `tmp/` is gitignored. This includes:
  - **Session planning / handover notes** (`tmp/next-batch.md`, `tmp/handover-*.md`).
  - **TODO/task scratch markdown** generated during a session.
  - **Per-agent output captures**, `compile_failures.txt`, ad-hoc analysis dumps.
  - **Compile/build logs**, `tmp/symbol-diff/*`, `tmp/asm-verify/*` reports.

  Do NOT commit these. The `docs/` tree is reserved for the small load-bearing reference docs (file formats, init order, coordinate convention, intentional-skip lists, toolchain provenance, this CLAUDE.md, plus `docs/HANDOVER*.md` / `docs/TODO.md` / `docs/port-plan.md` which capture project-wide policy that survives across sessions). Anything else — drafts, working notes, gap-survey snapshots, dispatch-shape proposals — belongs in `tmp/`.
- **`printf` / log strings: ASCII only** — no emoji, no Unicode arrows (`→`/`←`/`↓`/`↑`), no fancy quotes, no en/em dashes, no box-drawing chars. The Windows console codepage mangles non-ASCII bytes regardless of toolchain. Use plain ASCII substitutes (`->`, `--`, `'`, etc.). Comments inside source files can use Unicode freely; this is a runtime-output rule.

## Key Files

The canonical RE record is in `src/`. The surviving `docs/` set is small and load-bearing only — things you cannot derive from code:

- `docs/TODO.md` — project-wide RE backlog and intentional-skip lists.
- `docs/HANDOVER.md`, `docs/HANDOVER-gameplay.md` — onboarding context.
- `docs/port-plan.md` — high-level port intent.
- `docs/resources.md` — asset directory layout, XML schemas, loading flow (data, not derivable from code).
- `docs/source-files.md` — port file → binary symbol cross-reference index.
- `docs/engine/coordinate-system.md` — cross-cutting coordinate convention.
- `docs/engine/binary-static-init.md` — pre-`OspMain` static-init order.
- `docs/engine/binary-build-evidence.md` — toolchain / ABI provenance.
- `docs/engine/online-services-audit.md` — what we intentionally skip and why.
- `docs/engine/string-hash.md`, `docs/engine/font.md`, `docs/engine/particles.md`, `docs/engine/mesh.md`, `docs/engine/baked-string.md`, `docs/engine/localisation.md`, `docs/engine/formats/` — file/data formats.
- `docs/gallery/` — extracted models / textures.
- `docs/README.md` — index for the above.
- `tools/asm-verify/triage.json` — sticky verdicts for asm-verify divergences.

Per-class struct layouts, per-function pseudocode, per-screen RE notes, and `*-deep-re.md` / `*-asm-audit.md` / `*-asm-verify.md` session artifacts have been removed; their content lives in source comments (or has been folded into the surviving format/init docs).
