# ASM-verify pipeline — Phase A complete

End-to-end loop that compiles the port through the original binary's
toolchain (or a close approximation), extracts per-symbol Thumb-2
disassembly, and diffs it against the binary's own asm. Replaces 70-80% of
the eyeball-and-LLM-triage work the asm-inspector agents did during the
verification of commit `9afd5c3`.

## Architecture

```
src/**/*.cpp  ──►  cross-build (CMake + arm-bada-eabi-g++ 4.5.3)
                          │
                          ▼ .obj per TU (with -ffunction-sections)
            tools/asm-verify.py  ──┬──► objdump --section=.text.<sym>
                                    │
            bada-binary/symbols/*.s ┘ (one-time exports via
                                       tools/export-binary-symbols.py)
                          │
                          ▼ normalize + difflib.unified_diff
            tmp/asm-verify/report.md
                          │
       ┌──────────────────┼─────────────────────┐
       ▼                  ▼                     ▼
   MATCH/COSMETIC      SUSPICIOUS              DIVERGE / UNPAIRED
   (auto-accept)       (escalate to            (block / fix)
                        asm-triager agent)
```

## Files

| Path | Role |
|---|---|
| `tools/wsl-armgcc.sh` / `tools/wsl-armgxx.sh` | Compiler wrapper. Translates MSYS-style `/c/...` paths to Windows `C:/...` and shells through to the bada SDK 4.5.3 toolchain (Win32 native). |
| `cmake/toolchain-arm-bada.cmake` | CMake toolchain file. Sets `CMAKE_{C,CXX}_COMPILER` to the wrappers, ar/ranlib to bada SDK binutils, ABI flags to match the binary's `Tag_*` attributes, and disables linking. |
| `cross-build/CMakeLists.txt` | Object-only build target `fnverify`. Compiles selected `.cpp` from the port to ARM Thumb-2 `.o`. |
| `cross-headers/` | Stub headers that satisfy `#include`s the cross compiler can't find or fully parse (SDL.h, util/Delegate.h, input/InputManager.h). |
| `cross-headers/fn-cxx11-shims.h` | Forced-include via `-include`: maps post-4.5 keywords (`noexcept`, `override`, `final`) to era-correct equivalents so GCC 4.4/4.5 can parse the port. |
| `tools/export-binary-symbols.py` | Reads `tools/asm-verify-manifest.toml`, dumps each listed binary symbol via `arm-bada-eabi-objdump`, writes one normalized `.s` per symbol under `bada-binary/symbols/`. |
| `tools/asm-verify-manifest.toml` | List of `[[symbol]]` entries: mangled name, binary VA, byte size, port `.o` path, free-form notes. |
| `tools/asm-verify.py` | Differ + classifier + report writer. Disassembles port `.o` per-symbol, normalises both sides, runs `difflib.unified_diff`, classifies into `MATCH / COSMETIC / SUSPICIOUS / DIVERGE / UNPAIRED`. Writes Markdown report at `tmp/asm-verify/report.md`. |

## Phase A scope (now complete)

Smoke test the loop end-to-end on:

- `src/game/ScoreState.cpp` — two trivial globals (`g_ComboCount` BSS,
  `g_LastSlasher = -1` `.data`). Cross-build verified to produce real ARM
  `.obj` with both symbols.
- `tmp/asm-compare/gameover_demo.cpp` — standalone reduction of
  `WaveManager::GameOver_Before` / `GameOver_After`. Pre-fix and post-fix
  shapes both compile through the cross toolchain.

The pipeline correctly classifies `GameOver_Before` as SUSPICIOUS and the
extracted diff hunk shows the `CALL PowersEnabled / cbz` gate present in
the binary but missing from the pre-fix port — exactly the shape of Fix #5.

```sh
$ python tools/export-binary-symbols.py
$ cmake --build build-bada-cross
$ python tools/asm-verify.py
Report: tmp\asm-verify\report.md
  SUSPICIOUS  _ZN11WaveManager14GameOver_AfterEv  -- major opcode delta (15 +/- lines)
  SUSPICIOUS  _ZN11WaveManager15GameOver_BeforeEv -- major opcode delta (12 +/- lines)
```

## Phase B scope

Open work to scale the pipeline to the full set of 43 symbols verified
during the commit-`9afd5c3` review:

1. **Real `WaveManager.cpp` cross-build.** Currently blocked by libstdc++
   4.5.3 bug in `vector<PROBABILITY_OVERIDE>::push_back` (the type contains
   `std::vector<std::string>`). Two paths:
   - Header-shim PROBABILITY_OVERIDE to use a trivially-movable type alias
     in cross builds (e.g. `vector<const char*>` for `m_Types`).
   - Get the GCC 4.4.1 toolchain working under WSL with TU-staging
     through `/tmp` (the i386 binary fails to stat() files on `/c/...`
     drvfs mounts due to 32-bit inode overflow).

2. **Vendor `asm-differ`** properly (currently using a homegrown
   normaliser + difflib). Replace `tools/asm-verify.py` core with
   asm-differ's matcher + register-rename heuristic. Get smarter
   `MATCH / COSMETIC / SUSPICIOUS` classification.

3. **Auto-discover symbols** by intersecting cross-build mangled names
   with the binary's `nm` output, instead of maintaining a manifest by
   hand. Fall back to manifest only for ambiguous overload sets.

4. **Pre-commit / CI integration.** Add `cmake --build build-bada-cross --target verify-asm` and wire into git hooks. Block commit on
   `DIVERGE` or `UNPAIRED`; warn on `SUSPICIOUS`.

5. **Agent triage integration.** Add a new `asm-triager` agent that takes
   the SUSPICIOUS hunks from the report (already pre-extracted) and
   classifies each as `ACCEPT-cosmetic` / `ACCEPT-deferred` / `FIX-NEEDED`.
   Replaces the heavyweight `asm-inspector` for routine commits.

## Known compatibility issues

- **`noexcept` / `override` / `final`** — port uses these (added in GCC 4.6 / 4.7) but the binary's compiler is 4.4.1. Worked around via `-include cross-headers/fn-cxx11-shims.h`.
- **`std::function` in vector** — libstdc++ 4.5.3 has a known bug instantiating `vector::push_back` for non-trivially-movable element types. Worked around for `InputManager` via a stub header. Same bug surfaces on `WaveManager`'s `vector<PROBABILITY_OVERIDE>` — Phase B work.
- **Win32 toolchain quirks** — bada SDK's `arm-bada-eabi-g++.exe` is i686-mingw32 native; Win-style paths needed (handled in the wrapper).
- **WSL i386 limitations** — Sourcery G++ Lite 2010q1-188 is i386 ELF Linux. Runs under WSL Debian after installing libc6:i386 + libstdc++6:i386, BUT 32-bit inode_t overflows when stat()ing files on the `/c/...` drvfs mount. Phase A pivots to bada SDK 4.5.3 instead. Phase B can stage TUs through `/tmp` to use 4.4.1.

## Toolchain version delta

| Source | Compiler | C++ stdlib | Notes |
|---|---|---|---|
| Original binary | Samsung Sourcery G++ 4.4-157 (= GCC 4.4.1) | newlib + Samsung-patched libstdc++ | per `.comment` section |
| Phase A cross-build | bada SDK 4.5.3 (i686-mingw32 native) | libstdc++ 4.5.3 | works on Windows, codegen ~95% similar |
| Phase B cross-build (target) | Sourcery G++ Lite 2010q1-188 (= GCC 4.4.1) | newlib + Mentor libstdc++ 4.4.1 | exact match upstream of Samsung's fork |

Phase A's 4.5.3 emits one cosmetic divergence vs the binary's 4.4.1 in
empirical testing: tail-call elision (`pop+b` instead of `bl+pop`).
asm-differ's normaliser layer can paper over this so it doesn't surface as
SUSPICIOUS. Currently our toy normaliser does NOT mask it; Phase B asm-differ swap will.

## Quickstart

```sh
# 1. One-time: export binary symbols listed in the manifest.
python tools/export-binary-symbols.py

# 2. Build the cross objects.
cmake -S cross-build -B build-bada-cross -G "MSYS Makefiles" \
      -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-arm-bada.cmake
cmake --build build-bada-cross

# 3. Run the verifier.
python tools/asm-verify.py

# 4. Read tmp/asm-verify/report.md. SUSPICIOUS items have inline diff
#    blocks; pass the report to an asm-triager agent (Phase C) or
#    eyeball it for now.
```
