# System Verification Pass (2026-04-29)

Six parallel asm-inspector passes against branch `base-class-fidelity`
HEAD `036627a` to surface incomplete-port and divergence issues across
fundamental subsystems.

## Summary table

| System            | Verdict                       | Known divergences | Spec gaps |
|-------------------|-------------------------------|-------------------|-----------|
| ActorManager      | Confirmed (small fixes)       | 2                 | 0         |
| Model/Mesh loader | Confirmed (1 known issue)     | 1 (in HANDOVER)   | 0         |
| Audio system      | Mostly faithful               | 5 (3 HAL, 2 logic) | 3        |
| Slicing           | Geometry confirmed; gameplay scope gaps | 6        | 2         |
| SplatEntity       | Significant divergences       | 22                | 6         |
| Font system       | Re-implementation, not a port | layout + Load + DrawString all wrong | 5 |

## ActorManager — Confirmed (small fixes)

Binary layout matches; class size 0x1048 with the kill-buffer at +0x80c
and counter at +0x100c. All 11 verified methods are binary-faithful.

**Diverges (real, low-risk):**
- `Clear` (`ActorManager.cpp:78-89`) and `Remove` (`:179-182`) call
  `delete entity` directly. Binary @ 0x00170316 / Clear's vtable+0xc
  dispatch calls `entity->Release(); operator delete(entity);`
  explicitly. Currently works because per-class dtors call Release()
  themselves, but this is a base-class-fidelity gap.

**Cosmetic (no port action):**
- Kill buffer is class-resident +0x80c (513 entries) in binary;
  port uses 512-entry stack-local. Functionally identical.
- `Initialise` allocates `numTypes` slots; binary allocates `numTypes+1`
  (unused tombstone).
- `Update` flag sequencing minor difference (binary sets 0x0c once
  before both vtable calls; port sets/clears around each).

**Markers ready:** Initialise@0x0017046c, Destroy@0x0017037c,
Add(int)@0x0017068c, Add(Entity*,long)@0x00170654,
DeactivateAllEntities@0x0016fb44, Update@0x001701f4, Draw@0x0016fe7c,
GetNumEntities (5 overloads), GetEntityFirst/Next/GetEntity/GetEntityIdx.
Do NOT mark `Clear`/`Remove` until the Release-then-delete divergence
is closed.

## Model/Mesh loader — Confirmed

Class layouts (Model 0x58, Mesh 0x7c, BoneBinding 0x44), field offsets,
Skeleton::Bone read order, LoadModel/LoadMesh flow, material loop,
vertex/index stream parsers, Model::Draw sort, Mesh::Draw single-bone
branch — all match binary. Base-class shifts didn't invalidate `e5fa507`.

**Diverges (already in HANDOVER):**
- `Mesh::GetBounds` (`Mesh.cpp:54-72`): port reduces raw `bmin/bmax`;
  binary @ 0x001b0840-0x001b08c0 transforms each bone's bounds through
  the bone's **world** matrix first. Sort key wrong for non-trivial
  hierarchies; negligible for Bomb/Fruit (single-bone, identity bind).
  - **New finding:** missing call is `GetBoneWorldTransform`, NOT
    `GetBoneVertTransform`. Port has the latter helper, needs to add
    the former.

`.mad` parser correctly omitted (binary's animation code is unreferenced
by FruitNinja's assets).

## Audio — Mostly faithful

**Confirmed:** MortarSound 16-byte layout, GameSound 1800-byte pool,
SFXPlay volume formula, sound trigger string keys. Markers ready:
MortarSound@0x0018c6ac, GameSound@0x00129270.

**Diverges (HAL — acceptable):**
- SoundManager internal layout (port: SDL2 voice table; binary:
  `List<MortarSound*>`)
- Music routing: binary uses "SFX with music flag byte" at +0x50;
  port has dedicated `m_MusicVoice`. Port's design is cleaner.

**Diverges (logic — could matter):**
- Mute threshold: port `vol <= 0.0f`; binary `vol < 0.1` (DAT_0018ca48)
- No master-mute byte (DAT_0018ca54) — port has per-channel
  `s_MusicMuted`/`s_SFXMuted`
- `Initialise` doesn't pre-decode soundlist.csv → first-play hitch
- `GameSound::Update` delegate early-return missing (delegates stubbed)

**Spec gaps:**
- DAT_0018ca54 master-mute byte semantics
- soundlist.csv asset
- MakeSFXDelegate_GT callback semantics

## Slicing — Geometry confirmed; gameplay-scope gaps

**Confirmed (geometry math is binary-faithful):** half-velocity
flipSide handling, critical-path velocity (int trunc × 0.5), spin-loop
oneBig branch, impulse clamp + timer, special-fruit detection, impact
emitter rotation, SliceEffect keyframe lerp + rotation construction,
SlashModifier mask OR.

**Diverges (gameplay):**
1. **Critical-hit system never fires.** Port hardcodes
   `isCritical=false` at `Fruit.cpp:772`. No `m_bCriticalEligible`
   field. → no 1.5× impulse, no 2× spin, no ±2 splat bonus, no dual
   ±60° AddSlice, no crit MissControl
2. **Clean-Slice SFX double-trigger.** `Fruit.cpp:822-829` AND AddSlice
   both fire; binary only via AddSlice
3. **Clean-Slice RNG gate wrong.** Port: 1/3 single. Binary: compound
   `Rand32(3)==0 && Rand32(3)==0` (~1/9)
4. **CollisionResponse arg drop** (player-index/MP-score, SP-only)
5. **PowerUp mask branches missing** (mask 0x01-0x10, currently inert
   since PowerUpManager unported)
6. **Ghost trail / combo / swipe-SFX timer** — entire field group
   not ported

**Spec gaps:**
- SlashEntity full 0x180 field layout
- WaveManager critical-chance RNG ladder @ 0x00178100..0x001781d8

## SplatEntity — Significant divergences

22 findings. Critical (visual/behavior):
- `m_AxisB` scale = 0.25 in port, 0.5 in binary (wrong aspect ratio)
- Colour lerp lives in Update in binary, port re-computes it in Draw
  every frame (no captured drift)
- Per-splat-type slide-rate table missing (port uses single scalar)
- Golden-splat angle realign + axis rebuild on type 4/5 missing
- DrawActiveSplats missing global Vec3 translate (splats render at
  world origin instead of offset)
- **Splat-landing SFX trigger missing entirely** (4-float timer block
  + SFXPlay)
- Scale stored as 2 independent axes in binary, port collapses to
  one Vec3
- `m_bFlipV` is actually a horizontal mirror (cosmetic naming bug)

Architectural (not breaking):
- Pool model: binary uses fixed `SplatEntity[N]` array; port uses
  MemoryPool free-list
- `NumActiveSplats` is O(1) cached in binary, O(N) walk in port
  (called 3x/frame)

**Spec gaps for re-analyst:**
- ~~`DAT_0017fd58 + 0x78` per-type slide-rate table~~ — RESOLVED 2026-04-29
  (`{2.5, 2.5, 2.5, 2.9, 0, 0}` at `0x001bd08c`)
- ~~`DAT_0017fad4 + 0x60` per-type post-landing scale~~ — RESOLVED
  (`{1.6 x4, 2.9 x2}` at `0x001bd074`)
- ~~GOT+0x420 translation Vec3~~ — DEBUNKED 2026-04-29: misread of the
  decompile. `0x0024d620` is `_Vector3<float>::Zero` per-TU sentinels.
  Each entity drawer's `MatrixStack::Translate(scaledVec)` resolves to
  `(0,0,0) * scalar = no-op`. Camera shake is already baked into the
  view matrix via `FruitCamera::SetupPerspective`. No port fix needed.
- `_GLOBAL__I_SplatEffect.cpp` at 0x00180530 — RE'd 2026-04-29:
  doesn't load s_SplatTex; loads matrix/Vec3 statics. Texture loaded
  by separate LoadContent.
- `TintColour` semantics — RE'd 2026-04-29: 3-arg per-channel form
  `(Colour* dst, Colour* src, float* tintRGB)`. Cross-cutting; tracked
  separately.
- ~~`field_0x74` SSMP flag semantics~~ — RESOLVED: pure SSMP gate; port
  hard-wires false, drops the horizontal-gravity branch.
- **Splat-landing SFX (Pulp-drip-1/2) +0x28 arming site** — partial RE
  2026-04-29; arming lives outside UpdateActiveSplats (likely in vtable
  +0x14 callback or MakeSplat). NEEDS another re-analyst pass before
  the SFX can fire.
- **`PlaySplat @ 0x0017f5ec`** — fully RE'd; per-impact SFX with 6
  variants + self-arming `+= 0.5f`. PORTABLE NOW (separate from the
  blocked Pulp-drip ambient block).

## Font — Re-implementation, not a port

Class layout, parser pipeline, draw backend all completely diverged
from binary. Currently produces visually-similar output but is not
binary-faithful at any layer.

**Class layout wrong:** binary is 0x438 bytes (`CharTemplate*` at
+0x000, 256-entry lookup table at +0x004, `Page*` at +0x408,
`Kerning*` at +0x410, `lineHeight/base` at +0x424/0x428,
`vector<vector<QUADCUSTOMVERTEX>>` at +0x42c). Port has
`FontGlyph m_Glyphs[256]` + ad-hoc fields. **No field offset matches.**

**Load (`Font.cpp:63-142`):** fopen+fgets+strstr line scanning vs
binary's slurp+tokenize pipeline. Glyph metrics stored raw vs binary's
normalized-by-`lineHeight` float. **No kerning support at all.** No
vertex-buffer pre-allocation.

**DrawString (`Font.cpp:199-523`):**
- Per-glyph `Renderer::DrawQuad` loop vs binary's batched
  `Mesh::DrawTriStrip`
- Raw `glEnable(GL_BLEND) / glDisable(GL_DEPTH_TEST)` (binary makes
  zero GL calls)
- Color tag syntax: `[FFFFFF]...[/]` vs binary's `<color=RRGGBB>...</color>`
- Byte iteration vs binary's `Utf8StringIterator` (multi-byte aware)
- Vertical-alignment math diverges for multi-line
- Skips clipRect entirely

**Spec gaps:**
- `Utf8StringIterator` ABI
- `WordWrap::IsLineFeed` policy
- `GetCharTemplate/GetKerning/GetPage` accessors
- `Mesh::DrawTriStrip` signature
- `MortarRectangleDec` clip-rect math

## Recommended next-step order

**Tier 1 — Low-risk, immediate (this session):**
1. ActorManager: switch `Clear`/`Remove` to `entity->Release(); delete entity`
2. Mesh: add `GetBoneWorldTransform` helper, fix `Mesh::GetBounds`
3. SliceEffect: fix Clean-Slice double-trigger + correct RNG gate
4. Apply ASM-verified markers: ActorManager (12 sites), Mesh::GetBounds
   (after fix), MortarSound, GameSound

**Tier 2 — Targeted RE pass needed:**
5. Audio: master-mute byte (DAT_0018ca54), mute threshold tweak
6. Slicing: implement `m_bCriticalEligible` field + WaveManager critical
   RNG (requires re-analyst @ 0x00178100)

**Tier 3 — Major rework, multiple spec gaps:**
7. SplatEntity: 22 findings, 6 spec gaps. Targeted re-analyst pass to
   resolve the missing DATs and GOT slots before any code change.
8. Font: full rewrite of layout + Load + DrawString. 5 spec gaps. Should
   be a multi-step plan, not a one-shot pass.

## Evidence

ASM scratch files in `tmp/asm-compare/`:
- `slice_spin_test.{cpp,s}`, `actormgr_add_test.{cpp,s}`,
  `entity_layout_test.{cpp,s}`, `hudcontrol_layout_test.{cpp,s}`,
  `scrolling_layout_test.{cpp,s}`
- Per-method `*_binary.s` extracts from prior passes
