# asm-verify

Per-symbol ARM Thumb-2 verification of the desktop port against the original
FruitNinja.exe binary. This is the **per-symbol track (stages 4-8)** of the
pipeline — for the end-to-end map (how this feeds triage, the BinDiff parallel
track, data-file locations, why operand-level is real signal), see
**[`docs/re-pipeline.md`](../../docs/re-pipeline.md)**. This README is the
operational/run detail only.

## Run

```sh
# One-time: build the Docker image (bakes in the GCC 4.4.1 toolchain).
bash tools/asm-verify/setup.sh

# Verify one class (cross-build + diff + classify for *Foo* symbols):
bash tools/asm-verify/run.sh --class Foo

# Full sweep (every symbol in manifest.generated.toml; several minutes):
bash tools/asm-verify/run.sh

# Read the verdicts:
cat tmp/asm-verify/report.md
```

Pre-requisite: Docker (Desktop / Rancher / native). The toolchain is **not
vendored** — the Dockerfile pulls Sourcery G++ Lite 2010q1 at image-build time
and installs to `/opt/sourcery-2010q1/` (override via `FN_TOOLCHAIN_DIR`).
Everything runs in the `fnverify` image; no home-dir toolchain, no WSL distro
setup. The i386 toolchain can't `stat()` the bind-mount, so `verify.sh`
`rsync`s the tree into an ext4 named volume first (persists for fast rebuilds).

## Files

```
README.md                 -- this file
Dockerfile / setup.sh     -- toolchain image (cmake/python3/i386 multilib)
run.sh                    -- host entry: docker run with project bind-mount
verify.sh                 -- in-container: rsync -> cmake -> discover -> verify
toolchain.cmake           -- CMake toolchain (ARM Thumb-2, VFPv3, hard-float)
verify-sources.cmake      -- curated list of .cpp the cross-build compiles
compile-one.sh / check-tu.sh -- single-TU helpers (asm-inspector / preflight)
discover-symbols.py       -- nm intersection -> manifest.generated.toml
export-binary-symbols.py  -- objdump per symbol -> bada-binary/symbols/<sym>.s
asm-verify.py             -- operand-level diff + classify + report writer
classify-divergences.py   -- rank divergences (HIGH/MED/LOW) -> shortlist.md
triage.sh / triage.json   -- sticky per-asm_hash verdicts
asm-verify-hook.sh        -- pre-commit hook entry
manifest.toml             -- hand-written overrides (precedence)
manifest.generated.toml   -- auto-discovered (gitignored)
cross-build/              -- object-only `fnverify` target; TU list + demos
cross-headers/            -- C++11 shims + libstdc++ 4.5 workarounds (no SDL.h)
```

## Subdirectories

- **`bindiff/`** — whole-program BinDiff track (stage 9). See `bindiff/README.md`.
- **`layout/`** — binary class-size / RTTI reference generators (stage 10). See `layout/README.md`.
- **`checks/`** — standalone auxiliary checks (signatures, sources-drift, globals, capstone diff). See `checks/README.md`.

## Verdicts

| Verdict | Meaning | Action |
|---|---|---|
| MATCH | normalized diff is empty | accept |
| COSMETIC | only register-rename / literal-pool offsets differ | accept |
| SUSPICIOUS | major opcode delta (added/removed `bl`, `cbz`, `vcmp`...) | escalate to triage |
| DIVERGE | structural mismatch | block |
| UNPAIRED | port symbol not found, or binary symbol missing | manual fix |

Each verdict is sticky per `asm_hash` in `triage.json`; SUSPICIOUS/DIVERGE rows
go to the `asm-triager` agent (ACCEPT-cosmetic / ACCEPT-deferred / FIX-NEEDED).
Incremental runs are ~1.5s with no LLM calls. Triage definitions:
`.claude/agents/asm-triager.md`.
