# FruitNinja port — docs

## Policy: code is the canonical RE record

The port treats **source code as the canonical record of what the binary does and how the port matches it**:

- Struct layouts live in headers (`src/**/*.h`).
- Function logic lives in `.cpp`.
- Each unimplemented sub-block carries `// TODO: v1.6.1 0x<addr> (<Symbol>) — <gap>` — that comment is the canonical spec for that gap.
- `// ASM-verified: <ISO-time UTC> v1.6.1 <Symbol> @ 0x<addr> (asm-inspector)` lists functions ASM-checked against the binary. Inventory: `grep -rn 'ASM-verified:' src/`.
- `// DIFFERS: original = X from DAT_addr (v1.6.1 <Symbol> @0x<addr>), using Y because <reason>` flags any deliberate deviation.

This `docs/` tree previously held large per-class / per-screen / per-function RE narratives (`docs/structs/`, `docs/entities/`, `docs/screens/`, `docs/functions/`, plus `*-deep-re.md` / `*-asm-audit.md` / `*-asm-verify.md` files under `docs/engine/`). **All of those have been removed** in favour of source-side comments. If you find a dangling `// See docs/<deleted>.md` reference in `src/`, the spec it pointed to now lives in or near the surrounding source — the reference is a stale pointer pending opportunistic cleanup.

## What survives, and why

The remaining docs cover information that **isn't derivable from `src/`**:

### Project-level
- [`HANDOVER.md`](HANDOVER.md) — onboarding context.
- RE backlog lives in Claude tasks (`TaskList`), not in any docs/ file.
- Intentional-skip list lives in [`engine/online-services-audit.md`](engine/online-services-audit.md).
- [`port-plan.md`](port-plan.md) — high-level port intent.
- [`resources.md`](resources.md) — asset directory layout, XML schemas, loading flow (data, not derivable from code).
- [`source-files.md`](source-files.md) — port-file → binary-symbol cross-reference index.
- [`ghidra-re-techniques.md`](ghidra-re-techniques.md) — reusable Ghidra-scripting techniques (RTTI walk, bulk proto application, GOT-DAT resolution, offset scanners). Scripts are user-local, not in-repo.
- `tools/` — build / verification / asset tooling (see [`../tools/README.md`](../tools/README.md)).

### Cross-cutting reference
- [`engine/coordinate-system.md`](engine/coordinate-system.md) — centered-ortho coordinate convention used everywhere.
- [`engine/binary-static-init.md`](engine/binary-static-init.md) — pre-`OspMain` static-init order across translation units.
- [`engine/binary-build-evidence.md`](engine/binary-build-evidence.md) — toolchain / ABI provenance (Sourcery 4.4.1, hard-float).
- [`engine/online-services-audit.md`](engine/online-services-audit.md) — what we intentionally skip and why.

### File / data formats
- [`engine/string-hash.md`](engine/string-hash.md) — Jenkins lookup3 variant (constants + folding).
- [`engine/localisation.md`](engine/localisation.md) — `.str` file format.
- [`engine/font.md`](engine/font.md) — `.fnt` bitmap-font format, g_GameData font slots, `Font::DrawString` transform order, baked-string classes.
- [`engine/particles.md`](engine/particles.md) — particle XML schema + 0xA4-byte template layout.
- [`engine/mesh.md`](engine/mesh.md) — `.mad` / `.mmd` mesh format (HBR0 container).
- [`engine/formats/`](engine/formats/) — `audio.md`, `fonts.md`, `models.md`, `textures.md` (raw asset format docs).

### Asset gallery
- [`gallery/models/README.md`](gallery/models/README.md) — interactive `.mmd` viewer.
- [`gallery/README.md`](gallery/README.md) — gallery build process (models via `dump_meshes.py`, textures streamed at runtime).

## When to add a new doc

Almost never. The default is **persist the finding as a source-side comment** near the code it describes. Add a doc only if:
1. The information is genuinely cross-cutting (touches many files / threads / startup) AND
2. It doesn't fit in a `// TODO:` / function-banner comment.

If you're tempted to add a per-class spec doc, you're going against policy. Put it in the header (`// Binary @ 0x...`) or in a function banner.

## Subagent flow (reminder)

See `.claude/agents/` for full agent specs.

- `re-analyst` returns a structured report; `implementer` pastes it as source comments.
- `implementer` edits `src/` against existing source-side specs + the small load-bearing reference set above.
- `doc-writer` only touches the whitelist above.
- `asm-inspector` produces a `// ASM-verified:` marker line for `implementer`; `asm-triager` reads `tools/asm-verify/run.sh` output and updates `tools/asm-verify/triage.json`.
