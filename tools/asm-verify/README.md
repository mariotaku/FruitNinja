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
| Full binary+port symbol sets + demangle map | `tmp/asm-verify/symbol-index.json` | No (gitignored) |
| Pairing-gap findings | `tmp/asm-verify/signature-mismatch.json` | No (gitignored) |
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
operand_resolve.py        -- --resolve-operands: CALL-target / data-symbol identity (see below)
resolve-eval.py           -- quantify what --resolve-operands adds and costs (JSON -> tmp/asm-verify/resolve-eval/)
classify-divergences.py   -- rank divergences (HIGH/MED/LOW); enrich report.json cause/likelihood + suggested-triage.json (ranked shortlist -> stdout)
audit-config.toml         -- target-specific knobs for the two "looks-verified-but-isn't" audits (thresholds, instruction idioms, marker/verdict vocabularies, exclusion globs)
audit_config.py           -- loader for the above (missing file -> built-in defaults; malformed file -> hard error)
forwarder_rule.py         -- shared INVERTED-PAIRING rule (see below); used by asm-verify.py and detect-forwarders.py
detect-forwarders.py      -- post-hoc inverted-pairing scan over an existing report.json; JSON -> tmp/asm-verify/forwarders.json
signature-mismatch.py     -- PAIRING-GAP guard: same qualified name, different signature => never paired, never diffed (see below); JSON -> tmp/asm-verify/
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
| SUSPICIOUS-FORWARDER | the port side is a tail-call forwarder, so this row diffs a forwarder against a real body -- the score is meaningless | fix the PAIRING (manifest alias), then re-triage |

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

## Inverted pairing (`SUSPICIOUS-FORWARDER`)

Worse than an UNPAIRED row, because the row LOOKS handled: the binary's mangled
name lands on a port FORWARDER instead of the port's real body, so the sweep
diffs a 1-instruction tail call against a 140-instruction binary body **and
still produces a score**. Nothing about the row says the comparison is
meaningless -- it reads as a big DIVERGE and gets triaged as one.

Rule (`forwarder_rule.py`, thresholds in `audit-config.toml`): a PAIRED symbol
whose port side is under `max_port_ratio` of its binary side, with the binary
side at least `min_binary_instrs`, is size-suspect; the port body's SHAPE then
splits it three ways --

| Shape | Meaning | Handling |
|---|---|---|
| `FORWARDER` | <= `max_forwarder_instrs`, last instruction an unconditional branch, no call | verdict becomes `SUSPICIOUS-FORWARDER`; `triage.json` can NOT clear it |
| `EMPTY-STUB` | body is only return scaffolding | annotated only -- unported/defunct, a different disease (`detect-gutted-bada.py`) |
| `SMALLER` | a real but shorter port body | annotated only -- inlining / container swap / terser code all do this honestly |

Nothing is auto-suppressed or auto-aliased: a wrong auto-fix would recreate the
exact problem. Fix a `FORWARDER` row by adding a `port_mangled` alias in
`manifest.toml` pointing at the port's REAL body -- **after reading both sides**
(the direction is not always the obvious one: `GameModifier::ApplyModifier`
forwards to `OnDeferComplete`, while `WaveModifier` has it the other way round).

```
python tools/asm-verify/detect-forwarders.py           # ranked summary
python tools/asm-verify/detect-forwarders.py --all     # incl. EMPTY-STUB / SMALLER
python tools/asm-verify/detect-forwarders.py --check   # exit 1 on any FORWARDER
```

Runs from the stored `report.json` (the diff hunks reconstruct both normalized
streams exactly), so it needs no re-sweep; it enriches each row with
`pairing_suspect` while preserving the report's mtime, which is the freshness
signal the other audits compare against `git log -1 -- src`.

## ASM-verified marker corroboration (`stale-marker-lint.py` Check D)

A `// ASM-verified:` marker is written by the same agent that benefits from it
and **nothing recomputes it**. Check D makes the sweep recompute it: every
`ASM-verified` marker must be PAIRED in `report.json` and must not carry a
contradicting verdict, otherwise it is an ERROR (not a note) and gates
`--check`.

| Outcome | Meaning |
|---|---|
| `CONFIRMED` | paired, verdict in `confirming_verdicts` |
| `SWEEP-CONTRADICTED` | **ERROR** -- the sweep says DIVERGE / FIX-NEEDED / UNPAIRED / SUSPICIOUS-FORWARDER |
| `NAME-NOT-IN-BINARY` | **ERROR** -- the cited symbol resolves to no binary address at all; the marker cites some other function's address |
| `SWEEP-WEAK` | paired, but SUSPICIOUS / ACCEPT-deferred neither confirms nor refutes |
| `SWEEP-CANNOT-VERIFY` | structurally outside the sweep (platform file, TU absent from `verify-sources.cmake`, symbol not in the manifest) -- **its own quiet category, never a silent skip**, because "cannot verify" is exactly the state that has been passing for verified |

Every run prints the coverage line

```
  N markers claim ASM-verified, M confirmed by this run, K cannot be checked
```

so unverified is a number that moves rather than something to hunt for. A
`report.json` older than the newest commit touching `src/` is called out
prominently; a missing one prints "Check D DID NOT RUN -- 0 of the ASM-verified
markers are corroborated". Skip with `--no-sweep-check`, point elsewhere with
`--sweep-report`.

## Operand resolution (`--resolve-operands`, default OFF)

The baseline normalizer keeps immediates, `movw`/`movt` constants, struct
displacements, condition codes, literal-pool `.word` values and branch direction
strict, but discards **call targets** (`bl` -> `CALL`) and **symbol identity**
(`<...>` / `.LANCHOR<n>` / `.L<n>` -> `<SYM>`). So a port that calls libc
`rand()` where the binary draws from `Math::g_random`, or that stores the wrong
vtable in a destructor, normalizes to a byte-identical stream.
`operand_resolve.py` recovers that identity.

```sh
bash tools/asm-verify/run.sh --resolve-operands      # -> report.resolved.json/.md
python3 tools/asm-verify/resolve-eval.py tmp/asm-verify/report.json \
                                         tmp/asm-verify/report.resolved.json
```

**The rule.** An operand that resolves to a RELOCATED slot, or to writable
`.data`/`.bss`, compares by **symbol name**; an operand that resolves to
NON-relocated read-only bytes compares by **value**. Relocation presence rather
than section alone, because a `.rodata` word can hold an address (vtable, jump
table) that legitimately moves, and a logically-const object with a dynamic
initialiser (`_Vector3<float>::UnitZ`) lives in `.bss` where its at-rest zeros
mean nothing. The tool therefore never reads a writable value, so neither the
dead-`.data`-initialiser trap nor its runtime-filled-`.bss` inverse is reachable.
Both sides expose the same distinction: the port `.o` via `objdump -dr`
relocations, the binary via `.got` slots + `.rel.dyn`/`.rel.plt` (PLT thunks and
`b` veneers are followed through to the real target). Names are compared
**demangled** -- the two sides pick different Itanium substitution encodings for
the same entity, and mangled comparison reports that as a divergence.

**Resolve-then-compare, else fall back.** A name is emitted only when the
resolver lands on a real named symbol. `.LANCHOR<n>`, section relocations
(`.rodata.str1.1`) and the `_GLOBAL_OFFSET_TABLE_`-PC delta are NOT annotated --
they differ between builds by construction and annotating them is pure one-sided
noise. A compiler-local outline (`T.<n>`) is annotated as `LOCAL[a,b]`, labelled
by the named symbols its own body touches, since "the binary calls an outline
that touches `Math::g_random` here" is a real checkable fact even though the
outline's name is not comparable.

**Cost of switching it on.** It rewrites the compared stream, so ~83% of
`asm_hash` values change and the sticky triage verdicts keyed on them go stale.
That is why it is opt-in and writes its own `report.resolved.*`; with the flag
off the report is bit-identical to the baseline normalizer (verified: 0 of 2055
rows differ).

**What it still cannot see** -- add to the blind-spot list, do not assume otherwise:

- **Anything inside an unpaired function.** Resolution names the CALLEE at the
  call site; it does not descend. A bug in a port-only `static` helper
  (`RandInt` calling libc `rand`) shows up only as "the caller calls a different
  function", never as the offending instruction.
- **Argument values.** `SFXPlay("a")` vs `SFXPlay("b")` resolves to the same
  name on both sides; string CONTENT is still invisible (string-literal
  relocations are section-relative and deliberately unannotated).
- **Draw ORDER / count of identical calls.** N identical `CALL =f` lines compare
  as a multiset under LCS; a reordering inside a matched run is not surfaced.
- **Everything the baseline is already blind to** -- excluded platform TUs,
  `!__bada__` code, gutted `__bada__` bodies, and unpaired symbols (see
  "Pairing gap" below).
- **Indirect calls.** `blx rN` through a vtable or function pointer has no
  static target; unchanged.

## Pairing gap

`discover-symbols.py` pairs binary<->port on the **exact mangled name**. A binary
symbol that does not pair is **invisible**: it has no row, so it cannot fail, so
it silently reads as "no problem". This is the sweep's largest structural blind
spot and it does not show up anywhere in the report.

Scale, stated honestly. Of 9619 binary `FUNC` symbols, ~2500 pair — but unpaired
symbols skew tiny, so the count understates coverage badly. Measured in **bytes
of non-template project code**, roughly **three quarters is covered** and about a
quarter is undiffed; "~75% of symbols never diffed" is true by count and
misleading as a statement about blindness. Rank by bytes, not by symbol count.

Three causes, three remedies:

1. **Symbol class.** GCC 4.4 emits most C++ bodies as `W` (weak) — inline
   members, template instantiations, implicit ctors/dtors: 5880 of the binary's
   9619. `run_nm()` accepts `T/t/W/w`. `V/v` are weak *objects* (typeinfo,
   vtables, guard vars — all past the end of `.text`) and are correctly refused;
   libstdc++/`__gnu_cxx`/`__cxxabiv1` weak instantiations are filtered by
   `SKIP_PREFIXES` because diffing them measures the two libstdc++ header
   versions, not the port.
2. **TU not cross-compiled.** A symbol whose `.cpp` is absent from
   `verify-sources.cmake` can never pair. Adding a TU is the cheapest yield in
   the whole pipeline — but only for TUs whose port body is a real port of the
   binary body (see the exclusion list at the top of `verify-sources.cmake`).
3. **Signature drift.** Same fully-qualified name on both sides, different
   mangled signature (`Fruit::Draw()` vs `Fruit::Draw(Renderer&)`), so the
   exact-name intersection never fires. `signature-mismatch.py` is the detector;
   `run.sh` calls it after the sweep and prints the ranked top rows.
4. **Scope drift.** Same `Class::Method(params)`, different ENCLOSING scope —
   the port moved a class into or out of a namespace (`LinkedHeap` vs
   `Mortar::LinkedHeap`) or dropped an outer class (`SlashModInfo::SlashSoundMods`
   vs `SlashSoundMods`). The base identities differ, so cause 3's detector is
   blind to it; `signature-mismatch.py`'s second pass matches on the innermost
   `Class::Method` + normalised params instead.
5. **Rename drift.** The port renamed the symbol outright — a corrected binary
   typo (`CheckHasGoneOffsceen` → `CheckHasGoneOffscreen`), a vtable-slot
   rename (`DrawUpdate` → `PostUpdate`), free function → class static. Nothing
   generic can detect this; each one needs a hand-reviewed alias.
6. **Inverted pairing** — worse than unpaired, because it reads as *handled*.
   The port's symbol carrying the binary's exact mangled name is a thin
   forwarder while the real body lives under a port-chosen name, so the sweep
   diffs forwarder-vs-body, scores it, and the body is never looked at
   (`GameModifier::ApplyModifier` = 20 B forwarder, body in `OnDeferComplete`).
   Tell: a paired row whose port side is a handful of bytes against a large
   binary body. The fix is the same `port_mangled` alias.

```sh
python tools/asm-verify/signature-mismatch.py              # report only
python tools/asm-verify/signature-mismatch.py --write-back # apply aliases
```

It ranks by **live undiffed bytes**, not by signature shape — a well-understood
cause (the established `Draw(Renderer&)` port refactor) means the finding is
*cheap to fix*, not that it is unimportant. `--write-back` appends a
`port_mangled = ` alias to `manifest.toml` for the unambiguous 1:1 live
findings; aliasing is preferred to renaming port code (a few TOML lines, no ABI
churn) and the alias states the honest assertion "this port symbol IS the port of
that binary symbol". Findings with a differing param TYPE at equal arity are
never auto-aliased — those may be genuinely different functions, and an alias
there would hide a real gap behind a fake pairing. Symbols already aliased drop
out of the report, so the list drains as it is worked.

A base identity with SEVERAL unpaired overloads per side is not automatically
ambiguous: `overload_pairs` matches them one by one on (ctor/dtor variant,
arity, width-normalised param types), and a key with two claimants on either
side stays unmatched. Without that, every multi-overload base was pinned at
"ambiguous" forever and the list stopped draining on its own. Templates are
still excluded from auto write-back — their bodies are libstdc++-version
sensitive, and the mangled name carries a return type the loose key ignores.

Addresses in the report and in the aliases it writes are **Ghidra convention**
(raw ELF + image base, matching `src/` markers); `binary_addr_raw` keeps the
nm/objdump value. Override the base for another target with
`ASM_VERIFY_IMAGE_BASE`.

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
