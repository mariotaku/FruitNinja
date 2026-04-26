<!-- Analysed: 2026-04-26T12:00 -->

# ShopListItem::Draw -- Binary Spec

Binary address: **0x0015eb00** (vtable slot 11, +0x2C from ShopListItem vtable at 0x001ea030).

## 1. Function entry and early guards

```
0x0015eb00  ShopListItem::Draw(void)
  r5 = this (in_r0)
  r4 = GOT base = DAT_0015eeac + 0x15eb14 = 0x001ec730
  static_block = r4 + DAT_0015eeb0 = r4 + 0x000451b4 = 0x002318e4  (in .bss)
```

Order of guards before any drawing:

1. If `*(this + 0x27D) != 0` (m_bSelected): write `0xFFFFFFFF` to
   `static_block[+0x8C]` (cost-type colour cache reset).
2. If `*(this + 0x2D) == 0` (m_bOnscreenItem field at +0x2D in
   ScrollingMenuItem): **return** -- skip everything. NOTE: the port uses
   `+0x27C` (m_bOnscreenItem). The binary tests `+0x2D`, which is the
   `m_bVisible` flag inside the ScrollingMenuItem base class. These are
   DIFFERENT fields. The port's `m_bOnscreenItem` at `+0x27C` is a separate
   extended field that does NOT correspond to the `+0x2D` check. Fix: the
   guard should test ScrollingMenuItem::m_bVisible (+0x2D), not the
   ShopListItem-extended m_bOnscreenItem (+0x27C).

After the guards:

- `pFVar22` = `*(Game + 0x54)` = `Game::GetInstance()->pFontMain.Get()`
- `aCStack_3c` = Colour copied from `*(Colour**)(r4 + 0x73A4)` -- white colour
  singleton (0xFF,0xFF,0xFF,0xFF); this is the draw-colour template.
- `ItemInfo::IsLocked()` called; if locked: `CStack_40 = Colour(200,200,200,255)`,
  overrides the white colour.
- `local_d0` = `Vec3(this->pos)` -- basePos copied from `this + 4`.

## 2. Field offsets referenced in Draw

All offsets are ARM32 absolute from `ShopListItem*`.

| Offset | Type    | Name               | Notes                                    |
|--------|---------|--------------------|------------------------------------------|
| +0x04  | Vec3    | pos                | Set by Move each frame                   |
| +0x0D  | uint8   | m_bVisible         | ScrollingMenuItem inner flag (NOT +0x27C)|
| +0x10  | void*   | m_pParent          | ScrollingMenu* parent                    |
| +0x24  | float   | m_Height           | 80.0f                                    |
| +0x18  | float   | m_Size.x           | 60.0f (used in Move for icon X offset)   |
| +0x2D  | uint8   | m_bVisible/onscreen| Guard check: Draw skips if 0             |
| +0x58  | void*   | m_pShopScreen      | ShopScreen* back-ptr                     |
| +0x5C  | char[]  | m_DescText         | 512-byte inline text buffer              |
| +0x25C | float   | m_NewItemAlpha     | >0 draws new_item_sml badge              |
| +0x260 | float   | m_SelectedAlpha    | >0 draws selected_sml ring               |
| +0x264 | float   | m_LockFlashAlpha   | jitter trigger; 0.25 on locked-tap       |
| +0x268 | Vec3    | _pad2 / iconPos    | Written by Move: (pos.x+95.2, pos.y, 0) |
| +0x274 | SmartPtr| m_pIconTex         | Item icon texture                         |
| +0x278 | ItemInfo*| m_pItemInfo       | Item data pointer                         |
| +0x27C | uint8   | m_bOnscreenItem    | Extended onscreen flag (NOT the guard)   |
| +0x27D | uint8   | m_bSelected        | Resets colour cache when set             |
| +0x27E | uint8   | m_bIsNew           | Triggers loading.tex stripes + divider 2 |
| +0x280 | float   | m_CostAlpha        | Alpha for description/cost text block    |

**PORT DIVERGENCE -- guard field**: The binary guard tests `*(this + 0x2D)`, not
`*(this + 0x27C)`. The port should be using `m_bVisible` from ScrollingMenuItem
(+0x2D), not `m_bOnscreenItem` (+0x27C).

## 3. HD/non-HD font scale

```cpp
bool isHD = (Game::GetInstance() + 3 == '\f');  // byte at Game+3 == 0x0C
uint titleScale = isHD ? 20 : 25;               // integer, used as float
```

## 4. Static block layout (at 0x002318e4, BSS-zeroed)

| Offset  | Type     | Name             | Notes                                          |
|---------|----------|------------------|------------------------------------------------|
| +0x1C   | char*    | costStr[0]       | GETSTRING(0xB7) -- "0 stars" type string       |
| +0x20   | char*    | costStr[1]       | GETSTRING(0xB6)                                |
| +0x24   | char*    | costStr[2]       | GETSTRING(0xB8)                                |
| +0x28   | char*    | costStr[3]       | GETSTRING(0x113)                               |
| +0x3C   | SmartPtr | tex_selected_sml | selected_sml.tex                               |
| +0x40   | SmartPtr | tex_locked_stroke| locked_stroke.tex                              |
| +0x44   | SmartPtr | tex_new_item_sml | new_item_sml badge texture                     |
| +0x6C   | float    | badge_y_offset   | Zero until set; used in Part 3 badge Y         |
| +0x8C   | int      | colour_cache     | Cost-type selector; FFFF=reset; grey if mismatch|
| +0x90   | float[4] | costWidths[0..3] | Cached measured text widths * costScale        |

The static block is in BSS so all fields start as 0 / nullptr. The textures at
+0x3C, +0x40, +0x44 are populated during ShopScreen::LoadContent. The costWidths
cache at +0x90..+0x9C is filled lazily: if `static_block[+0x90] == 0.0f`, all 4
widths are measured and cached.

## 5. Per-element draw calls, in order

### Part 1 -- Title text (2 draws: shadow + fill)

Source: `ItemInfo + 0x14` (m_pTitle char* pointer).

**Scale computation (before drawing):**
```cpp
uint scale = isHD ? 20 : 25;
if (isHD) {
    float w = Font::MeasureString(font, titleStr);
    if (w * scale > DAT_0015eea8 = 175.0f) {
        float ratio = 175.0f / (w * scale);
        scale = (ratio * 20.0f > 0.0f) ? (uint)(ratio * 20.0f) : 0;
    } else {
        scale = 20;  // non-scaled branch (fVar26 = 1.0 sentinel)
    }
}
```

**Shadow draw:**
```
Vec3 shadowPos = Vec3(4.0, -4.0, 0.0) + local_d0
Colour = (0, 0, 0, 0x40)   // = (0,0,0,64)
Font::DrawString(scale, 1.0, 0.0, font, titleStr, shadowPos, colour, vec2, flags=0xE, 0)
```

**Fill draw:**
```
Vec3 pos = Vec3(0.0, 0.0, 0.0) + local_d0  // = local_d0 itself
Colour = itemColour  (white or grey-200 if locked)
Font::DrawString(scale, 1.0, 0.0, font, titleStr, local_d0, itemColour, vec2, flags=0xE, 0)
```

After both title draws: `local_d0.y -= 26.0f`. All subsequent parts use this
decremented Y.

**PORT FIX**: The shadow offset is `(+4, -4, 0)`. The port has this correct.
The port title draw position matches. The `-26.0` Y adjustment is applied after
the title draw (before cost draw), matching the binary.

---

### Part 2 -- Cost hint text (2 draws: shadow + fill)

Source: `static_block[+0x1C + ItemInfo->m_Type * 4]` -- pointer to one of 4
cost strings indexed by `*(char*)(ItemInfo + 0x10) = m_Type`.

The 4 strings are set in Create (via GETSTRING):
- index 0 (m_Type=0): GETSTRING(0xB7)
- index 1 (m_Type=1): GETSTRING(0xB6)
- index 2 (m_Type=2): GETSTRING(0xB8)
- index 3 (m_Type=3): GETSTRING(0x113)

**Width cache (lazy):**
If `static_block[+0x90] == 0.0f`, measure all 4 strings and store widths:
`static_block[+0x90 + i*4] = MeasureString(font, costStr[i]) * costScale`.

Cost scale:
```cpp
uint costScale = isHD ? (fVar26 * 16.0f) : 0x41A00000;  // 0x41A00000 = 20.0f
// where fVar26 is the title fit ratio (1.0 if no shrink needed).
// HD: costScale = title_ratio * 16.0
// non-HD: costScale = 20.0f (literal)
```

**Shadow draw:** (same offset +4,-4,0)
```
Vec3 shadowPos = Vec3(4.0, -4.0, 0.0) + local_d0
Colour = (0, 0, 0, 64)
Font::DrawString(costScale, ...)  flags=0xE
```

**Fill draw:**
```
Vec3 pos = local_d0   (local_d0.y already -= 26 from Part 1)
Colour = itemColour
Font::DrawString(costScale, ...)  flags=0xE
```

Only drawn if `costStr != nullptr`. The string pointer comes from the static
block at `+0x1C + m_Type * 4`; if null, both shadow and fill are skipped.

**PORT FIX**: The port formats cost as `"FREE"` or `"%d"` directly. The binary
uses 4 pre-set localised strings indexed by `m_Type` from the static block.
The static block strings are populated in `Create()` via GETSTRING(0xB7/B6/B8/113).
The port should look up `static_block[+0x1C + m_Type * 4]` to get the cost string,
not format it inline.

---

### Part 3 -- new_item_sml badge (conditional: m_NewItemAlpha > 0)

Gate: `*(this + 0x25C) > 0.0f`

Texture: `static_block[+0x44]` = new_item_sml texture.

**Scale:**
```cpp
float alpha = *(this + 0x25C);  // m_NewItemAlpha
Vec3 scaleVec = Vec3(DAT_0015eebc, DAT_0015eec0, DAT_0015eec4) = Vec3(65.0, 33.0, 0.0)
scaleVec *= alpha    // Vec3 * float
scaleVec *= alpha    // Vec3 * float  (= alpha^2)
Matrix44::Scale44(scaleVec)
```

**Translate:**
```cpp
// fVar27 = MeasureString(font, titleStr) -- measured BEFORE the title draw (line 196)
// pFVar30 = (Font*)costScale -- cost font scale (reused as float value)
//   The measured title width * costScale gives the cost-text pixel width.
float badgeX = (local_d0.x - fVar27 * (float)pFVar30) - 4.0f;
//   After the early-exit check (ShopScreen field at +0xB8 == 1):
//     if (local_13c.x * 0.25 - DAT_0015f178 <= badgeX): badgeX unchanged
//     The 0.25 factor and DAT_0015f178 = 240.0f limit are clamp logic.
float badgeY = DAT_0015eec8 + local_d0.y + static_block[+0x6C];
// = 34.0f + (pos.y - 26.0f) + badge_y_cache
// = pos.y + 8.0f + badge_y_cache   (badge_y_cache is 0 unless set elsewhere)
float badgeZ = DAT_0015f19c = 0.0f;
Matrix44::GlobalTranslate44(badgeX, badgeY, badgeZ)
```

Colour: `*(Colour**)(r4 + 0x73A4)` = white (255,255,255,255); same as itemColour
white. Note: the badge is ALWAYS drawn white regardless of locked state (binary
does not use itemColour here -- it uses the white singleton directly).

```
Mortar::Texture::Set(static_block[+0x44])
DrawQuad(white_colour)
Mortar::Texture::UnSet(static_block[+0x44])
```

**PORT FIX**: 
- Badge scale correct (65x33, alpha-squared).
- Badge X: port uses `basePos.x - 4.0` (stub). Binary: `(pos.x - measured_title_width * costScale) - 4.0`. The title width measurement happens before the title draw and is stored in `fVar27`.
- Badge Y: port uses `34.0 + basePos.y`. Binary: `34.0 + (pos.y - 26.0) + static[+0x6C]` = `pos.y + 8.0 + badge_y_cache`. Port's `34.0 + basePos.y` is off by 26.0; correct to `8.0 + basePos.y` (since `local_d0.y` has already had 26 subtracted).
- Colour: port uses `itemColour`. Binary uses white singleton. Fix: always draw badge with (255,255,255,255).

---

### Part 4 -- selected_sml highlight ring (conditional: m_SelectedAlpha > 0)

Gate: `*(this + 0x260) > 0.0f`

Texture: `static_block[+0x3C]` (via SmartPtr copy).

**Scale:**
```cpp
float alpha = *(this + 0x260);  // m_SelectedAlpha
Vec3 scaleVec = Vec3(DAT_0015f17c, DAT_0015f180, DAT_0015f19c) = Vec3(65.0, 33.0, 0.0)
scaleVec *= alpha * alpha
Matrix44::Scale44(scaleVec)
```

**Translate:**
```cpp
// Uses the cached cost text width for current item type:
float cachedW = static_block[+0x90 + m_Type * 4];  // pre-measured in Part 2
float selX = (local_d0.x - cachedW) - DAT_0015f184;
// DAT_0015f184 = 32.0f
// = pos.x - 26.0f_adj - cachedCostWidth - 32.0f
// Note: local_d0.x is unchanged (only .y was decremented); local_d0.x = pos.x.
float selY = local_d0.y;    // = pos.y - 26.0f
float selZ = DAT_0015f19c = 0.0f;
Matrix44::GlobalTranslate44(selX, selY, selZ)
```

Colour: white singleton (255,255,255,255).

```
SmartPtr copy of static_block[+0x3C]
Mortar::Texture::Set(local_copy.ptr)
DrawQuad(white_colour)
Mortar::Texture::UnSet(local_copy.ptr)
SmartPtr dtor
```

**PORT FIX**:
- Scale correct (65x33, alpha-squared).
- X: port uses `basePos.x - 32.0`. Binary: `(pos.x - cached_cost_width) - 32.0`. Missing the subtraction of cached cost text pixel width.
- Y: port uses `basePos.y`. Binary uses `local_d0.y = pos.y - 26.0`. Port should use `pos.y - 26.0`.

---

### Part 5 -- Item icon texture (conditional: m_pIconTex.IsValid())

Gate: `SmartPtr<Texture>::operator bool(this + 0x274) == true`

**Scale:**
```cpp
Vec3 scaleVec = Vec3(DAT_0015f188, DAT_0015f188, DAT_0015f19c) = Vec3(64.0, 64.0, 0.0)
Matrix44::Scale44(scaleVec)
```

**Translate:**
```cpp
// global_icon_vec3 = *(*(GOT + 0x73EC))  -- BSS Vec3, init (0,0,0)
// iconPos = _pad2 (this + 0x268), set by Move each frame:
//   _pad2.x = pos.x + 95.2   (= pos.x + DAT_0015d474(35.2f) + m_Size.x(60.0f))
//   _pad2.y = pos.y
//   _pad2.z = pos.z
// translate = global_icon_vec3 + _pad2
float tx = global_icon_vec3.x + _pad2.x  = 0.0 + (pos.x + 95.2) = pos.x + 95.2
float ty = global_icon_vec3.y + _pad2.y  = 0.0 + pos.y           = pos.y
float tz = global_icon_vec3.z + _pad2.z  = 0.0 + pos.z           = pos.z
Matrix44::GlobalTranslate44(tx, ty, tz)
```

**Draw branch (locked vs unlocked):**
```cpp
if (!ItemInfo::IsLocked()) {
    Texture::Set(*(this + 0x274))  // m_pIconTex
    DrawQuad(white_colour)
    Texture::UnSet(*(this + 0x274))
} else {
    Texture::Set(static_block[+0x40])  // locked_stroke.tex
    DrawQuad(white_colour)
    Texture::UnSet(static_block[+0x40])
}
```

Colour is always white singleton (255,255,255,255) -- NOT itemColour.

**PORT FIX**:
- Scale correct (64x64).
- Translate X: port uses `basePos.x`. Binary: `pos.x + 95.2`. Fix: add 95.2 to X.
- Translate Y: port uses `basePos.y`. Binary: `pos.y`. Match (no change).
- Colour: port uses `itemColour`. Binary uses white singleton. Fix: use (255,255,255,255).

---

### Part 6 -- scratch_deviders divider (always drawn)

Texture: `static_block[+0x30]` via `iVar21 + DAT_0015f530 + 0x30`.
(`DAT_0015f530 = GOT offset 0x451b4` = same static block base offset.)

**Scale:**
```cpp
Vec3 scaleVec = Vec3(DAT_0015f198, 17.0f, DAT_0015f19c) = Vec3(257.0, 17.0, 0.0)
Matrix44::Scale44(scaleVec)
```

**Multiply + divide:**
```cpp
// Global float pointer at GOT+0x7214 (DAT_0015f52c = GOT offset 0x7214):
float divider_scale = *(*(GOT + 0x7214));   // runtime float, init to 1.0f
Vec3 scaled = Vec3(257, 17, 0) * divider_scale
local_6c = 2.0f   // divisor stored on stack
Vec3 half = scaled / 2.0f    // = Vec3(128.5, 8.5, 0) if scale=1.0
Vec3 translate = half + this->pos   // uses ORIGINAL this->pos, NOT local_d0
Matrix44::GlobalTranslate44(translate.x, translate.y, translate.z)
```

Effective translate = `(pos.x + 128.5, pos.y + 8.5, 0)` assuming scale=1.0.

**Colour logic (cost-type cache):**
```cpp
int costType = (int)(char)(*(ItemInfo + 0x10));   // m_Type byte, sign-extended
if (static_block[+0x8C] == costType) {
    Colour divColour(255, 255, 255, 200);
} else {
    static_block[+0x8C] = costType;   // update cache
    Colour divColour(128, 128, 128, 255);
}
Texture::Set(divider_tex)
DrawQuad(divColour)
Texture::UnSet(divider_tex)
```

**PORT FIX**:
- Scale: port uses `290.0f` as divider width. Binary: `257.0f`. Fix to 257.0.
- Divider height: both port and binary use 17.0f. Match.
- Translate: port uses `(basePos.x, basePos.y, 0)`. Binary: `(pos.x + 128.5, pos.y + 8.5, 0)` using **original** `this->pos`, not local_d0. Port should use `(pos.x + 128.5, pos.y + 8.5, 0)`.
- Colour logic: port uses `m_bSelected` boolean. Binary uses `costType == static_block[+0x8C]` cache. Fix to use item type cache.

**Second divider (gate: m_bIsNew != 0):**
```cpp
Vec3 scaleVec2 = Vec3(DAT_0015f51c, 17.0f, DAT_0015f53c) = Vec3(257.0, 17.0, 0.0)
Matrix44::Scale44(scaleVec2)
Vec3 scaled2 = scaleVec2 * divider_scale
Vec3 half2 = scaled2 / 2.0
Vec3 translate2 = half2 - this->pos   // operator- : SUBTRACT pos, not add
// = (128.5 - pos.x, 8.5 - pos.y, 0)
Matrix44::GlobalTranslate44(translate2.x, translate2.y, translate2.z)
Colour divColour2(128, 128, 128, 255)   // always grey
Texture::Set(divider_tex)
DrawQuad(divColour2)
Texture::UnSet(divider_tex)
```

**PORT FIX** (second divider):
- Scale: port uses `290.0f`. Binary: `257.0f`.
- Translate: port uses `(pos.x - dividerW, pos.y)`. Binary uses `half2 - pos = (128.5 - pos.x, 8.5 - pos.y)`. Note the sign inversion on BOTH X and Y.

---

### Part 7 -- Description / cost text (conditional: m_CostAlpha > 0, derived alpha > 0)

Gate: `*(this + 0x58) != 0 && *(this + 0x278) != 0` (m_pShopScreen and m_pItemInfo non-null).

**Alpha computation:**
```cpp
uint alphaU = (uint)(*(this + 0x280) * DAT_0015f520);
// DAT_0015f520 = 255.0f
// alphaU = (uint)(m_CostAlpha * 255.0f)
if (alphaU > 0xFE) alphaU = 0xFF;
alphaU &= ~((int)alphaU >> 31);  // clamp negative to 0
uint8_t alpha = (uint8_t)alphaU;
if (alpha == 0) skip Part 7 entirely.
```

**Text source:** `pcVar23 = (char*)(this + 0x5C)` = `m_DescText` inline buffer.

**Font for Part 7:**
```cpp
pFVar22 = *(Game* at GOT+0x7990 + 0x54);  // same as main font, via DAT_0015f534
```

**Font size shrink loop:**
```cpp
float descFontSize = 18.0f;
float h = Font::GetStringHeight(font, m_DescText, 18.0f, DAT_0015f540);
// DAT_0015f540 = 160.0f  -- wrap width
while (h > DAT_0015f524) {    // DAT_0015f524 = 82.5f  -- max height
    descFontSize -= 0.25f;
    h = Font::GetStringHeight(font, m_DescText, descFontSize, 160.0f);
}
```

**X position:** `ShopScreen::GetDescriptionTextXPos()` -- called as a float return.

**Purchase state branch (`*(ItemInfo + 0x24)` = m_RequirementType):**

**Case 0 or 3 (or IsLocked()):**
```cpp
Colour descColour;
if (!ItemInfo::IsLocked()) {
    descColour = Colour(0x74, 0x5D, 0x3B, alpha);   // brown: (116, 93, 59)
} else {
    descColour = Colour(0xFF, 0xFF, 0xFF, alpha);   // white
}
Font::DrawString(GetDescriptionTextXPos(), 0.0, 0.0, descFontSize, 160.0, 0.0, 0.0,
                 font, m_DescText, descColour, flags=0xF, 0)
```

**Case 1 (cost-per-play):**
```cpp
Colour base(0xBD, 0, 0, 255);   // red
bool upsideDown = IsDeviceUpsideDown();
LocalizedString strId = upsideDown ? 0xC3 : 0xC2;
char* str = GETSTRING(strId);
base.a = alpha;
Font::DrawString(GetDescriptionTextXPos(), 0xC1A00000=-20.0, 0.0,
                 descFontSize * DAT_0015f528=0.8, 160.0, 0.0, 0.0,
                 font, str, base_col_with_alpha, flags=3, 0)
// (second draw for the actual description at 10.0 y-offset, scale * 0.9)
Font::DrawString(GetDescriptionTextXPos(), 0x41200000=10.0, 0.0,
                 descFontSize * DAT_0015f538=0.9, 160.0, 0.0, 0.0,
                 font, m_DescText, white_with_alpha, flags=0xF, 0)
```

**Case 2 (played-mode-today):**
```cpp
Colour base(0xBD, 0, 0, 255);
iVar2 = FruitSaveData::PlayedModeToday(saveData, 3);
LocalizedString strId = (iVar2 == 0) ? 0xBB : 0xBC;
if (iVar2 != 0) {
    base = Colour(0xA0, 0xDC, 0, 255);  // green (played today)
}
float fontScale2 = descFontSize * DAT_0015f528 = descFontSize * 0.8f;
float fontScale3 = descFontSize * DAT_0015f538 = descFontSize * 0.9f;
base.a = alpha;
Font::DrawString(xPos, -20.0, 0.0, fontScale2, 160.0, 0.0, 0.0,
                 font, str, base_col_alpha, flags=3, 0)
Font::DrawString(xPos, 10.0, 0.0, fontScale3, 160.0, 0.0, 0.0,
                 font, m_DescText, white_alpha, flags=0xF, 0)
```

**PORT FIX**: The port draws description at `Vec3(xPos, pos.y, 0)` with flags 0xF.
The binary passes `xPos` and zero as separate X/Y parameters to Font::DrawString
(not a Vec3). The port's description draw position is actually correct if
`Font::DrawString(float xPos, float yPos, ...)` matches. The font size shrink
loop (while `h > 82.5f`) is documented but not yet ported -- fix to implement
the while loop. The purchase state 1/2 paths need FruitSaveData wiring.

---

### Part 8 -- loading.tex new-badge stripes (conditional: m_bIsNew != 0)

Gate: `*(this + 0x27E) != 0` (m_bIsNew).

This part is OUTSIDE the `if (m_bOnscreenItem)` block -- it runs regardless of
the onscreen flag, as long as m_bIsNew is set.

Texture: `static_block2[+0x2C]` via `iVar21 + DAT_0015f72c + 0x2C`.
(`DAT_0015f72c = 0x451b4` = same static block base offset.)

```cpp
Texture::Set(loading_tex)

// Stripe 1 (top)
Vec3 scale1 = Vec3(DAT_0015f718, DAT_0015f71c, DAT_0015f720) = Vec3(290.0, 120.0, 0.0)
Matrix44::Scale44(scale1)
float parentPosX = *(float*)(*(this + 0x10) + 8);  // parent->pos.x (Vec3.x at +8)
Matrix44::GlobalTranslate44(parentPosX - 2.0f, DAT_0015f724, DAT_0015f720)
// DAT_0015f724 = 105.0f, DAT_0015f720 = 0.0f
DrawQuad(Colour(0, 0, 0, 0x80))   // = (0,0,0,128)

// Stripe 2 (bottom)
Vec3 scale2 = Vec3(290.0, 120.0, 0.0)
Matrix44::Scale44(scale2)
Matrix44::GlobalTranslate44(parentPosX - 2.0f, DAT_0015f728, DAT_0015f720)
// DAT_0015f728 = -105.0f, DAT_0015f720 = 0.0f
DrawQuad(Colour(0, 0, 0, 0x80))

Texture::UnSet(loading_tex)
```

The X uses `*(this + 0x10) + 8` = `this->m_pParent->pos.x` (parent's Vec3.x
at offset +8 within the parent struct), NOT `this->pos.x`. The `-2.0f` offset is
a literal constant in the binary.

**PORT FIX**:
- Scale: port has `290.0 x 120.0`. Matches binary exactly.
- Translate X: port uses `m_pParent->pos.x - 2.0`. Binary uses `*(*(this+0x10) + 8) - 2.0`. Same thing; port is correct.
- Translate Y: port has `105.0` and `-105.0`. Binary has `DAT_0015f724 = 105.0` and `DAT_0015f728 = -105.0`. Match.
- Colour: port has `(0,0,0,128)`. Binary has `(0,0,0,0x80)`. Match.
- Part 8 is outside the onscreen guard. Port currently has it inside. Move it outside the `if (m_bOnscreenItem)` block.

---

## 6. Constants table

All float constants resolved from binary:

| DAT address  | Hex         | Value    | Usage                                       |
|--------------|-------------|----------|---------------------------------------------|
| 0x0015eea8   | 0x432F0000  | 175.0f   | Title scale-to-fit max pixel width          |
| 0x0015eebc   | 0x42820000  | 65.0f    | new_item_sml badge scale X                  |
| 0x0015eec0   | 0x42040000  | 33.0f    | new_item_sml badge scale Y                  |
| 0x0015eec4   | 0x00000000  | 0.0f     | Z = 0.0f (used throughout)                  |
| 0x0015eec8   | 0x42080000  | 34.0f    | Badge Y = local_d0.y + 34.0 + cache         |
| 0x0015f17c   | 0x42820000  | 65.0f    | selected_sml scale X (same as badge)        |
| 0x0015f180   | 0x42040000  | 33.0f    | selected_sml scale Y (same as badge)        |
| 0x0015f178   | 0x43700000  | 240.0f   | Badge X clamp limit (screen edge guard)     |
| 0x0015f184   | 0x42000000  | 32.0f    | selected_sml X offset from cost text right  |
| 0x0015f188   | 0x42800000  | 64.0f    | Icon texture scale X and Y                  |
| 0x0015f198   | 0x43808000  | 257.0f   | Divider scale X (row width)                 |
| 0x0015f19c   | 0x00000000  | 0.0f     | Divider z=0                                 |
| 0x0015f51c   | 0x43808000  | 257.0f   | Second divider (m_bIsNew) scale X           |
| 0x0015f520   | 0x437F0000  | 255.0f   | Alpha scale: m_CostAlpha * 255.0            |
| 0x0015f524   | 0x42A50000  | 82.5f    | Max description text height (shrink loop)   |
| 0x0015f528   | 0x3F4CCCCD  | 0.8f     | Purchase state 1/2: cost line font scale    |
| 0x0015f53c   | 0x00000000  | 0.0f     | Font DrawString y/z args (0.0)              |
| 0x0015f538   | 0x3F666666  | 0.9f     | Purchase state 1/2: desc line font scale    |
| 0x0015f540   | 0x43200000  | 160.0f   | Description text wrap width                 |
| 0x0015f718   | 0x43910000  | 290.0f   | loading.tex stripe scale X                  |
| 0x0015f71c   | 0x42F00000  | 120.0f   | loading.tex stripe scale Y                  |
| 0x0015f720   | 0x00000000  | 0.0f     | loading.tex z/Y-third arg                   |
| 0x0015f724   | 0x42D20000  | 105.0f   | loading.tex top stripe Y translate          |
| 0x0015f728   | 0xC2D20000  | -105.0f  | loading.tex bottom stripe Y translate       |
| 0x0015d474   | 0x420CCCCD  | 35.2f    | Icon X offset component 1 (in Move)         |

Icon X offset formula (computed in Move, stored in _pad2):
```
_pad2.x = pos.x + DAT_0015d474 + m_Size.x = pos.x + 35.2 + 60.0 = pos.x + 95.2
```

Title Y decrement between Part 1 and Part 2:
```
local_d0.y -= 26.0f   // hardcoded literal in binary
```

---

## 7. Summary of port divergences

| Part | Element          | Port currently                      | Binary (correct)                                           | Fix                                                                 |
|------|------------------|-------------------------------------|------------------------------------------------------------|---------------------------------------------------------------------|
| Guard| onscreen check   | `m_bOnscreenItem` at +0x27C         | `m_bVisible` at +0x2D (ScrollingMenuItem field)            | Test `*(this+0x2D)` not `*(this+0x27C)`                             |
| 2    | Cost string      | Formatted inline `"%d"` / `"FREE"`  | Localised strings from static block, indexed by m_Type     | Use GETSTRING(0xB7/B6/B8/113) array indexed by ItemInfo->m_Type     |
| 2    | Cost draw pos    | `basePos.y` (same as title)         | `local_d0.y = pos.y - 26.0` (decremented after title)     | Draw cost at `pos.y - 26.0` (already done via local_d0)             |
| 3    | Badge X          | `basePos.x - 4.0`                   | `(pos.x - title_width * costScale) - 4.0`                  | Measure title width, subtract `width * costScale + 4.0` from pos.x  |
| 3    | Badge Y          | `34.0 + basePos.y`                  | `34.0 + (pos.y - 26.0) + cache[+0x6C]`                     | Use `8.0 + pos.y` (= 34 - 26); cache is 0 in normal usage          |
| 3    | Badge colour     | `itemColour`                        | White (255,255,255,255)                                    | Always use white for badge                                          |
| 4    | Selected X       | `basePos.x - 32.0`                  | `(pos.x - cached_cost_pixel_width) - 32.0`                 | Subtract measured cost width (from Part 2 cache) before -32         |
| 4    | Selected Y       | `basePos.y`                         | `pos.y - 26.0` (local_d0.y)                                | Use `pos.y - 26.0`                                                   |
| 5    | Icon X           | `basePos.x`                         | `pos.x + 95.2` (from Move, stored in _pad2)                | Use `_pad2.x` (= pos.x + 95.2); Move must write this                |
| 5    | Icon colour      | `itemColour`                        | White (255,255,255,255)                                    | Always use white                                                    |
| 6    | Divider width    | `290.0f`                            | `257.0f`                                                   | Change to 257.0                                                      |
| 6    | Divider translate| `(pos.x, pos.y)`                    | `(pos.x + 128.5, pos.y + 8.5)` (half-extent + original pos)| Add half-extents: `+128.5` X, `+8.5` Y                             |
| 6    | Divider colour   | `m_bSelected` boolean               | cost-type cache: grey if type changed, white-200 if same   | Use static cache indexed by m_Type                                  |
| 6    | Div2 translate   | `(pos.x - 290, pos.y)`              | `(128.5 - pos.x, 8.5 - pos.y)` (half - pos, both negated) | Negate: `(128.5 - pos.x, 8.5 - pos.y)`                             |
| 7    | Desc font shrink | Fixed 18.0f (stub)                  | Loop: `while h > 82.5f: size -= 0.25f`                     | Implement shrink loop using Font::GetStringHeight + wrap width 160   |
| 8    | Stripe position  | Inside `m_bOnscreenItem` guard      | Outside the guard (always drawn when m_bIsNew)             | Move Part 8 outside the onscreen guard block                        |

---

## 8. Move function (_pad2 / icon position)

Binary address: **0x0015d1fc** (vtable slot 6, +0x18 from ShopListItem vtable).

Move writes the icon position (this+0x268 = _pad2) each frame:
```cpp
// Always: copy pos into _pad2
*(Vec3*)(this + 0x268) = pos;   // _pad2.{x,y,z} = pos.{x,y,z}
// If m_pIconTex.IsValid():
float iconXOff = DAT_0015d474(35.2f) + *(this + 0x18)(m_Size.x = 60.0f) = 95.2f;
*(float*)(this + 0x268) += iconXOff;  // _pad2.x = pos.x + 95.2
```

The port's Move stub only sets `pos.x/y/z`. It must also update `_pad2` to
enable the correct icon translate in Draw.
