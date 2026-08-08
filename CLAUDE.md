# Project: FruitNinja.exe Reverse Engineering & Port

## Response style — short
User reads replies on a phone. Keep answers **simple, short, precise**: lead with the result, no preamble/recap, thought process in 1-3 terse bullets, prefer bullets over paragraphs. Same for subagent prompts (task + constraints tight, no narrative). Long detail only on explicit request.

## Port Goal
- **Fidelity first** — match the original game as closely as possible
- Preserve all gameplay mechanics, physics, scoring, timing, and visual behavior
- Preserve simultaneous multi-finger slicing (per-finger blades, up to 8 fingers) — this *is* the binary's "multiplayer". NOTE: v1.6.1 has **no** same-screen split-screen mode to port; its networked MP was **online P2P (defunct, stubbed)**, and `m_PlayerIdx`∈{0,1,2} is the P2P/EntityTracker partition, NOT a screen-half split (confirmed by the input-path audit, #158).
- All UI screens and widgets should be ported, not just core gameplay

## Defunct features — stub, never skip

Defunct features (OpenFeint, GameCenter, P2P multiplayer, online news, online leaderboards, etc.) are **stubbed**, never skipped. The shape — class, struct fields, vtable, public-API methods, call sites — is **preserved as if the feature were ported**, with method bodies as no-ops that return safe defaults. Each stub carries `// Defunct: <subsystem> — no-op stub; v1.6.1 <Symbol> @ 0x<addr>` so it's greppable.

Why: skipping breaks the call graph and causes cascading port-side workarounds at every call site. Stubbing keeps the call graph identical to the binary, isolates the "this is a no-op" decision to one spot, and lets the asm-verify pass treat the call sites as cosmetic-only divergences rather than real bugs.

What "stub" means concretely:
- Class exists, vtable layout matches binary (slot order + count).
- Public methods are declared and have bodies. Bodies typically return `0` / `false` / `nullptr` and do nothing observable.
- Singletons' `GetInstance()` returns a valid (possibly empty) instance so call-graph-walking code doesn't crash.
- Network packet structs / handler interfaces are declared so call sites compile.
- A `// Defunct: ...` comment marks each method body.

What "skip" would have meant (NOT what we do): omitting the class entirely so call sites in surrounding code need to be deleted or guarded with `#ifdef`s. Don't do this.

**Defunct UI is still DRAWN — when the target binary actually draws it.** Stubbing the *logic* never means dropping the *visuals*. If a defunct feature has in-game UI that **the v1.6.1 target's call graph actually instantiates and draws** — a button, a navigable page in a carousel, a badge, a banner, a menu entry — port that UI as a **visible stub**: it must appear on screen exactly as the original drew it (same texture, position, layer), even though tapping/activating it is a no-op. The handler is a no-op; the draw is faithful.
  - **The test is "does v1.6.1 draw it," not "does the class exist."** A defunct class that is *linked-but-unreferenced* in v1.6.1 (zero call-site xrefs — present only as RTTI/vtable relics from other SKUs/older builds) is **dead code, not in-game UI**. Do NOT instantiate it to force visuals the v1.6.1 binary never shows — that's a band-aid that *adds* divergence. (Concrete case: the game-over fact-board nav arrows. They only appear when >1 page is registered; v1.6.1 registers exactly one page per mode and the multi-page classes — `FruitFactLeaderboard`/`FruitFactRewardsPage`/etc. — have no call sites, so v1.6.1 shows **no arrows**. The port correctly shows none. Restoring them would be a `// DIFFERS:` enhancement, not fidelity.)

## No band-aid fixes

When a port-side bug is identified, the fix is to **RE the binary correctly and port it faithfully**, NOT to apply an empirical workaround that happens to make the visible symptom go away.

Forbidden patterns:
- **Magic-number fudges** — `pos.y - 20` because "it looked right", `scale * 0.7f` to compensate for an oversize bug, hardcoded offsets that aren't in the binary.
- **Symptom-masking branches** — `if (problem_value) { force_correct_value(); }` instead of finding why the value is wrong.
- **Empirical try-this-might-work patches** — guessing at fix values without RE'ing the binary's actual semantic.
- **"Good enough for now" workarounds** that drift from binary behaviour without an explicit `// DIFFERS:` marker explaining why fidelity is intentionally sacrificed.

**Prefer the binary's actual implementation over `std::` / self-substitutions.** When the binary uses a custom structure — `Mortar::MemoryPool`, an intrusive linked list (`Mortar::List<T>`), a fixed/flat object pool — port **that**, not a `std::vector`/`std::map` stand-in. The substitution is the single biggest asm-verify divergence inflator and tends to *force* band-aids (e.g. a `std::vector` reap that truncates orphan particles, then a `!empty()` workaround to hide it — exactly what #76's `std::vector`→`MemoryPool` rework had to undo, dropping `ClearEmitter` 724%→164%). Layout-faithful `std::` uses are fine where the container's byte layout already matches the binary (`std::list` = 8 bytes, `std::map` = 24 bytes); the rule targets *custom* binary structures replaced with std containers. Verify the win with `tools/asm-verify/run.sh --class <X>`.

Required workflow when a port-side bug surfaces:
1. **RE the binary** to find what the binary actually does at the relevant function/field. This is non-negotiable — every fix starts here.
2. **Identify the port's divergence** against that binary baseline — a missing call site, an inverted gate, a wrong field offset, a missing struct member, a swapped argument.
3. **Fix at the root** — port the missing binary code. If a dependency is unported (e.g. `Fruit::KillFruit` semantic), port that too rather than calling a less-correct port-side substitute (`am->Deactivate`).
4. If the fix genuinely cannot land yet (blocked on a larger subsystem port), leave a `// TODO: <binary addr> — <gap>` marker with the binary spec inline, NOT an empirical mitigation.

When a deviation is intentional and binary fidelity is sacrificed (e.g. SDL2 replacing Bada audio backend), it must carry a `// DIFFERS: original = X, using Y because <reason>` marker so future RE doesn't get confused. `// Port specific:` is the SDL/platform variant. Anything else without a marker is a band-aid and must be removed at the root.

## Target Platform
- SDL2 + OpenGL ES 2.0
- Build: CMake. Windows: MSYS2/MinGW or MSVC (VS Build Tools 2022). Linux/webOS NDK: GCC.
- Audio: SDL2 raw audio API (no SDL_mixer)
- Language: C++11
- Resolution: 480x320 landscape original, scaled to display
- Assets: convert .tex/.mad/.mmd to standard formats (PNG, OBJ/glTF, etc.)
- Text: keep original .fnt bitmap fonts
- Rendering: native OpenGL ES 2.0 shader pipeline. The renderer was migrated off the binary's ES1 fixed-function approach — **all** draws (2D quads, 3D meshes, font/text, particles) go through hand-written ES2 shaders (`src/engine/render/Shaders.cpp`, `ShaderProgram`, `Renderer::DrawShaded2D`/`DrawMesh3D`); MVP is a uniform, verts are VBO-backed. Zero fixed-function calls remain, so the emscripten build is **pure WebGL/ES2** (no `LEGACY_GL_EMULATION`). Pixel-identical to the original fixed-function output (the shaders replicate `GL_MODULATE`; 3D is unlit — all meshes `IsLit=false`). NOTE: the webOS `libGLESv1_CM` target still needs moving to an ES2 lib before it links; host + web are done.

## Building & Running

`build/` is a gitignored CONTAINER holding all build trees — `build/host` (the single configured native host build), `build/web` (emscripten), `build/asan` (sanitizer), `build/bada-cross` (asm-verify cross-build). Configure the host build once with the toolchain you want, then build incrementally with:

```
cmake --build build/host -j$(nproc)
./build/host/fruit-ninja.exe          # MSYS2/MinGW
./build/host/Debug/fruit-ninja.exe    # MSVC
```

Initial host configure — **CMake presets (vcpkg, MSVC/Ninja)** are the primary path and mirror the CLion "Debug-Visual Studio" profile (`build/host`):

- One-time: install deps into vcpkg, then point `VCPKG_ROOT` at it. The committed `CMakePresets.json` (`host` / `host-release`) references `$env{VCPKG_ROOT}`; the gitignored `CMakeUserPresets.json` sets the concrete machine path (`host-local`).
  ```
  vcpkg install sdl2 "sdl2-image[libjpeg-turbo]" freetype tinyxml2 --triplet x64-windows
  cmake --preset host          # configure (run from a VS Developer prompt or CLion; needs ninja + MSVC env)
  cmake --build --preset host
  ctest --preset host
  ```
  In CLion: reload using profiles from `CMakePresets.json` and pick `host-local`. vcpkg supplies SDL2 / SDL2_image / FreeType / tinyxml2 (no FetchContent on Windows once installed); CMake falls back to `find_package`→`FetchContent` for any dep not vcpkg-installed and on non-vcpkg platforms. NOTE: never reconfigure a `build/host` that was generated with a *different* generator — delete it first (a stale `_deps/*-subbuild` cached with the old generator fails with `Error: generator : Ninja`).
- Fallback **MSYS2 / MinGW** (no vcpkg): `cmake -G "MSYS Makefiles" -B build/host`

Optional ASAN build setup (clang64 only) is documented in `.claude/agents/implementer.md`. Optional headless-rendering software-GL setup (Mesa llvmpipe) is documented in `tests/README.md`.

## Testing — unit-test new components with CTest

When you add a **new component**, cover it with a **minimum-unit CTest** test wherever practical (register it in `tests/CMakeLists.txt`; run via `ctest` or the test executable). A component that merely compiles is **not yet verified** — a unit test against real asset data or known binary values is what proves the port matches.

Prioritise by **dependency shape**: a component that has **few dependencies of its own but is depended on by many others** (parsers, data-table loaders, string/hash/math utilities, file-format readers) **must** get proper test cases. A bug in a high-fan-in leaf cascades into every consumer and surfaces as a confusing downstream symptom rather than a local failure — e.g. the `StringTable` integer-ID drift showed up as garbled on-screen text several layers away, where `test_localisation` pins it at the source.

Keep tests minimal and deterministic; they must not require GPU/platform state or play audio (stub `SoundManager` / `GameSound`).

## Original Binary
- ARM32 Little-Endian ELF (Samsung Bada OS), Halfbrick Mortar Engine
- 480x320 landscape (on portrait 480x800 Bada device, touch/camera rotated 90°), entry point: `OspMain`
- Built with Samsung Sourcery G++ 4.4.1, hard-float ABI (`Tag_ABI_VFP_args: VFP registers`).
- **`Tag_ABI_enum_size: small`** — enums are sized to the smallest underlying integer type (1/2/4 bytes depending on max value), not always 4. Cross-build must compile with `-fshort-enums`. Affects struct layouts that contain enum members.
- **`Tag_ABI_PCS_wchar_t: 2`** — `wchar_t` is 2 bytes. Cross-build uses `-fshort-wchar`.
- `Tag_ABI_align_needed: 8-byte`.
- **`std::list` is 8 bytes** in the binary (sentinel prev/next only). Earlier project notes claimed 12 bytes assuming post-C++11 list-size caching, but R4 W1 RE proved the binary uses Sourcery 2010q1's pre-C++11 layout. The asm-verify cross-toolchain runs unpatched. `std::map` IS 24 bytes (cached `_M_node_count`) — that part of the policy stays.

## RE record lives in source code, not docs

The port treats **source code as the canonical RE record**:
- Struct layouts live in headers (`src/**/*.h`).
- Function logic lives in `.cpp`.
- Each unimplemented sub-block carries a `// TODO: v1.6.1 0x<addr> (<Symbol>) — <gap>` comment that **is the spec** for that gap.
- `// ASM-verified: <ISO-time UTC> v1.6.1 <Symbol> @ 0x<addr> (asm-inspector)` markers list functions ASM-checked against the binary.
- `// DIFFERS: original = X from DAT_addr (v1.6.1 <Symbol> @0x<addr>), using Y because <reason>` flags deliberate deviations.

This replaces the prior practice of authoring large `docs/*-deep-re.md` / `docs/structs/*.md` / per-class narratives. Those have been removed; only a small reference set survives — file formats, init order, coordinate convention, intentional-skip lists, toolchain provenance.

## Documentation structure (user preference)

When writing or maintaining any doc (READMEs, the load-bearing `docs/` set, agent/skill files):

- **Vertical, not central.** Docs live co-located with the tree they describe — a `README.md` per directory, plus source-side comments — NOT one central overview doc. (The asm-verify pipeline is documented in `tools/asm-verify/README.md`, not a `docs/re-pipeline.md`; web tooling + its runbook in `tools/web/README.md`.) Each level is an entry point that links *down*.
- **Index + pointer, never re-statement.** A doc links to the canonical source for detail; it does not duplicate it. One source of truth per fact (e.g. the web asset hashes live only in `pages.yml`). Cross-file duplication of a rule → keep one copy + a one-line pointer.
- **Concise.** READMEs are short, index-style. Instruction docs (agents/skills) state *rules*, not changelog/log narrative — no dated `task #N, <date>` framing, war-stories, commit-SHA provenance, or "now ENABLED" prose (that belongs in commit messages).
- **Remove stale aggressively.** Outdated / deprecated / day-1 / dangling-reference docs get deleted, not kept "just in case" (git history preserves them). But *verify before deleting* a doc you didn't create — if `src/` cites it as a spec, it's load-bearing, not stale.
- (See also: RE backlog → Claude tasks not repo files; narrative RE → source comments.)

## Subagents (in `.claude/agents/`)

Specialised agents handle distinct phases of the RE+port workflow. **Each agent stays in its own lane** — see the "do not do" line in each agent file. Detailed RE rules / GhidraMCP usage live in `re-analyst.md`; detailed implementation rules / coordinate system / fidelity policy live in `implementer.md`.

| Agent | When to use | Outputs |
|-------|-------------|---------|
| `re-analyst`   | Decompile a binary function, resolve a struct, follow GOT pointers, read DAT constants — **any task pulling info *out of* `FruitNinja.exe`.** NOT for finding/reading/analysing existing PORT source (`src/`) — that is `Explore`/`general-purpose` (see below). | A structured report handed back to the caller. The `implementer` pastes the relevant pieces as source-side comments. Does NOT author standalone narrative docs. |
| `Explore` / `general-purpose` (built-in) | **Finding, reading, mapping, or root-causing existing PORT source (`src/`, tools, build).** Use `Explore` for locate/fan-out searches ("where does boot eager-load happen", "which files call X"); use `general-purpose` for deep root-cause analysis + multi-step port investigations ("why does the FontInterface effect-atlas drop the first N shadow glyphs"). This is the DEFAULT for any port-code understanding task. | A report (findings / blueprint) back to the orchestrator. Read-only. |
| `implementer`  | Write or edit C++ against existing source-side specs (`// TODO`, `// ASM-verified`, `// DIFFERS`) and the small load-bearing reference docs. | Code in `src/`; build verification |
| `doc-writer`   | Update one of the load-bearing reference docs (formats, init order, skip-lists, etc.) — see whitelist in `doc-writer.md`. NOT for per-class / per-screen RE narratives. | Markdown docs (whitelist only) |
| `asm-inspector`| Settle "does the binary really do X?" questions by compiling a minimal C++ test unit with the Bada toolchain and diffing against Ghidra's `disassemble_function` output. Used when the decompiler output is suspicious. | ASM-level verdict + a `// ASM-verified:` marker line for `implementer` to paste |
| `asm-triager`  | Read the `tools/asm-verify/run.sh` report and classify SUSPICIOUS / DIVERGE rows as ACCEPT-cosmetic / ACCEPT-deferred / FIX-NEEDED. | Updated `tools/asm-verify/triage.json` |

**Coordination rules:**
- **Orchestrator-stays-in-its-lane rule.** The main session is the **orchestrator**: it dispatches subagents, sequences their work, applies their patches, runs builds, commits, and answers the user. It does **not** itself open `src/` files to read code, nor read large binary disassembly, nor write code-under-test. Code exploration and RE go to subagents; code edits go to `implementer`; only the small surgical patches that the orchestrator must apply itself (commit-shaping touch-ups, cherry-picking a single agent-supplied snippet) stay in the orchestrator. If you find yourself reading more than a few dozen lines of source or `Grep`-ing across `src/` for understanding, stop and dispatch an `Explore` or `re-analyst` instead — the orchestrator's context is the bottleneck. This applies to both code exploration AND fix application.
- **Builds happen ONLY in the orchestrator, serialized — never in subagents.** All agents share the single `build/host` tree, so two concurrent `cmake --build` invocations **race** and corrupt each other's object/link outputs (false failures; a stray locked `fruit-ninja.exe` also causes `LNK1168`). Subagents (`implementer`, `re-analyst`, `asm-inspector`, …) **edit/analyse only and do NOT build or run tests/the game**; they verify statically and end their report with **what to rebuild + which `ctest` cases to run** (and whether they added/removed a `.cpp` or touched a `CMakeLists.txt`, so the orchestrator knows to reconfigure). The orchestrator runs every build/test one at a time and owns the pass/fail verdict. Corollary: a subagent's own build result is never trustworthy (wrong env + races) — the orchestrator's fresh build is the source of truth. **Prefer a *fresh* build when a prior run churned the tree** (git checkout/stash, `--clean-first`, or many incremental edits) — stale incremental state has repeatedly produced false green/red readings.
- One screen / system at a time. Don't spawn two agents that touch the same files in parallel.
- **`re-analyst` is for the BINARY only.** Port-source exploration/analysis (finding boot-load sites, mapping call sites in `src/`, root-causing a port rendering/logic bug, authoring an implementation blueprint from existing port code) goes to **`Explore`** (locate) or **`general-purpose`** (deep analysis), NOT `re-analyst`. Only reach for `re-analyst` when the question is "what does the BINARY actually do at 0x<addr>". A blueprint that's mostly "read the port's own code and plan the change" is a `general-purpose`/`Explore` task.
- For new work that needs BINARY RE: spawn `re-analyst` first (RE+spec), then `implementer` (code from spec). Don't ask one agent to do both phases.
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
- **Precise staging, never `git add -A`.** Subagent reports list the exact files they changed — stage only those files. `git add -A` bundles unrelated edits (CRLF rewrites, editor temp files, stale triage drift) into commits that should be isolated. The orchestrator reads the agent's report to identify changed files and stages them individually.
- **LOG_* macros need no individual gates.** The cross-build defines `__bada__`, which makes `LOG_INFO`/`LOG_DEBUG`/`LOG_ERROR`/`LOG_WARN` expand to `((void)0)` (see `src/debug/Logger.h` lines 24-30). Individual `#ifndef __bada__` guards around LOG calls are redundant for cross-build scores. Do not add new ones; remove existing ones when working on the containing function.
- **Source-side comment grammar** (greppable, load-bearing). **Every binary-referencing marker MUST carry the binary VERSION + the SYMBOL name + the ADDRESS** — e.g. `v1.6.1 MenuButton::Update @0x0019a860`. A marker that cites an address but **no version is treated as OUTDATED** (presumed stale v1.5.x — the port migrated v1.5.1→v1.6.1 and most addresses moved; this has repeatedly misled RE); re-verify it against the v1.6.1 binary before trusting the address. **When you finish verifying/porting a function, update its marker to the `v1.6.1 <Symbol> @0x<addr>` form.**
  - `// TODO: v1.6.1 0x<addr> (<Symbol>) — <gap>` — unimplemented sub-block; the comment is the canonical spec for that gap. Delete the line when you close the gap. (Version-less / old-address TODOs are suspect — confirm against v1.6.1.)
  - `// ASM-verified: <ISO-time UTC> v1.6.1 <Symbol> @ 0x<addr> (asm-inspector)` — confirmed by ASM diff. Inventory: `grep -rn 'ASM-verified:' src/`.
  - `// ASM-spec v1.6.1 <Symbol> @ 0x<addr>: <behaviour>` — RE'd spec pasted by the implementer (not yet ASM-diffed).
  - `// DIFFERS: original = X from DAT_addr (v1.6.1 <Symbol> @0x<addr>), using Y because <reason>` — deliberate deviation.
  - `// Defunct: <subsystem> — no-op stub; v1.6.1 <Symbol> @ 0x<addr>` — feature is permanently dead (online services, P2P MP, etc.) but the call shape and class layout are preserved per the "stub-don't-skip" policy. Inventory: `grep -rn 'Defunct:' src/`.
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

  **Namespace every tmp/ write under a per-tool subdir** — `tmp/<tool>/…` (e.g. `tmp/asm-verify/`, `tmp/bindiff-out/`, `tmp/symbol-diff/`), never a loose file at `tmp/` root. Any script that emits to `tmp/` must write into its own subdir so the tree stays reviewable and one tool's output can be wiped without touching another's. Loose `tmp/foo.json` at root is a bug — give it a home.

  **Tool output format (suggestion).** A tool's durable output should be a **machine-readable file** (JSON or similar) under `tmp/<tool>/` — that file is the single source of truth, queryable and consumable by other tools/agents. The **human/LLM-facing summary goes to stdout** as concise, ranked markdown/plaintext. Don't make markdown the data *product* (it isn't queryable and drifts); enrich/emit the structured file and render a short ranked summary to the terminal. (Pattern: `classify-divergences.py` enriches `report.json` with per-symbol `cause`/`likelihood` and prints the ranked shortlist to stdout — no markdown artifact.) Apply this when reworking any tool.

  Do NOT commit these. The `docs/` tree is reserved for the small load-bearing reference docs (file formats, init order, coordinate convention, intentional-skip lists, toolchain provenance, this CLAUDE.md, plus `docs/port-plan.md` which captures port intent that survives across sessions). Anything else — drafts, working notes, handover/onboarding snapshots, gap surveys, audits, dispatch-shape proposals — belongs in `tmp/`. **Handover notes and TODO lists are volatile and never belong in the repo**, however tidy they look — they drift out of date and then mislead. Onboarding lives in the top-level `README.md`; policy lives here. **RE backlog lives in Claude tasks (`TaskCreate`/`TaskList`), NOT in any `docs/` file.** The intentional-skip list lives in `docs/engine/online-services-audit.md`.
- **`printf` / log strings: ASCII only** — no emoji, no Unicode arrows (`→`/`←`/`↓`/`↑`), no fancy quotes, no en/em dashes, no box-drawing chars. The Windows console codepage mangles non-ASCII bytes regardless of toolchain. Use plain ASCII substitutes (`->`, `--`, `'`, etc.). Comments inside source files can use Unicode freely; this is a runtime-output rule.

## Key Files

The canonical RE record is in `src/`. The surviving `docs/` set is small and load-bearing only — things you cannot derive from code:

- `docs/README.md` — index for the below.
- `docs/port-plan.md` — high-level port intent.
- `docs/resources.md` — asset directory layout, XML schemas, loading flow (data, not derivable from code).
- `docs/source-files.md` — port file → binary symbol cross-reference index.
- `docs/engine/coordinate-system.md` — cross-cutting coordinate convention.
- `docs/engine/binary-static-init.md` — pre-`OspMain` static-init order.
- `docs/engine/binary-build-evidence.md` — toolchain / ABI provenance.
- `docs/engine/online-services-audit.md` — what we intentionally skip and why.
- `docs/engine/string-hash.md`, `docs/engine/font.md`, `docs/engine/particles.md`, `docs/engine/mesh.md`, `docs/engine/localisation.md`, `docs/engine/formats/` — file/data formats.
- `docs/gallery/` — extracted models / textures.
- `tools/asm-verify/triage.json` — sticky verdicts for asm-verify divergences.

Per-class struct layouts, per-function pseudocode, per-screen RE notes, and `*-deep-re.md` / `*-asm-audit.md` / `*-asm-verify.md` session artifacts have been removed; their content lives in source comments (or has been folded into the surviving format/init docs).
