---
name: zero-divergence-class
description: Byte-faithful port of a single class against the original binary. Two-phase workflow producing a complete pseudocode spec via re-analyst, then mechanical fix application via implementer. Use when a class has multiple suspected divergences and the user demands "no divergence allowed".
user_invocable: true
---

# Zero-Divergence Class Port

A two-phase workflow that takes a single class from "probably mostly ported"
to "byte-faithful against binary" with explicit verification of every method
and field.

## When to invoke

- A class has visible buggy behaviour (touch dead zones, wrong z-order,
  scroll physics off-by-one, etc.) and earlier ad-hoc RE didn't catch the
  full divergence.
- The user says "no divergence allowed" or "fix every single difference".
- Asm-verify reports SUSPICIOUS / DIVERGE for a large fraction of one
  class's methods.
- You've already RE'd ~3 methods piecemeal and want a comprehensive sweep
  to avoid further whack-a-mole.

## When NOT to use

- Single-method bug. Use re-analyst + implementer directly without the spec scaffold.
- Class is defunct (online MP, GameCenter). Use stub-don't-skip per CLAUDE.md.
- Class has no port-side equivalent yet. Use re-analyst to produce a fresh spec,
  then implementer for first-time port — this skill is for *converging* an existing port.

## Pre-requisites

- Binary nm dump at `tmp/symdiff/binary-text.txt`:
  ```bash
  "bada_SDK/Tools/Toolchains/ARM/bin/arm-bada-eabi-nm.exe" --demangle \
    FruitNinjaBada/Bin/FruitNinja.exe \
    | grep -E " [TWtw] " > tmp/symdiff/binary-text.txt
  ```
- GhidraMCP server running (re-analyst uses `decompile_function` / `disassemble_function`).
- Port-side header + cpp exist under `src/` for the target class.

## Workflow

### Step 0 — enumerate the class's methods

```bash
grep -E "^[0-9a-f]+ [TWtw] <ClassName>::" tmp/symdiff/binary-text.txt | sort -u
```

Note: Ghidra addresses = ELF addresses + `0x10000`. nm dump shows ELF
offsets; GhidraMCP expects VAs. Don't confuse the two — failing to apply
the rebase has caused multiple wasted RE passes.

### Step 1 — Phase 1 (re-analyst spec)

Dispatch a re-analyst with `run_in_background: true`:

```
Task: PHASE 1 of zero-divergence <ClassName> pass. Produce COMPLETE
byte-faithful spec for EVERY <ClassName> function. The implementer will
apply your spec mechanically in Phase 2 — no interpretation allowed.

Functions to spec (Ghidra VAs = ELF + 0x10000):
  <list from Step 0, rebased>

Field layout — DEFINITIVE: read accessor functions to lock the meaning of
every field. Resolve any port-side ambiguities.

Output format: tmp/symdiff/<classname>-spec.md with:
  - Field layout table with definitive offsets
  - Per-function: binary pseudocode + port-side spec (with
    `// ASM-verified: <ISO-date> v1.6.1 <Symbol> @ 0xADDR (re-analyst)` markers)
    + notes

Use GhidraMCP decompile_function and disassemble_function heavily. Cite
every binary address.

Budget: thorough. Target ~1500-3000 words.

Special focus:
  - ctor: identify ALL field initializations.
  - Reset / Update / Draw: complete pseudo-C bodies.
  - Any "weak inline" 1-3 instr accessors: confirm field offsets.
  - Phase-broken large methods (e.g. Update with touch physics): break
    into named phases (touch acquire, drag tracking, etc.) with each
    phase's complete pseudo-C body.

If field naming is swapped vs binary semantics, propose ONE definitive
resolution. Be decisive.

Print one-sentence summary to stdout when done.
```

While Phase 1 runs (typically 5-15 minutes), avoid touching the target
class — don't pre-empt the spec with guesses.

### Step 2 — Phase 2 (implementer mechanical application)

Once Phase 1 reports the spec file path, dispatch implementer:

```
Task: Phase 2 of zero-divergence <ClassName> pass. Apply EVERY fix from
tmp/symdiff/<classname>-spec.md mechanically. No interpretation —
match the spec 1:1.

Per CLAUDE.md milestone-split rule: prefer one commit per logical fix,
but combine atomic groups where splitting would leave incorrect
intermediate state. The implementer decides splits based on data flow.

For EACH change:
  - Match spec's pseudo-C exactly — no liberties.
  - Add `// ASM-verified: <ISO-date> v1.6.1 <Symbol> @ 0xADDR (re-analyst)`
    markers per function.
  - Build must stay clean (`cmake --build build/host -j$(nproc) --target
    fruit-ninja`).
  - Where spec has unresolved item, leave precise `// TODO: v1.6.1 0x<addr> (<Symbol>) -- <gap>` marker rather than guessing.

Verification at end:
  - Final clean build.
  - `grep -rn "TODO: v1.6.1" src/<path>/<ClassName>.{cpp,h}` should be 0
    (or list intentional deferrals).

Report under 500 words. List each commit's SHA + one-line summary
+ any blocker.
```

### Step 3 — verify

```bash
cmake --build build/host -j$(nproc) --target fruit-ninja  # must be clean
grep -rn "// ASM-verified.*re-analyst" src/<path>/<ClassName>.{cpp,h}
grep -rn "TODO: v1.6.1" src/<path>/<ClassName>.{cpp,h}   # only intentional deferrals
```

Then ideally re-run asm-verify to confirm the previously-DIVERGE rows
flip to MATCH or are at least classified ACCEPT-cosmetic by the triager.

## Common pitfalls

1. **Address-space confusion**: nm dump uses ELF offsets, GhidraMCP uses
   VAs (= ELF + 0x10000). Symbol search via grep should use nm offsets;
   GhidraMCP calls must rebase. Always disambiguate in the Phase 1
   prompt by including both formats or being explicit.

2. **Field-name swaps**: prior re-analysts may have inverted port-field
   names vs binary. Phase 1 must lock the field layout definitively by
   reading accessor functions (`GetX` returns `*(this + 0xNN)`).

3. **Atomic commit groups**: Phase 2 implementer must NOT split commits
   that would leave incorrect intermediate state (e.g. field-swap C1
   must include all readers' updates). The implementer decides.

4. **"Weak inline" accessors** (1-3 instr like `GetWidth`): usually correct in port; don't re-derive — the field offset is all you need.

5. **Phase-broken methods**: large physics-state-machine functions (`Update(float)`) need phase-by-phase decomposition; ask Phase 1 to break into named sub-specs.

6. **Vtable slot vs symbol address**: callers via vtable+0xNN may
   resolve to different addresses than the symbol's nm-listed address.
   When in doubt, follow the actual vtable indirection in disasm.

## Output artifacts

- `tmp/symdiff/<classname>-spec.md` — the Phase 1 spec (kept; not
  committed; gitignored under `tmp/`).
- N git commits applying the spec, each with ASM-verified markers.
- Updated `tools/asm-verify/triage.json` after re-running asm-verify
  (separate skill).
