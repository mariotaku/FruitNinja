# asm-verify

Per-symbol ARM Thumb-2 verification of the desktop port against the
original FruitNinja.exe binary. Replaces the eyeball-and-LLM-triage step
that used to run via `asm-inspector` agents on every commit.

## TL;DR

```sh
# One-time: build the Docker image (pulls + bakes in GCC 4.4.1 toolchain).
bash tools/asm-verify/setup.sh

# Drive the pipeline:
bash tools/asm-verify/run.sh

# Read the verdicts:
cat tmp/asm-verify/report.md
```

Pre-requisite: Docker Desktop / Rancher Desktop / native docker. No WSL
distro setup, no toolchain in your home dir — everything lives in the
`fnverify` image.

## Layout

```
tools/asm-verify/
  README.md                 -- this file
  Dockerfile                -- bakes the toolchain + cmake/python3/i386 multilib
  setup.sh                  -- host-side: builds the Docker image
  run.sh                    -- host-side: docker run with project bind-mount
  verify.sh                 -- in-container: rsync->cmake->discover->verify
  toolchain.cmake           -- CMake toolchain (ARM Thumb-2, VFPv3, hard-float)
  cross-build/
    CMakeLists.txt          -- object-only target `fnverify`; lists TUs to verify
    demos/gameover_demo.cpp -- standalone WaveManager::GameOver before/after
  cross-headers/
    fn-cxx11-shims.h        -- `noexcept`/`override`/`nullptr` macros + snprintf
    input/InputManager.h    -- libstdc++ 4.5 vector<func> bug workaround
    util/Delegate.h         -- Mortar::Delegate stub (real one needs noexcept)
    (SDL.h removed -- SDL is now confined to *SDL.cpp files which the
     symbol-diff skill skips and this cross-build never lists.)
  discover-symbols.py       -- nm intersection -> manifest.generated.toml
  export-binary-symbols.py  -- objdump per symbol -> bada-binary/symbols/<sym>.s
  asm-verify.py             -- diff + classify + report writer
  manifest.toml             -- hand-written symbol overrides (precedence)
  manifest.generated.toml   -- auto-discovered (gitignored)
```

## Architecture

```
src/**/*.cpp
       │
       ▼ cross-build/CMakeLists.txt + toolchain.cmake (GCC 4.4.1)
build-bada-cross/**/*.obj  (per-symbol .text sections via -ffunction-sections)
       │
       ▼ discover-symbols.py
manifest.generated.toml  -- intersection of binary nm and port nm
       │
       ▼ export-binary-symbols.py
bada-binary/symbols/*.s  -- objdump -d --start/stop per symbol
       │
       ▼ asm-verify.py
tmp/asm-verify/report.md -- per-symbol verdict + escalation diff hunks
```

Each symbol gets one verdict:

| Verdict | Meaning | Action |
|---|---|---|
| MATCH | normalized diff is empty | accept |
| COSMETIC | only register-rename / literal-pool offsets differ | accept |
| SUSPICIOUS | major opcode delta (added/removed `bl`, `cbz`, `vcmp`...) | escalate to LLM triage |
| DIVERGE | structural mismatch | block |
| UNPAIRED | port symbol not found, or binary symbol missing | manual fix |

## Toolchain pinning

The pipeline uses **Sourcery G++ Lite 2010q1-188 (GCC 4.4.1)** — the upstream
of Samsung's `Sourcery G++ 4.4-157` that built `FruitNinja.exe` (per the
binary's `.comment` section, see `docs/engine/binary-build-evidence.md`).

The toolchain is **NOT vendored in the repo**. The Dockerfile pulls it
from the [Khadas mirror](https://github.com/khadas/buildroot_toolchain_gcc_linux-x86_arm_Sourcery_Gpp_Lite-2010q1)
at image-build time and installs to `/opt/sourcery-2010q1/`. Override
that path via the `FN_TOOLCHAIN_DIR` env var (set in the Dockerfile;
override at run-time with `-e FN_TOOLCHAIN_DIR=/your/path`).

The toolchain binaries are i386 ELF Linux. They can't `stat()` files on
Docker-Desktop's `/work` bind-mount (drvfs / 9p, 32-bit inode overflow).
`verify.sh` works around this by `rsync`'ing the project tree into an
internal named volume (`/staging`, ext4-backed) before invoking the
compiler. Volume contents persist across runs for fast incremental builds.

## Why WSL

asm-verify runs entirely inside WSL Debian. The Win-side cross-build
(historical, removed) used the bada SDK 4.5.3 native toolchain, which is
~5% codegen-different from 4.4.1 (notably, it doesn't emit the tail-call
elision pattern Halfbrick's compiler did). The WSL/4.4.1 path matches the
binary's compiler exactly.

## Iterative loop

Typical inner loop after editing a verified TU:

```sh
$ wsl.exe -d Debian -- bash /c/.../tools/asm-verify/run.sh
=== [1/5] sync project tree to ext4 ===
=== [2/5] cmake configure (4.4.1 toolchain) ===
=== [3/5] cmake build ===
[100%] Built target fnverify
=== [4/5] discover + export ===
  cache hit: skipping 26 symbols (already exported).
=== [5/5] asm-verify ===
  COSMETIC    1
  SUSPICIOUS  25

Report: tmp/asm-verify/report.md
```

~1.5 seconds incremental, no LLM calls. Dispatch an agent only on
SUSPICIOUS+ items (see `.claude/agents/asm-inspector.md`).

## Phase status

- **Phase A** — pipeline plumbing. ✓ done.
- **Phase B** — auto-discovery, parallel, cached. ✓ done.
- **Phase C** — full GCC 4.4.1 via WSL. ✓ done.
- **Phase D** — vendor `asm-differ` proper (replaces our toy normalizer +
  difflib). Reduces SUSPICIOUS false-positive rate from ~85% (today) to ~10%
  by handling register-renames and reloc-vs-abs branch encoding.
- **Phase E** — `asm-triager` agent that takes the SUSPICIOUS/DIVERGE
  hunks (already pre-extracted) and classifies as
  ACCEPT-cosmetic / ACCEPT-deferred / FIX-NEEDED.
- **Phase F** — pre-commit hook + CI gate.
