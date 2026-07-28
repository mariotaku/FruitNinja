# Font Slots in g_GameData

<!-- Analysed: 2026-04-25T22:15 -->

## Overview

`g_GameData` (the flat 0x608-byte gameplay struct) holds 11 `Font*` slots at
offsets +0x50 through +0x80.  All populated slots are loaded in
`GameInitialise @ 0x0010bdfc`.  Three slots are reserved (zeroed but never
loaded by any Font::Load call in this binary version).  `GameDestroy @
0x0010b7ec` deletes every non-null slot.

`MainScreen` also owns a private `Font*` (loaded from `verdana.fnt`) stored as
a member of the `MainScreen` object (`m_pFont`), **not** in g_GameData.

---

## Font::Load Signature

```
// Binary: 0x00199e9c (270 lines)
// Thunk:  0x000fb960
int __thiscall Mortar::Font::Load(Font* this, const char* path);
// Returns 1 on success, 0 on failure.
// Self-contained: allocates glyphs, reads page textures via TextureManager.
// No separate Font::Init or Font::SetScale method exists in the binary.
```

Font object size: **0x438 bytes** (`operator_new(0x438)` before each call).

---

## HD vs SD Selection

`GameInitialise` calls `DisplayManager::ShouldUseHDFonts()` once and stores
the result in `useHDFonts` (a local int).  Slots +0x54 and +0x58 use a branch
on this flag to pick between HD and SD paths.  All other slots use a single
path regardless of HD flag.

---

## Per-Slot Table

| g_GameData offset | Decimal | SD filename | HD filename | Loader call site | Primary readers |
|-------------------|---------|-------------|-------------|------------------|-----------------|
| +0x50 | 80 | *(never loaded)* | — | n/a | *(none found)* |
| +0x54 | 84 | `fonts/font_fruit_ninja.fnt` | `fonts/font_fruit_ninja_HD.fnt` | `0x0010bf3a` | AboutScreen::Draw, ScoreControl, NotificationControl, FruitFactControl, ShopListItem, PowerUpShop, ScrollingMenuItem, LeaderboardItem, FriendLeaderboardItem, many others |
| +0x58 | 88 | `fonts/fruit_ninja_numbers.fnt` | `fonts/fruit_ninja_numbers_HD.fnt` | `0x0010bf6e` (guarded: only if +0x58 == 0) | ScoreControl, TimeControl, GameOverScreen, ScoreMultiplyerBoard, ProgressionTimerControl |
| +0x5C | 92 | *(none — never loaded)* | — | n/a | CoinCounter::Draw |
| +0x60 | 96 | *(none — gap)* | — | n/a | *(none found)* |
| +0x64 | 100 | *(none — never loaded)* | — | n/a | *(none found)* |
| +0x68 | 104 | `fonts/fruit_ninja_numbers_green.fnt` | — | `0x0010c082` (guarded: only if +0x68 == 0) | unknown |
| +0x6C | 108 | `fonts/arcade_results_numbers.fnt` | — | `0x0010bfa4` (guarded: only if +0x6C == 0) | FriendLeaderboardItem::Draw (alternate path) |
| +0x70 | 112 | `fonts/gold_numbers.fnt` | — | `0x0010bfcc` (File::Exists guard) | unknown |
| +0x74 | 116 | `fonts/silver_numbers.fnt` | — | `0x0010bff0` (File::Exists guard) | unknown |
| +0x78 | 120 | `fonts/bronze_numbers.fnt` | — | `0x0010c014` (File::Exists guard) | unknown |
| +0x7C | 124 | *(alias of +0x6C)* | — | `*(+0x7c) = *(+0x6c)` | unknown |
| +0x80 | 128 | `fonts/fruit_ninja_numbers_blue2.fnt` | — | `0x0010c038` (guarded: only if +0x80 == 0) | ScoreControl (multiplier), ScoreMultiplyerBoard, BonusScreen |

### Notes on loading guards

Slots +0x58, +0x68, +0x6C, +0x80 are loaded only when they are null
at the time GameInitialise runs:
```c
if (*(int*)(g_GameData + 0x58) == 0) {
    pFVar8 = operator_new(0x438);
    Font::Font(pFVar8);
    *(Font**)(g_GameData + 0x58) = pFVar8;
    Font::Load(pFVar8, path_sd_or_hd);
}
```
This guard allows the slots to survive an internal soft-restart (if ever
triggered) without leaking the old Font object.

### Optional medal font slots (+0x70..+0x78)

These are loaded only if `File::Exists(path)` returns true.  The .fnt files
`gold_numbers.fnt`, `silver_numbers.fnt`, `bronze_numbers.fnt` are **not
present** in the `FruitNinjaBada/Data/fonts/` directory shipped with this
binary — so these slots always stay null at runtime.

### Alias slot +0x7C

After loading +0x6C:
```c
uVar14 = *(undefined4*)(g_GameData + 0x6c);
*(undefined4*)(g_GameData + 0x7c) = uVar14;
*(undefined4*)(g_GameData + 0x70) = uVar14;   // overwritten again if gold_numbers.fnt loads
*(undefined4*)(g_GameData + 0x74) = uVar14;   // overwritten again if silver_numbers.fnt loads
*(undefined4*)(g_GameData + 0x78) = uVar14;   // overwritten again if bronze_numbers.fnt loads
```
So +0x70, +0x74, +0x78, +0x7c are all set to the arcade_results_numbers font
as a fallback, then selectively overwritten by the optional medal fonts.

GameDestroy handles +0x70..+0x7c via a loop that skips deletion if the slot
equals +0x6c (i.e. it is a non-owning alias):
```c
// Loop body (iterates +0x70, +0x74, +0x78, +0x7c):
if (slot_ptr == *(Font**)(g_GameData + 0x6c)) {
    *slot = null;      // alias — just clear, don't delete
} else if (slot_ptr != null) {
    Font::~Font(slot_ptr);
    operator_delete(slot_ptr);
    *slot = null;
}
```

### Reserved / unused slots

| Offset | Status | Evidence |
|--------|--------|----------|
| +0x50 | Reserved — always null | Zeroed in GameInitialise; deleted (with null guard) in GameDestroy; no Font::Load targets it anywhere in binary |
| +0x5C | Reserved — always null | Same pattern; CoinCounter::Draw presumably reads it but the pointer is always null in this binary |
| +0x60 | Gap — not a Font slot | Not in GameDestroy's deletion sequence; not zeroed in GameInitialise (the nearby zero block zeroes +0x50..+0x80 as a bulk memset, but +0x60 is not explicitly deleted by name) |
| +0x64 | Reserved — always null | Zeroed; deleted as `*(g_GameData + 100)`; no loader found |

---

## Port Method Requirements

The port's existing `Font::Load` (`src/engine/render/Font.cpp`) matches the
binary's `0x00199e9c` signature.  **No additional Font methods need porting**
for the font-slot system:

- `Font::Init` — does not exist as a separate function in the binary; `Font::Font` (ctor) + `Font::Load` is the complete construction sequence.
- `Font::SetScale` — not present in binary.
- Baked (cached) text meshes — a separate feature; see "Baked-string classes" below.

---

## Baked-string classes

There is **no bitmap `Mortar::BakedString` class in v1.6.1**. The only
baked-string classes are `BakedStringTTF`, `BakedStringBox` and
`FancyBakedString` — all TTF-backed, all built on `FontCacheObjectTTF`, none of
them consumers of the `.fnt` bitmap `Font` documented above. (`MenuButton::SetText`,
historically cited as the bitmap-BakedString consumer, is v1.6.1
`MenuButton::SetText @0x0019b0ac` and builds `BakedStringTTF` objects only.)

Per the code-is-canonical policy their layouts, vtables and call contracts live
in the port headers, not here:

- `src/engine/render/BakedStringTTF.h`
- `src/engine/render/BakedStringBox.h`
- `src/engine/render/FancyBakedString.h`

---

## Font Asset Cross-Reference

All font filenames used in the binary:

| String address | Filename | g_GameData slot |
|----------------|----------|-----------------|
| 0x001b97f1 | `fonts/font_fruit_ninja_HD.fnt` | +0x54 (HD path) |
| 0x001b980f | `fonts/font_fruit_ninja.fnt` | +0x54 (SD path) |
| 0x001b982a | `fonts/fruit_ninja_numbers_HD.fnt` | +0x58 (HD path) |
| 0x001b984b | `fonts/fruit_ninja_numbers.fnt` | +0x58 (SD path) |
| 0x001b9869 | `fonts/arcade_results_numbers.fnt` | +0x6C |
| 0x001b988a | `fonts/gold_numbers.fnt` | +0x70 (optional) |
| 0x001b98a1 | `fonts/silver_numbers.fnt` | +0x74 (optional) |
| 0x001b98ba | `fonts/bronze_numbers.fnt` | +0x78 (optional) |
| 0x001b98d3 | `fonts/fruit_ninja_numbers_blue2.fnt` | +0x80 |
| 0x001b98f7 | `fonts/fruit_ninja_numbers_green.fnt` | +0x68 |
| 0x001bbcb3 | `fonts/verdana.fnt` | MainScreen::m_pFont (NOT g_GameData) |

Files in `FruitNinjaBada/Data/fonts/` that are **NOT referenced** by any
string in the binary: `electrofied_medium.fnt`, `fruit_ninja_numbers_blue.fnt`,
`fruit_ninja_numbers_red.fnt`, `fruit_ninja_numbers_silver.fnt`.

---

---

## Font::DrawString Implementation

Binary: **v1.6.1 `Mortar::Font::DrawString` @ `0x0024c7f0`** (the 10-param
`_Vector3` overload) — this is the owner of the scale / rotate / align / position
math described below.

> Address hygiene: addresses in this section that carry no `v1.6.1` prefix are
> **unverified v1.5.1-era** and must be re-checked against v1.6.1 before use
> (see CLAUDE.md "Source-side comment grammar"). The signature and per-glyph
> vertex maths below are believed still accurate; only the addresses are suspect.

### True Signature

```c
// ARM thiscall: floats in s-regs, integers/pointers in r-regs.
// Ghidra bundles several stack args into param_8[16]; the real layout is:
void Font_DrawString(
    float     scale,        // param_1 — world-space em size (e.g. 20.0, 25.0)
    float     maxWidth,     // param_2 — 1.0 when unused (see note)
    float     rotZ,         // param_3 — rotation in radians (0.0 for upright text)
    Font*     this,         // r0
    Utf8StringIterator* iter, // r1
    _Vector3<float>* pos,   // r2 — world-space anchor position
    Colour*   colour,       // r3
    _Vector2<float> maxWH,  // stack: 8 bytes, two floats (from CopyGlobalVec2)
    int       alignment,    // stack: horizontal | vertical flags (see below)
    float     z,            // stack: z depth component (0.0 in all ShopListItem calls)
    MortarRectangleDec* clipRect  // stack: null = no clipping
);
```

The 13-param overload at `0x00199aa0` (`DrawString`) is a thin wrapper that
packs a `_Vector3` from three separate x/y/z floats and delegates to
`Font_DrawString` with `maxWidth=1.0`.

### Alignment Flags (param `alignment`)

The `alignment` int is split as:
- bits 0-1: horizontal — `0`=left, `1`=centre/word-wrap, `2`=right
- bits 2-3: vertical — `0`=top, `0x4`=middle (half-height shift), `0x8`=bottom (full-height shift), `0xC`=both (bottom)
- bit 4: `0x10` — enable word-wrap width limit from `maxWH.x`

`0xe` = `0b1110` = right (0x2) + middle (0x4) + bottom (0x8). In the binary's
check logic: `flags & 0xc != 0` triggers vertical adjustment; `flags & 4`
selects half vs full shift. `0xe & 0xc = 0xc` fires vertical adjust; `0xe & 4 = 4`
selects the 0.5 factor — so `0xe` means **right-align, shift UP by half line-height**.

IMPORTANT: The binary shifts text UPWARD via `MatrixStack::TranslateLocal(0, +factor, 0)` before scaling.

### Transform order — v1.6.1 `Mortar::Font::DrawString` @ `0x0024c7f0`

The scale / rotate / align / position math is owned by the 10-param `_Vector3`
overload `Mortar::Font::DrawString @0x0024c7f0`. Order is taken from the flush
block `0x0024d610`–`0x0024d694` and the align branch `0x0024d2f4`–`0x0024d37c`.
**Order matters** — each step composes onto the result of the previous one:

1. `world.m_Current = _Matrix44<float>::Identity` (inlined Reset), then bump
   `m_Version`. The world matrix is *replaced*, not pushed onto.
2. `MatrixStack::Scale @0x0015d100` → `_Matrix44<float>::Scale44 @0x0015d06c`
   with `(scale, scale, 1.0f)`. This is a LEFT / row multiply (`S*M`), but it is
   applied to Identity, so the side is inert here.
3. `MatrixStack::RotZ @0x00105338`.
4. **Vertical** alignment only, gated on `tst r2, #0xc`:
   ```c
   alignY = (-height - cursorY) * ((align & 4) ? 0.5f : 1.0f);
   MatrixStack::TranslateLocal @0x0024a150  ->  LocalTranslate44(0, alignY, 0);
   ```
   Because it lands **after** scale and rotate, the alignment offset is itself
   scaled and rotated.
5. `MatrixStack::Translate @0x00107d84` → `GlobalTranslate44(pos)` — world anchor.
6. `_UploadCurrentMatrices(1)`, then per-page `DrawTriStrip`.

**Horizontal alignment is not a matrix operation at all.** It is folded into the
scalar x-cursor offset baked into each glyph vertex (`LAB_0024c998`, recomputed
per line at `0x0024ca3c`). Only vertical alignment touches the matrix. Getting
this wrong is the standard failure mode when re-deriving this function.

### GL State Changes — NONE

`Font::DrawString` makes **no direct GL calls** — no `glEnable(GL_BLEND)`, no
`glDisable(GL_DEPTH_TEST)`, no `glOrtho`. Blend/depth state is whatever the
surrounding HUD/world rendering already set. Glyph quads accumulate into
per-page vertex arrays (one `QUADCUSTOMVERTEX` tristrip per glyph) and are
submitted all at once via `Mesh::DrawTriStrip`, with
`Texture::Set(page_tex)` around each page.

### CharTemplate Binary Layout (ARM-confirmed, 0x24 bytes each)

`Font::Load` calls `Parse_Char` to get raw pixel values, then normalizes them
in-place before storing. The CharTemplate array is at `*(void**)font` (byte
offset 0 in the Font object), with `count * 0x24` bytes total. Ghidra confirmed
at `0x0019a128`–`0x0019a180`.

| Byte offset | Type | Content after normalization |
|-------------|------|-----------------------------|
| +0x00 | short (2 bytes) | char id (raw, used as lookup key; also in lookup table at `font[id*4+4]`) |
| +0x02 | short (2 bytes) | padding/unused |
| +0x04 | float | `x / scaleW` — UV left edge (normalized [0,1]) |
| +0x08 | float | `y / scaleH` — UV top edge (normalized [0,1]) |
| +0x0c | float | `width / lineHeight` — glyph width in lineHeight units |
| +0x10 | float | `height / lineHeight` — glyph height in lineHeight units |
| +0x14 | float | `xoffset / lineHeight` — x bearing in lineHeight units |
| +0x18 | float | `yoffset / lineHeight` — y bearing in lineHeight units |
| +0x1c | float | `xadvance / lineHeight` — cursor advance in lineHeight units |
| +0x20 | byte  | page index |
| +0x21–+0x23 | — | padding |

**UV coordinates** (x, y at +0x04/+0x08) are divided by `scaleW/scaleH` so they
are already in [0,1] atlas space.

**Metric coordinates** (width, height, offsets, advance at +0x0c..+0x1c) are
divided by **`lineHeight`** (the font's `lineHeight=` value from the `common`
line, stored as float at `font+0x424`). They are NOT divided by scaleW/H.

Normalization code in `Font::Load` (ARM at `0x0019a128`):
```c
// iVar6 = *(int*)(font + 0x41c) = scaleW
// iVar7 = *(int*)(font + 0x420) = scaleH
// *(float*)(font + 0x424) = lineHeight (float)
glyph[+0x08] = raw_y      / (float)scaleH;
glyph[+0x04] = raw_x      / (float)scaleW;
glyph[+0x0c] = raw_width  / lineHeight;
glyph[+0x10] = raw_height / lineHeight;
glyph[+0x1c] = raw_xadv   / lineHeight;
glyph[+0x14] = raw_xoff   / lineHeight;
glyph[+0x18] = raw_yoff   / lineHeight;
```
After parsing all glyphs the final normalization:
```c
*(float*)(font + 0x428) /= *(float*)(font + 0x424);  // base_float /= lineHeight
```
So `font+0x428` stores `base / lineHeight` (normalized ascent fraction).

### Font Object Field Map (ARM-confirmed from Font::Load)

| Font byte offset | C type | Name | Source |
|-----------------|--------|------|--------|
| +0x000 | void* | char_template_array | `operator_new(count * 0x24)` |
| +0x404 | int | char_count | `chars count=N` |
| +0x408 | Page* | pages | `operator_new((N+1)*8)` |
| +0x40c | int | page_count | `pages=N` |
| +0x410 | Kerning* | kernings | `operator_new(N*0xc)` |
| +0x414 | int | kerning_count | `kernings count=N` |
| +0x41c | int | scaleW | `common ... scaleW=N` |
| +0x420 | int | scaleH | `common ... scaleH=N` |
| +0x424 | float | lineHeight | `common lineHeight=N` (stored as float) |
| +0x428 | float | base_norm | `base / lineHeight` (after all-glyph normalization) |
| +0x42c | vector<vector<QUADCUSTOMVERTEX>> | page_verts | per-page glyph vertex buffers |

### Vertex Geometry (ARM-grounded, lineHeight-normalized space)

`Font_DrawString` reads pre-normalized float fields from CharTemplate. The ARM
at `0x0019919e`–`0x0019924a` confirms:
```
s28 = [r4+0x18] = glyph.yoffset_norm   (= yoffset/lineHeight)
s16 = [r4+0x04] = glyph.u0             (= x/scaleW, UV left)
s13 = [r7+0x424] = font.lineHeight_f
s12 = vcvt((int)[r7+0x41c]) = (float)scaleW
s11 = vcvt((int)[r7+0x420]) = (float)scaleH
s18 = [r4+0x08] = glyph.v0             (= y/scaleH, UV top)
s14 = [r4+0x0c] = glyph.w_norm         (= width/lineHeight)
s15 = [r4+0x10] = glyph.h_norm         (= height/lineHeight)
s27 = [r4+0x14] = glyph.xoff_norm      (= xoffset/lineHeight)

-- UV right/bottom computed as:
u1 = u0 + w_norm * (lineHeight / scaleW)   = (x + width)  / scaleW
v1 = v0 + h_norm * (lineHeight / scaleH)   = (y + height) / scaleH

-- Center position of quad (in lineHeight-normalized space):
cx = cursor_x + xoff_norm + w_norm * 0.5
cy = cursor_y - yoff_norm - h_norm * 0.5  (note: Y decreases downward in atlas)
hw = w_norm * 0.5
hh = h_norm * 0.5

-- Four quad corners (tristrip order 0,1,2,3 with degenerate join):
vertex[0] = (cx - hw, cy - hh)  // top-left
vertex[1] = (cx - hw, cy + hh)  // bottom-left
vertex[2] = (cx + hw, cy - hh)  // top-right
vertex[3] = (cx + hw, cy + hh)  // bottom-right
vertex[4] = copy of vertex[3]   // tristrip degenerate
vertex[5] = copy of vertex[1]   // tristrip degenerate
```
All 6 vertices get `local_17c` as packed colour and `DAT_00199a94 = 0.0f` for z.

Cursor advance (ARM at `0x00199848`–`0x00199876`):
```c
cursor_x += xadvance_norm + kerning + spacing * (glyph_id == 0x20 ? 3.0 : 1.0);
// spacing = local_64[0] (pre-computed from maxWH)
```

### Scale Semantic (ARM-confirmed)

`scale` (param_1) is the **target em size in world units** (e.g. 20.0 or 25.0).
It is applied as `MatrixStack::Scale(Vec3(scale, scale, 1.0))` (ARM at
`0x00199900`–`0x0019990e`). The glyph vertices are stored in **lineHeight-
normalized units** (glyph_pixel / lineHeight), so after the matrix scale:

```
world_width  = (glyph_width_px  / lineHeight) * scale
world_height = (glyph_height_px / lineHeight) * scale
world_advance= (xadvance_px     / lineHeight) * scale
```

For `font_fruit_ninja.fnt`: `lineHeight=28`, `scaleW=256`, `scaleH=256`.
Worked example — glyph `width=16`, `scale=25`, `lineHeight=28`:
```
stored_w = 16 / 28 = 0.5714
world_w  = 0.5714 * 25 = 14.3 world units  (correct)

WRONG (port's invW=1/scaleW formula):
stored_w_wrong = 16 / 256 = 0.0625
world_w_wrong  = 0.0625 * 25 = 1.56 world units  (18x too small)
```

**There is NO division by `m_LineHeight` in `Font_DrawString` itself.** The
division happens once in `Font::Load` when storing the CharTemplate, not at
draw time. The ratio `scale / lineHeight` is NOT used anywhere in
`Font_DrawString`.

The MatrixStack::Scale call (ARM confirmed at `0x00199900`):
```arm
add r0, r9, #0x1094+0x14    ; MatrixStack* this
vldr.32 s0, [pc, #0x180]    ; s0 = 0.0f
vmov.f32 s1, s22            ; s1 = scale (param_1)
vmov.f32 s2, s0             ; s2 = 0.0f
blx MatrixStack::Scale       ; Scale(Vec3(scale, scale, 1.0)) -- s22 was param_1
```
The argument is `param_1` (scale) verbatim, no modification.

### maxWH (_Vector2<float>) Semantics

`CopyGlobalVec2_GameTask` copies two floats from a global Vec2 in the GameTask
static block. Inside `Font_DrawString`:
```c
*(float*)(param_8._0_4_) /= param_1;               // modifies maxWH.x /= scale
*(float*)(param_8._0_4_ + 4) /= (param_2 * scale); // modifies maxWH.y /= (maxWidth * scale)
```
These modify the caller's stack copy, not a persistent value. When `maxWidth`
argument is `1.0`, `maxWH.y /= scale`. The resulting `maxWH.x` is the word-wrap
column limit in glyph units (only active when `flags & 0x10`).

---

## DrawString (0x00199aa0) — Thin Wrapper

```c
// Packs three floats into a _Vector3, then calls Font_DrawString.
// maxWidth = 1.0 always. rotZ = param_7 (third stack arg after colour).
void DrawString(Font* this, Utf8StringIterator iter,
                float x, float y, float z,          // packed into Vec3 pos
                Colour colour,
                float maxWH_x, float maxWH_y,        // Vec2 maxWH
                float rotZ,
                int alignment,
                MortarRectangleDec* clipRect, float extra);
```

Delegates to `Font_DrawString(this, iter, &pos, &colour, Vec2(maxWH_x,maxWH_y),
alignment, rotZ, clipRect)` with `maxWidth=1.0`.

---

## ShopListItem::Draw Text-Call Breakdown

<!-- Analysed: 2026-04-25T22:15 -->

Binary: `0x0015eb00`. All five `Font_DrawString` calls use scale cast from
integer pixel size. `DAT_0015eec4 = 0.0f` (z/rotation). `DAT_0015eea8 = 0x432f0000 = 175.0f` (fit threshold).
`DAT_0015f524 = 0x42a50000 = 82.5f` (max desc height). `DAT_0015f540 = 0x43200000 = 160.0f` (desc wrap width).

### Call 1 — Title shadow

| Param | Binary value | Meaning |
|-------|-------------|---------|
| scale | `(float)(int)uVar15` | 20 (HD) or 25 (SD) — after scale-to-fit shrink |
| maxWidth | `1.0f` | unused |
| rotZ | `0.0f` | upright |
| pos | `basePos + Vec3(4, -4, 0)` | shadow offset |
| colour | `Colour(0,0,0,0x40)` | black 25% alpha |
| alignment | `0xe` | right + vertical half |
| z | `0.0f` | |
| clipRect | `null` | |

### Call 2 — Title fill

Same as Call 1 but `pos = basePos + Vec3(0,0,0)` (zero offset — Vec3 of all
`DAT_0015eec4=0.0`) and `colour = itemColour` (white or grey).

### Call 3 — Cost shadow

| Param | Binary value | Meaning |
|-------|-------------|---------|
| scale | `pFVar29` = `16.0 * fVar26` (HD) or `0x41a00000 = 20.0f` (SD) | |
| pos | `basePos + Vec3(4,-4,0)` after `basePos.y -= 26.0` | shifted down 26 |
| colour | `Colour(0,0,0,0x40)` | shadow |
| alignment | `0xe` | |

Cost string comes from `*(char**)(static_block + 0x1c + type*4)` — a pre-cached
array of 4 cost strings indexed by `ItemInfo->m_Type` (byte at +0x10).

### Call 4 — Cost fill

Same as Call 3 but `colour = itemColour`, `pos = basePos + Vec3(0,0,0)`.

### Call 5 — Description text

Uses `Font::DrawString` (0x00199aa0) overload, not `Font_DrawString` directly.

| Param | Binary value | Meaning |
|-------|-------------|---------|
| scale | `fVar28` (starts 18.0, shrunk by -0.25 until height fits 82.5f) | |
| x | `ShopScreen::GetDescriptionTextXPos()` | slide-in x position |
| y | `basePos.y` | row y |
| z | `0.0f` | |
| colour | locked: `(255,255,255,alpha)`, unlocked: `(0x74,0x5D,0x3B,alpha)` | |
| alignment | `0xf` (right+bottom+middle — all set) | |
| maxWH | `(DAT_0015f540=160.0f, DAT_0015f53c=0.0f)` | wrap at 160 units wide |
| height-shrink threshold | `DAT_0015f524 = 82.5f` | |

The font used for description (Call 5) is `*(Font**)(iVar21 + DAT_0015f534 + 0x54)` —
a SECOND GameData pointer, retrieved from a DIFFERENT GOT offset than the title
font. This may be `g_GameData+0x54` (same font_fruit_ninja slot) but the code
path goes via a separate GOT indirection (`DAT_0015f534` vs `DAT_0015eeb4`).

---

## Port Drift — Root Causes for White Stripes / Invisible Text

<!-- Analysed: 2026-04-25T23:50 -->

### Cause 1 (RESOLVED in current port): Wrong rendering pipeline

**Binary**: `Font_DrawString` renders via `MatrixManager::UploadCurrentMatrices` +
`Mesh::DrawTriStrip`. No direct GL state changes.

**Current port** (`Font.cpp` lines 335–370): Uses `MatrixStack::Push/Scale/Translate`,
calls `MatrixManager::UploadCurrentMatrices`, then `Renderer::DrawTriStrip`.
This now matches the binary's pipeline.

### Cause 2 (CRITICAL — still present): Wrong glyph metric divisor

**Binary (ARM-confirmed)**: `Font::Load` normalizes ALL glyph metrics (width,
height, xoffset, yoffset, xadvance) by dividing by `lineHeight` (the `lineHeight`
value from the BMFont `common` line, e.g. 28 for `font_fruit_ninja.fnt`). Only
UV coordinates (x, y) are divided by scaleW/scaleH. The CharTemplate stores:
```
glyph.width  = raw_width_px  / lineHeight   (stored float)
glyph.height = raw_height_px / lineHeight   (stored float)
glyph.xoff   = raw_xoff_px   / lineHeight   (stored float)
glyph.yoff   = raw_yoff_px   / lineHeight   (stored float)
glyph.xadv   = raw_xadv_px   / lineHeight   (stored float)
glyph.u0     = raw_x_px      / scaleW       (stored float, UV only)
glyph.v0     = raw_y_px      / scaleH       (stored float, UV only)
```
`Font_DrawString` reads these pre-normalized floats directly and uses them as
vertex positions without any additional division. `MatrixStack::Scale(scale,scale,1)`
then multiplies them to world size.

**Port** (`Font.h` `FontGlyph` struct): Stores raw integer pixel values
(width, height, etc. are `int`). `Font::DrawString` (`Font.cpp` lines 205–310)
applies `invW = 1.0f/m_ScaleW` and `invH = 1.0f/m_ScaleH` as the divisor when
computing vertex positions — dividing by **scaleW/H instead of lineHeight**.

**Effect — the quantitative failure**:
For `font_fruit_ninja.fnt`: `lineHeight=28`, `scaleW=256`.
Glyph `width=16`, called with `scale=25`:
```
Binary:  stored_w = 16/28 = 0.5714,  world_w = 0.5714 * 25 = 14.3 units (correct)
Port:    stored_w = 16/256 = 0.0625, world_w = 0.0625 * 25 = 1.56 units (9x too small)
```
At 1.56 world units wide against a 480-unit screen, each glyph is ~0.3% of screen
width — rendering as horizontal white stripes or invisible pixels at any font size.

**Fix** (two-part):

Part A — Store normalized floats in `FontGlyph`, not raw ints. Change `Font::Load`
to apply the same normalization the binary does:
```cpp
// In Font::Load, after parsing each char:
float invLH = (m_LineHeight > 0) ? (1.0f / (float)m_LineHeight) : 1.0f;
float invW  = (m_ScaleW > 0)     ? (1.0f / (float)m_ScaleW)     : 1.0f;
float invH  = (m_ScaleH > 0)     ? (1.0f / (float)m_ScaleH)     : 1.0f;
g.xf      = (float)g.x       * invW;   // UV left (normalized by scaleW)
g.yf      = (float)g.y       * invH;   // UV top  (normalized by scaleH)
g.wf      = (float)g.width   * invLH;  // width in lineHeight units
g.hf      = (float)g.height  * invLH;  // height in lineHeight units
g.xofff   = (float)g.xoffset * invLH;  // xoffset in lineHeight units
g.yofff   = (float)g.yoffset * invLH;  // yoffset in lineHeight units
g.xadvf   = (float)g.xadvance* invLH;  // advance in lineHeight units
```

Part B — In `Font::DrawString`, use the pre-normalized float fields for vertex
positions (not `g.width * invW`), and use the pre-normalized UV floats (not
`g.x * invW`). Remove `invW`/`invH` from the vertex position math entirely.

Alternatively: keep raw-int storage but change the divisor in `DrawString` from
`invW/invH` to `invLH = 1.0f/m_LineHeight` for vertex positions:
```cpp
// WRONG (current port):
const float cx = cursorX + ((float)g.xoffset + (float)g.width  * 0.5f) * invW;
// CORRECT (matches binary):
const float invLH = 1.0f / (float)m_LineHeight;
const float cx = cursorX + ((float)g.xoffset + (float)g.width  * 0.5f) * invLH;
// UVs still use invW/invH:
const float u0 = (float)g.x * invW;
```

### Cause 3 (RESOLVED in current port): Glyph vertex layout

The current port's vertex layout (`cx ± hw, cy ± hh`) already matches the
binary's centered-rectangle layout. This was fixed in a prior session.

### Summary of active bugs (as of 2026-04-27)

| # | File | Lines | Bug | Fix |
|---|------|-------|-----|-----|
| 1 | `Font.cpp` | 205–310 | Vertex metric divisor is `1/scaleW` instead of `1/lineHeight` | Change divisor to `1.0f/m_LineHeight` for cx/cy/hw/hh/cursor; keep `1/scaleW(H)` for UVs only |
| 2 | `Font.cpp` | 310 | `cursorX += g.xadvance * invW` uses wrong divisor | Change to `g.xadvance * invLH` |
| 3 | `Font.cpp` | 208 | `normLineH = m_LineHeight * invH` used for line-height in world space | Change to `1.0f` (binary stores 1 lineHeight unit = 1.0 in lineHeight-norm space) |
| 4 | `Font.cpp` | 233-238 | Vertical alignment sign wrong: `startY -= factor` (downward) vs binary's `TranslateLocal(+factor)` (upward) | Change to `startY += factor`. Predicts exactly 20-unit error at scale=20 (HD), 25-unit at scale=25 (SD). See "Vertical Alignment Root Cause" section. |

---

## See Also

- `docs/engine/formats/fonts.md` — BMFont .fnt file format
