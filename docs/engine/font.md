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
| +0x50 | 80 | *(none — never loaded)* | — | n/a | *(none found)* |
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
- `BakedString::Bake` — exists in the binary (`0x00198e44` region) but is a separate text-caching feature not yet needed by any ported screen.

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

## Font_DrawString Implementation

<!-- Analysed: 2026-04-25T22:15 -->

Binary: `0x00198e44` (Font_DrawString). 719 decompiled lines.

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
selects the 0.5 factor — so `0xe` means **right-align, shift down by half line**.

### GL State Changes — NONE

`Font_DrawString` makes **no direct GL calls**. All rendering goes through the
engine's MatrixManager/Mesh pipeline:

1. `MatrixStack::Push` — saves current world matrix.
2. `MatrixStack::Scale(Vec3(scale, scale, 1.0))` — applies em-size scale.
3. `MatrixStack::RotZ(rotZ)` — rotates (0 for upright).
4. `MatrixStack::TranslateLocal(...)` — vertical alignment offset.
5. `MatrixStack::Translate(&world, pos)` — places text at world anchor.
6. `MatrixManager::UploadCurrentMatrices(true)` — uploads MVP to the shader.
7. Per page: `Texture::Set(page_tex)` + `Mesh::DrawTriStrip(verts, count*6, false, null)`.
8. `MatrixStack::Pop` — restores saved matrix.

The function does **not** call `glEnable(GL_BLEND)`, `glDisable(GL_DEPTH_TEST)`,
`glOrtho`, or any raw GL state. The blend/depth state is whatever the
surrounding HUD/world rendering has already set. Glyph quads are added to
per-page vertex arrays (one `QUADCUSTOMVERTEX` tristrip per glyph) and
submitted all at once via `Mesh::DrawTriStrip`.

### Vertex Geometry (unit space, matrix-scaled)

Glyphs are built in normalized coordinates (`glyph_pixel / fontScaleWH`), then
the `scale` matrix scales them to world size. The quad corners are:
```
vertex[0].xy = (cursor_x - w*0.5,  cursor_y - h*0.5)   // top-left in centered coords
vertex[1].xy = (cursor_x - w*0.5,  cursor_y + h*0.5)   // bottom-left
vertex[2].xy = (cursor_x + w*0.5,  cursor_y - h*0.5)   // top-right
vertex[3].xy = (cursor_x + w*0.5,  cursor_y + h*0.5)   // bottom-right
// vertices 4,5 duplicate vertex 3,1 for tristrip degenerate join
```
All vertices get `local_17c` as colour and `DAT_00199a94 = 0.0f` for z.

### Scale Semantic

`scale` is the **target em size in world units** (e.g. 20.0 or 25.0). It is
passed directly as the MatrixStack scale factor. The glyph vertices are stored
in pre-normalized pixel coordinates divided by `font->scaleW/H`, so multiplying
by `scale` brings them to world size. **There is no division by `m_LineHeight`.**
The ratio `scale / m_LineHeight` is NOT used anywhere in `Font_DrawString`.

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

## Port Drift — Root Causes for Invisible Text

<!-- Analysed: 2026-04-25T22:15 -->

### Cause 1 (CRITICAL): Wrong rendering pipeline

**Binary**: `Font_DrawString` renders via `MatrixManager::UploadCurrentMatrices` +
`Mesh::DrawTriStrip`. No direct GL state changes.

**Port** (`Font.cpp:DrawString`, lines 307-356): Makes raw GL calls — `glMatrixMode`,
`glLoadMatrixf`, `glActiveTexture`, `glDrawArrays`, etc. — bypassing the engine's
MatrixManager/Mesh pipeline entirely.

**Effect**: The glyph vertex data is built correctly, but the draw call never
goes through the same shader setup that `Mesh::DrawTriStrip` uses. Depending on
what pipeline state `ShopListItem::Draw` leaves after the divider quad draws,
the raw GL calls may use the wrong shader program, wrong attribute bindings, or
have no valid MVP. This is the most likely reason for invisible text.

**Fix**: Replace the raw GL draw block in `Font::DrawString` (lines 299-356 of
`Font.cpp`) with a call to `Mesh::DrawTriStrip` after `MatrixManager::UploadCurrentMatrices`.
The font function must push/scale/translate the MatrixStack the same way the
binary does, then submit via `Mesh::DrawTriStrip`.

### Cause 2 (CRITICAL): Wrong scale semantic

**Binary**: `scale` passed to `Font_DrawString` is the raw em pixel size (20 or
25). It is applied as `MatrixStack::Scale(scale, scale, 1.0)`. Glyph vertices
are in normalized atlas-pixel units (divided by `scaleW/H`).

**Port** (`Font.h:DrawStringSized`, line 69):
```cpp
float mul = (m_LineHeight > 0) ? (targetSize / (float)m_LineHeight) : targetSize;
DrawString(mul, ...);
```
Then `Font.cpp:DrawString` uses `finalScale = scale * m_Scale` as a per-vertex
multiplier building geometry directly in world units.

**Effect**: The port builds geometry at a different size AND in the wrong
coordinate space. For `font_fruit_ninja.fnt` (lineHeight typically 32-40px),
`DrawStringSized(25)` produces `mul = 25/36 ≈ 0.69`, then glyph quads are built
with vertices at `glyph_pixel * 0.69` world units — roughly 1/36th of the
correct size. The text IS rendered somewhere, but microscopic.

**Fix**: The port's `DrawStringSized` must pass `targetSize` directly (not
divided by lineHeight) to `DrawString`, since `DrawString` itself applies the
scale to the MatrixStack (via `MatrixManager`), not to the vertex positions.
Alternatively: rebuild `DrawString` as the binary does — store normalized glyph
vertices (atlas pixel / scaleWH) and apply scale via MatrixStack.

### Cause 3 (MODERATE): Glyph vertex layout is centered, not top-left

**Binary** builds quads as centered rectangles:
```c
vertex.x = cursor_x +/- glyph_w * 0.5;
vertex.y = cursor_y +/- glyph_h * 0.5;
```

**Port** (`Font.cpp` lines 264-285) builds quads with top-left origin:
```cpp
float gx = cursorX + g.xoffset * finalScale;
float gy = cursorY - g.yoffset * finalScale;
v[0] = { gx, gy, ... };         // top-left
v[2] = { gx, gy - gh, ... };    // bottom-left
```

**Effect**: Text renders at a Y offset of ±half-glyph-height from the expected
position. The sign of gy also differs from what `MatrixStack::TranslateLocal`
would apply for vertical alignment. Text may render off-screen or in the wrong
row.

**Fix**: After fixing Cause 1+2, update the vertex construction to match the
centered layout, or verify the coordinate sign matches the engine's Y axis.

---

## See Also

- `docs/engine/formats/fonts.md` — BMFont .fnt file format
- `docs/engine/rendering-detail.md` — Font::Load, DrawString, QUADCUSTOMVERTEX
- `docs/structs/game.md` — g_GameData full layout (+0x50..+0x80 region)
