# New Contributor Onboarding

## What This Project Is

A faithful **reverse-engineered port** of FruitNinja.exe (v1.6.1), a 2010 mobile game originally built on Samsung Bada OS using the Halfbrick Mortar engine. The port runs on modern platforms (SDL2/desktop GL, emscripten/WebGL, Wii/devkitPPC, webOS) while preserving all game mechanics, physics, scoring, timing, and visuals to match the binary.

## Platforms Supported

| Platform | Build Path | Renderer | Notes |
|----------|-----------|----------|-------|
| **Host** (Windows/Linux/macOS) | `build/host` | SDL2 + OpenGL ES 2.0 | Via CMake presets; vcpkg or FetchContent for deps |
| **Web** (Emscripten) | `build/web` | WebGL ES 2.0 | Native emsdk 6.0.0; `tools/web/build.sh` |
| **Wii** | `build/wii` | GX (native graphics) | devkitPPC/libogc; `tools/wii/build.sh` |
| **webOS** | via buildroot NDK | GLES2 | Cross-compile via NDK toolchain |

## Building

**Host (primary dev):**
```bash
cmake --preset host          # one-time configure
cmake --build build/host -j$(nproc)
./build/host/fruit-ninja.exe  # MSYS2 / MinGW
./build/host/Debug/fruit-ninja.exe  # MSVC
```

**Web:**
```bash
tools/web/build.sh
# or
tools/web/rebuild-web.sh --worker
```

**Wii:**
```bash
tools/wii/build.sh
```

See the per-tool README in `tools/` for detailed setup.

## The RE Record Lives in Source Code

Canonical specs are **source-side comments**, not separate docs. Find them via:

| Marker | Meaning |
|--------|---------|
| `// TODO: v1.6.1 0x<addr> (<Symbol>) — <gap>` | Unimplemented sub-block; comment is the spec |
| `// ASM-verified: <ISO-time UTC> v1.6.1 <Symbol> @ 0x<addr> (asm-inspector)` | Confirmed by ASM diff against binary |
| `// DIFFERS: original = X from DAT_addr (v1.6.1 <Symbol> @0x<addr>), using Y because <reason>` | Intentional deviation from binary |
| `// Defunct: <subsystem> — no-op stub; v1.6.1 <Symbol> @ 0x<addr>` | Feature permanently dead (online, P2P, etc.) but call shape preserved |

Inventory grep commands:
```bash
grep -rn 'TODO: v1.6.1\|ASM-verified:\|DIFFERS:\|Defunct:' src/
```

## Load-Bearing Reference Docs

Small set of docs that capture **information not derivable from source**:

| Doc | Purpose |
|-----|---------|
| `docs/README.md` | Index + policy statement |
| `docs/port-plan.md` | Port intent (fidelity-first, defunct = stub, all screens ported) |
| `docs/resources.md` | Asset directory layout + XML schemas |
| `docs/source-files.md` | Port file → binary symbol cross-reference |
| `docs/engine/coordinate-system.md` | Centred-ortho convention (shared across codebase) |
| `docs/engine/binary-static-init.md` | Pre-`OspMain` static init order |
| `docs/engine/binary-build-evidence.md` | Toolchain / ABI (Sourcery 4.4.1, hard-float) |
| `docs/engine/online-services-audit.md` | Defunct subsystem inventory + stub list |
| `docs/engine/string-hash.md`, `font.md`, `particles.md`, `mesh.md`, `localisation.md` | File/data formats |
| `docs/engine/formats/` | Detailed asset format specs (textures, audio, models, fonts) |

**Per-class/per-screen RE narratives have been removed** — they lived in `src/` comments now.

**RE backlog** lives in Claude tasks (`TaskList`), not markdown files.

## Port Policy

- **Fidelity first** — match the binary's gameplay, physics, scoring, timing, visuals.
- **Preserve multi-finger slicing** — simultaneous per-finger blades (up to 8 fingers) are the binary's "multiplayer" model.
- **All screens ported** — UI, menus, game-over, pause, settings, etc., not just core gameplay.
- **Defunct features are stubbed, never skipped** — OpenFeint, GameCenter, P2P multiplayer, online leaderboards, etc. have call shapes preserved but bodies are no-ops. This keeps the call graph identical to the binary and isolates the "dead feature" decision to one place. See `docs/engine/online-services-audit.md`.
- **Defunct UI still drawn when the binary draws it** — if the original draws it (visible on screen in v1.6.1), the port draws it too (as a visible stub). If it's unreferenced dead code, don't instantiate it.
- **No band-aid fixes** — when a bug surfaces, RE the binary correctly and port faithfully. No empirical magic-number fudges or symptom-masking branches.

## Where to Start

1. **Find a task** in the RE backlog (ask the orchestrator for the current list).
2. **RE the binary** if needed (use `re-analyst` agent for binary decompilation; Ghidra decompiler + GhidraMCP tools).
3. **Read existing port code** related to your task (source-side comments carry the specs).
4. **Implement against the spec** (`implementer` agent for code writing; you edit only `.cpp` files under `src/`, not build/test).
5. **Verify** with unit tests (`ctest`) or the game itself (`./build/host/fruit-ninja.exe`).
6. **Commit** (orchestrator handles git; your agent outputs are applied to `src/` files only).

## Testing

- **Unit tests** via `ctest` — cover parsers, loaders, math, collisions, etc.
- **Manual verification** — run `fruit-ninja.exe` and inspect gameplay, menus, rendering via F12 screenshot.
- **HLE verification** — confirm features via bada-hle (the real Bada emulator running the v1.6.1 binary).
- **Cross-build** (`tools/asm-verify/run.sh`) — diffs port asm against binary for cosmetic / real-bug divergences.

## Subagents (Specialized Workflows)

- **`re-analyst`** — Decompiles binary functions, resolves structs, reads memory. Returns specs as source-side comments.
- **`implementer`** — Writes C++ against existing specs (`// TODO`, `// ASM-verified`) and reference docs. Edits `src/` only.
- **`doc-writer`** — Updates load-bearing reference docs (formats, init order, etc.). Never per-class narratives.
- **`asm-inspector`** — Compiles port code with Bada toolchain, diffs ASM vs binary, confirms `// ASM-verified:` markers.

See `.claude/agents/` for detailed policies.

## Quick Links

- **CMakePresets** / build config → `CMakePresets.json`, `CMakeLists.txt`
- **Coordinate system** → `docs/engine/coordinate-system.md`
- **Asset formats** → `docs/engine/formats/`
- **Online services (defunct)** → `docs/engine/online-services-audit.md`
- **Symbol map** → `docs/source-files.md`
- **Original binary** → `FruitNinjaBada/Bin/FruitNinja.exe` (must be locally sourced)
