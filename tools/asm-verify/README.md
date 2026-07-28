# asm-verify

Per-symbol ARM Thumb-2 verification of the desktop port against the original
FruitNinja.exe binary. This is the **per-symbol track (stages 4-8)** of the
end-to-end RE + port pipeline, mapped below.

## Pipeline

Binary + source → cross-compiled `.o` → operand-level ASM diff → ranked real-bug
shortlist → triage. Stages 1-3 (RE → spec → implement) are agent-driven
([`re-analyst`](../../.claude/agents/re-analyst.md) decompiles + writes
source-side `// TODO`/`// ASM-spec` markers; [`implementer`](../../.claude/agents/implementer.md)
codes against them). Stages 4-8 are **this dir** (cross-build, symbol discovery,
diff, classify, triage). Stage 9 = whole-program BinDiff ([`bindiff/`](bindiff/README.md)),
stage 10 = class-layout reference ([`layout/`](layout/README.md)), stage 11 =
single-function claim checks ([`asm-inspector`](../../.claude/agents/asm-inspector.md)).
Operand-level is real signal because the cross-build toolchain (Sourcery 2010q1,
GCC 4.4.1) is the open upstream of the binary's Samsung Sourcery G++ — same
operands = faithful, equivalent-but-different = cosmetic, missing-call / wrong-const = bug.

```
FruitNinja.exe (v1.6.1 ARM32)
  | [1] re-analyst(GhidraMCP)
  v
source-side comments (spec) -- [2] --> [3] implementer -> src/
                                              | [4] cross-compile (Docker, Sourcery 4.4.1)
                                              v
                                         fnverify.a
                          [5] discover-symbols.py + export-binary-symbols.py
                                              v
                                  [6] asm-verify.py -> report.json
                                              v
                          [7] classify-divergences.py -> report.json (cause/likelihood)
                                              v
                          [8] asm-triager -> triage.json (sticky) --(fix loop back to [3])

parallel: [9] bindiff/ (whole-program twins -> ranked CSV)
```

### File locations

| Item | Location | Commits? |
|------|----------|----------|
| Binary v1.6.1 (target) | `FruitNinjaBada/Bin/FruitNinja.exe` | No (gitignored) |
| Cross-build manifest | `manifest.generated.toml` | No (gitignored) |
| Triage verdicts (sticky per `asm_hash`) | `triage.json` | **Yes** (fidelity record) |
| asm-verify report (full sweep) | `tmp/asm-verify/report.json` + `.md` | No (gitignored) |
| asm-verify report (filtered run) | `tmp/asm-verify/report.scoped.json` + `.md` | No (gitignored) |
| Classification (triager input) | `report.json` `cause`/`likelihood` fields + `tmp/asm-verify/suggested-triage.json` | No (gitignored) |
| BinDiff CSV | `tmp/bindiff-out/` | No (gitignored) |
| Class-size / typeinfo reference | `tmp/binary-class-sizes.json`, `tmp/typeinfo-tree.json` | No (gitignored) |

## Run

```sh
# One-time: build the Docker image (bakes in the GCC 4.4.1 toolchain).
bash tools/asm-verify/setup.sh

# Verify one class (cross-build + diff + classify for *Foo* symbols):
bash tools/asm-verify/run.sh --class Foo        # -> report.scoped.json/.md

# Full sweep (every symbol in manifest.generated.toml; several minutes):
bash tools/asm-verify/run.sh                    # -> report.json/.md

# Read the verdicts:
cat tmp/asm-verify/report.md
```

Any `--filter`/`--class`/`--symbol` run writes `report.scoped.*` and leaves the
full-sweep `report.json` alone, so `report.json` always means "the whole
program, as of the last full sweep". Trust it only as far as its mtime: a
cross-build breakage makes `run.sh` fail *without* replacing the previous
report, so a stale-but-green report can outlive the tree it describes.

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
classify-divergences.py   -- rank divergences (HIGH/MED/LOW); enrich report.json cause/likelihood + suggested-triage.json (ranked shortlist -> stdout)
detect-gutted-bada.py     -- find bodies the -D__bada__ cross-build silently guts (see below); JSON -> tmp/gutted-bada/
triage.sh / triage.json   -- sticky per-asm_hash verdicts
asm-verify-hook.sh        -- pre-commit hook entry
manifest.toml             -- hand-written overrides (per-key precedence; incl. `port_mangled` forwarder-vs-body aliases -- see its header)
manifest.generated.toml   -- auto-discovered (gitignored)
cross-build/              -- object-only `fnverify` target; TU list + demos
cross-headers/            -- C++11 shims + libstdc++ 4.5 workarounds (no SDL.h)
rerender-report.py        -- re-apply triage.json to cached report.json (no Docker needed)
run-with-powerup-shim.sh  -- asm-verify wrapper that patches PowerUp.h cross-build guard
```

## Subdirectories

- **`bindiff/`** — whole-program BinDiff track (stage 9). See `bindiff/README.md`.
- **`layout/`** — binary class-size / RTTI reference generators (stage 10). See `layout/README.md`.
- **`checks/`** — standalone auxiliary checks (signatures, sources-drift, globals, capstone diff). See `checks/README.md`.
- **`coverage/`** — binary FUNC coverage analysis (lief + asm-verify report). See `coverage/README.md`.

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

`MATCH-FIX-APPLIED` marks a row whose bug was fixed in a named commit. It is
deliberately NOT one of the four sticky verdicts, so `asm-verify.py` ignores it
for stickiness and the symbol re-scores from scratch on the next full sweep --
the entry exists to preserve provenance, not to pin a verdict.

**Systemic-rationale rule.** An ACCEPT whose rationale is a systemic cause
(GOT/PIC encoding, register allocation, container swap, inlining, rate-split)
must explicitly assert that the cause accounts for the WHOLE divergence, not
merely its largest part. A systemic rationale on a large-body,
high-unmatched-ratio symbol is exactly the shape that hides real bugs
(calibration case: `ScrollingMenu::Update`, accepted at 65% unmatched, concealed
a missing `m_SnapDist` store).

## Gutted `__bada__` bodies

`toolchain.cmake:72` defines `-D__bada__`, which flips ~455 `#if(n)def __bada__`
regions inside `src/`. The lethal shape is WRITE-REMOVED / READ-KEPT: a guard
strips a STORE while the LOAD stays unguarded, so the cross-build diffs a
function against a value frozen at its constructor. Nothing errors, and the
resulting score is meaningless in EITHER direction -- a gutted body scores
falsely CLEAN as easily as falsely divergent -- so it cannot be found by reading
the ranking. `detect-gutted-bada.py` is the detector; `run.sh` calls it at the
end of every sweep.

```
tools/asm-verify/detect-gutted-bada.py                    # both detectors
tools/asm-verify/detect-gutted-bada.py --mode source      # guard scan only
tools/asm-verify/detect-gutted-bada.py --git-rev HEAD~1   # scan a past tree
tools/asm-verify/detect-gutted-bada.py --min-rank LOW --top 60
```

Two complementary detectors, both on by default:

**Source scan** -- preprocessor-aware pass over `src/`, five rules:

| rule | shape |
|------|-------|
| R1 write-removed-read-kept | symbol stored only inside excluded blocks, loaded outside |
| R2 gutted-body | whole body vanishes under `__bada__` while the port arm has code |
| R3 noop-else-arm | the `#else` (`__bada__`) arm is only `(void)x;` / a trivial return |
| R4 definition-removed | the DEFINITION is excluded, so the symbol never pairs -- green by silence |
| R5 binary-object-not-constructed | a guard drops `new <binary class>` from a live binary function |

Plus a **parse-anomaly** channel: `#elif` chains touching `__bada__`, orphan or
unterminated directives, and brace-spanning guards (a `#ifndef __bada__` that
opens a statement and closes it in a second guard -- `MainScreen.cpp`'s
`STATE_CAMERA_ZOOM` if/else is the live example). Anything the line-based scan
cannot model is reported, never dropped silently.

Ranking uses the binary's own symbol table
(`tmp/asm-verify/binary-func-symbols.json`, `--binary-symbols`): a removed body
is HIGH only when the binary actually exports that symbol AND the removed code
touches state the binary also has. Port-only helpers (`UpdateRealtime`, the
`s_ActiveControls` debug registry, `m_RawTouchPos`) drop to LOW, as do
`LOG_*`/GL/SDL blocks, layout `static_assert`/`offsetof` blocks, port-only data
members, and files under `platform/`, `debug/`, `*SDL/Posix/Win32/WebOS/Wii.cpp`.
A field with an `offsetof` assert is treated as a real binary field and promotes
R1 to HIGH.

**Report scan** -- mines `report.json` for symbols whose ported body is a tiny
fraction of the binary's (`max_score` is the binary instruction count, `+` lines
are port-only, so `port = common + plus`; threshold `bin >= 20 && port <= 10%`).
Raw that is ~98 candidates; ranking cross-references
`classify-divergences.py`'s `cause` (`port-stub` / `port-stub-defunct` -> NOISE)
and the source scan's flagged files, leaving a handful of HIGH. Keep both
cross-references -- without the `cause` filter the real signal drowns in stub
rows.

Findings land in `tmp/gutted-bada/findings.json` (the source of truth); stdout
is a short ranked summary.
