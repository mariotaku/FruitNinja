<!-- Analysed: 2026-04-26T14:00 -->

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

---

## Position re-verification (2026-04-26T14:00)

Full re-decompile of 0x0015eb00 + disassembly at key call sites. Three previous spec errors
found and corrected below.

---

### RE-FINDING 1: local_d0 is NOT a plain copy of pos

**Prior spec said**: `local_d0 = Vec3(this->pos)`.

**Binary (0x0015eb70 - 0x0015eb7c)**:
```
0015eb70: adds r2,r5,#0x4       ; r2 = &this->pos   (Vec3 at +0x04)
0015eb72: str r2,[sp,#0x14]     ; [sp+14] = &this->pos (saved as p_Var9)
0015eb74: add r0,sp,#0x338      ; r0 = &local_d0 (result slot)
0015eb76: add.w r2,r5,#0x18     ; r2 = &this->m_Size  (Vec3 at +0x18 = (60,13,0))
0015eb7a: ldr r1,[sp,#0x14]     ; r1 = &this->pos
0015eb7c: blx 0x00103290        ; _Vector3::operator+(result=r0, lhs=r1, rhs=r2)
```

`_Vector3::operator+` (0x001176cc, via thunk 0x00103290):
```c
_Vector3(result, lhs->x + rhs->x, lhs->y + rhs->y, lhs->z + rhs->z);
```

**Resolved**: `local_d0 = Vec3(pos.x + m_Size.x, pos.y + m_Size.y, pos.z + 0)` where
`m_Size = (60.0f, 13.0f, 0.0f)` (set in Create from DAT_0015caf0=60.0 and literal 13.0).

So at function entry:
```
local_d0.x = pos.x + 60.0f
local_d0.y = pos.y + 13.0f
local_d0.z = pos.z
```

After Part 1 title draw, `local_d0.y -= 26.0f`:
```
local_d0.y = pos.y + 13.0 - 26.0 = pos.y - 13.0f
```

**p_Var9** (= `&this->pos`, stored at `[sp+0x14]`) is the RAW pos pointer, used for the divider
translate in Part 6. The divider does NOT use local_d0.

**Impact on all text-draw positions** (title, cost, badge, selected ring, dividers -- see elements
1-3 below).

---

### RE-FINDING 2: scratch_deviders texture is confirmed at static_block[+0x30]

**Binary (ShopScreen::LoadContent at 0x0015cb08)**:
```
; Load 2: scratch_deviders.tex -> stored into static_block[+0x30]
0015cb38: ldr r1,[0x0015ccb0]    ; r1 = string ptr offset for scratch_deviders.tex
0015cb3a: mov r0,r5
0015cb3c: adds r1,r4,r1           ; r1 = GOT_base + 0xFFFD0005 -> string at 0x001BC735 area
0015cb3e: blx 0x00104298          ; TextureManager::LoadLocalisedTexture(&tmp, "scratch_deviders.tex")
0015cb42: ldr r0,[0x0015ccac]     ; r0 = static_block GOT offset = 0x000451b4
0015cb44: mov r1,r5
0015cb46: adds r0,r4,r0           ; r0 = static_block base
0015cb48: adds r0,#0x30           ; r0 += 0x30 -> static_block[+0x30]
0015cb4a: blx 0x000f9b34          ; SmartPtr::operator=(static_block[+0x30], tmp)
```

`DAT_0015ccac = DAT_0015f530 = 0x000451b4` (both same GOT entry, confirmed by memory read).
Texture string is `scratch_deviders.tex` (note intentional typo -- "deviders", not "dividers").
The port uses `s_TexScratch` which should load `scratch_deviders.tex`.

**Confirmed**: +0x30 = scratch_deviders.tex. +0x38 = another texture (from load 6 at +0x38). No
change needed to the texture lookup -- the doc was correct that the divider texture is at +0x30.

---

## Element 1: scratch_dividers (Part 6) -- position re-verification

### Binary call sequence

**Part 6a -- first divider (always drawn)**:
```c
// 0x0015f1a0: build Vec3(257.0f, 17.0f, 0.0f)
_Vector3(&_Stack_1b4, DAT_0015f198=257.0f, 17.0f, DAT_0015f19c=0.0f);
// 0x0015f1b2: Scale44 from that Vec3
_Matrix44::Scale44(... sx=257, sy=17, sz=0)
// 0x0015f1c2: multiply scaled Vec3 by divider_scale (*(float**)(iVar21+DAT_0015f52c=0x7214))
_Vector3::operator*(&_Stack_1c0, *(float**)(iVar21+0x7214));
// 0x0015f1de: local_6c = 2.0f (divisor on stack at sp+0x39c)
vstr.32 s15,[sp,#0x39c]          ; 2.0f
// 0x0015f1e6: divide scaled Vec3 by 2.0 -> half
_Vector3::operator/(&_Stack_1cc, &_Stack_1c0, &sp[0x39c]);  // half = scaled/2
// 0x0015f1ee: ldr r1,[sp,#0x14] -> r1 = &this->pos (p_Var9, the ORIGINAL pos)
// 0x0015f1f0: blx 0x00103290 -> operator+(result=r0, lhs=r1, rhs=r2)
//   r2 = &_Stack_1cc = half vector;  r1 = &this->pos
_Vector3::operator+(&_Stack_1d8, &this->pos, half);   // result = pos + half
// 0x0015f1f8: blx 0x000f7a4c -> GlobalTranslate44
_Matrix44::GlobalTranslate44(&_Stack_3b0, tx=result.x, ty=result.y, tz=result.z)
// 0x0015f20e: ldr r0,[r3,#0x30] -> texture = static_block[+0x30] = scratch_deviders.tex
Mortar::Texture::Set(static_block[+0x30])
// colour cache and DrawQuad: as prior spec
```

**Part 6b -- second divider (gate: m_bIsNew)**:
```c
// 0x0015f276: check m_bIsNew != 0
// 0x0015f27a: build Vec3(DAT_0015f51c=257.0f, 17.0f, DAT_0015f53c=0.0f)
_Vector3(&_Stack_1e4, 257.0f, 17.0f, 0.0f);
_Matrix44::Scale44(sx=257, sy=17, sz=0)
// multiply * divider_scale, divide by 2.0 -> half2
// 0x0015f2bc: mov r2,r10 -> r2 = half2
// 0x0015f2be: ldr r1,[sp,#0x14] -> r1 = &this->pos (ORIGINAL pos)
// 0x0015f2c2: blx 0x000ff414 -> operator-(result, lhs, rhs)
//             _Vector3::operator-(result=r0, lhs=r1=&this->pos, rhs=r2=half2)
_Vector3::operator-(&result, &this->pos, half2);  // result = pos - half
// result = (pos.x - 128.5, pos.y - 8.5, 0) when divider_scale=1.0
_Matrix44::GlobalTranslate44(tx=pos.x-128.5, ty=pos.y-8.5, tz=0)
// 0x0015f2dc-0x0015f2e0: Texture::Set(static_block[+0x30]) = scratch_deviders.tex
// 0x0015f2e6-0x0015f2f6: DrawQuad(Colour(0x80,0x80,0x80,0xFF)) -- always grey
```

### Resolved constants table (Part 6)

| DAT address | Hex value  | Float value | Usage                              |
|-------------|------------|-------------|------------------------------------|
| 0x0015f198  | 0x43808000 | 257.0f      | Divider scale X (first divider)    |
| 0x0015f51c  | 0x43808000 | 257.0f      | Divider scale X (second divider)   |
| 0x0015f53c  | 0x00000000 | 0.0f        | Divider scale Z; second divider Y  |
| 0x0015f19c  | 0x00000000 | 0.0f        | First divider scale Z (shared ref) |
| 0x0015f52c  | 0x00007214 | GOT offset  | divider_scale float** in GOT       |

Both DAT_0015ccac and DAT_0015f530 = 0x000451b4 (same static block GOT entry, verified by
memory read).

### Final position formula (Part 6)

```
divider_scale = *(float*)(GOT + 0x7214)   // ~1.0f at runtime
half = Vec3(257.0f, 17.0f, 0.0f) * divider_scale / 2.0f
     = Vec3(128.5f, 8.5f, 0.0f)  when divider_scale = 1.0f

Divider 1 translate:  pos + half = (pos.x + 128.5,  pos.y + 8.5,  0)
Divider 2 translate:  pos - half = (pos.x - 128.5,  pos.y - 8.5,  0)
```

Note: `pos` here is the RAW `this->pos`, NOT `local_d0` (which adds m_Size).

### Diff against current port code (Part 6)

`src/hud/ShopListItem.cpp` line 485:
```cpp
// PORT (wrong):
matDiv2.GlobalTranslate44(halfW2 - pos.x, halfH2 - pos.y, 0.0f);

// BINARY (correct):
// operator-(result, lhs=&this->pos, rhs=half) = pos - half
matDiv2.GlobalTranslate44(pos.x - halfW2, pos.y - halfH2, 0.0f);
// = (pos.x - 128.5f, pos.y - 8.5f, 0.0f)
```

Divider 1 (line 462) is **already correct**: `pos.x + halfW, pos.y + halfH`. No change needed.

---

## Element 2: Title text position -- re-verification

### Binary call sequence (title draw setup)

```c
// 0x0015eb70-0x0015eb7c:
// r2 = this+0x4 = &pos
// r2' = this+0x18 = &m_Size (= {60.0f, 13.0f, 0.0f} from Create)
// r0 = &local_d0
// _Vector3::operator+(local_d0, this->pos, this->m_Size)
// => local_d0 = pos + m_Size = (pos.x+60, pos.y+13, pos.z)

// 0x0015ec20+: shadow draw
_Vector3(&_Stack_dc, 4.0f, -4.0f, 0.0f);
_Vector3::operator+(shadow_pos, &_Stack_dc, &local_d0);
// shadow_pos = (4,−4,0) + local_d0 = (pos.x+64, pos.y+9, pos.z)

// fill draw
_Vector3(&_Stack_f4, 0.0f, 0.0f, 0.0f);
_Vector3::operator+(fill_pos, &_Stack_f4, &local_d0);
// fill_pos = local_d0 = (pos.x+60, pos.y+13, pos.z)
```

### Resolved constants table (Element 2)

| DAT address | Hex value  | Float value | Usage                               |
|-------------|------------|-------------|-------------------------------------|
| 0x0015eec4  | 0x00000000 | 0.0f        | Shadow/fill z arg; Vec3 zero comp   |
| this+0x18   | set by Create | 60.0f    | m_Size.x offset into local_d0.x     |
| this+0x1C   | set by Create | 13.0f    | m_Size.y offset into local_d0.y     |
| (literal)   | 26.0f       | 26.0f       | local_d0.y decrement after title    |

### Final position formula (Element 2)

```
local_d0 = Vec3(pos.x + 60.0f, pos.y + 13.0f, pos.z)   // at function entry

Title shadow pos = (local_d0.x + 4, local_d0.y - 4, local_d0.z)
                 = (pos.x + 64,     pos.y + 9,      pos.z)

Title fill pos   = local_d0
                 = (pos.x + 60,     pos.y + 13,     pos.z)

After title draw:  local_d0.y -= 26.0f  ->  local_d0.y = pos.y + 13 - 26 = pos.y - 13.0f

Cost shadow pos  = (local_d0.x + 4, local_d0.y - 4, local_d0.z)
                 = (pos.x + 64,     pos.y - 17,     pos.z)

Cost fill pos    = local_d0
                 = (pos.x + 60,     pos.y - 13,     pos.z)
```

**Alignment flag 0xE** = bits 1+2+3 = FONT_ALIGN_RIGHT(0x2) | FONT_ALIGN_MIDDLE(0x4) |
FONT_ALIGN_BOTTOM(0x8). Both title shadow and fill use 0xE.

### Diff against current port code (Element 2)

`src/hud/ShopListItem.cpp` line 225:
```cpp
// PORT (wrong):
Vec3 local_d0(pos.x, pos.y, pos.z);

// BINARY (correct):
Vec3 local_d0(pos.x + m_Size.x, pos.y + m_Size.y, pos.z);
// = (pos.x + 60.0f, pos.y + 13.0f, pos.z)
```

This single change propagates correctly to:
- Title shadow/fill positions (lines 261, 268) -- use local_d0 directly, no further change.
- Cost shadow/fill positions (lines 319, 325) -- use local_d0 after -26 decrement, no further change.
- Badge X/Y (lines 345-348) -- see Element 3 sub-impact below.
- Selected ring X/Y (lines 378-379) -- see Element 3 sub-impact below.

**Badge X formula correction (Part 3)**:

`src/hud/ShopListItem.cpp` line 345:
```cpp
// PORT (wrong -- uses costScale instead of titleScale, and wrong base):
float badgeX = (local_d0.x - fVar27 * costScale) - 4.0f;

// BINARY (0x0015ef26 vsub.f32 s17,s14,s17 where s17=fVar27*s18=fVar27*titleScale):
// s14 = local_d0.x; s17 = fVar27 * titleScale (from 0x0015ec04 vmul.f32 s17,s0,s18)
float badgeX = (local_d0.x - fVar27 * titleScale) - 4.0f;
```

`fVar27` = second MeasureString of title string (at 0x0015ebf0-0x0015ebfa).
`titleScale` = s18 = the integer scale (20 or 25, or shrunk value), NOT costScale.

After fixing local_d0 to `pos+m_Size`:
```
badgeX = ((pos.x + 60) - fVar27 * titleScale) - 4
badgeY = 34.0f + local_d0.y + 0.0f = 34 + (pos.y + 13 - 26) = pos.y + 21.0f
```

---

## Element 3: descText position -- re-verification

### Binary call sequence (Part 7 description draw)

The description text uses a DIFFERENT DrawString overload from title/cost:

**Title/Cost DrawString** (thunk at 0x00104118, real at 0x00198e44):
```
Font_DrawString(scale, 1.0f, 0.0f, font*, string_iter, Vec3* pos, colour*, vec2, flags, 0)
```

**Description DrawString** (direct at 0x000fd80c):
```
DrawString(Font* this, string_iter, float xPos, float yPos, float zPos,
           Colour, float fontSize, float wrapWidth, float z1, float z2,
           int flags, rect, float, 0)
```
In VFP calling convention: s0=xPos, s1=yPos, s2=?, s3=fontSize, s4=wrapWidth, s5=0, s6=0;
r0=font*, r1=string, r2=colour, r3=flags, [sp+0]=0.

**Case 0/3 description draw (0x0015f5b6 - 0x0015f5de)**:
```
0015f5b6: vldr.32 s1,[pc,#-0x7c]  ; s1 = [0x0015f53c] = 0.0f  (yPos)
0015f5ba: mov r0,r7               ; r0 = font* (loaded above)
0015f5bc: vmov.f32 s0,s17        ; s0 = s17 = GetDescriptionTextXPos() result  (xPos)
0015f5c0: add r1,sp,#0x98        ; r1 = string iterator
0015f5c2: add r2,sp,#0x36c       ; r2 = colour
0015f5c4: vldr.32 s4,[pc,#-0x88] ; s4 = wrap width = DAT_0015f540 = 160.0f
0015f5c8: movs r3,#0xf           ; r3 = flags 0xF
0015f5ce: vmov.f32 s3,s16        ; s3 = s16 = descFontSize (after shrink loop)
0015f5d2: vmov.f32 s2,s1         ; s2 = 0.0f
0015f5d6: vmov.f32 s5,s1         ; s5 = 0.0f
0015f5da: vmov.f32 s6,s1         ; s6 = 0.0f
0015f5de: blx 0x000fd80c          ; Font::DrawString
```

Where `s17` at this point contains `ShopScreen::GetDescriptionTextXPos()` return value, saved earlier:
```
; (state 0/3 path) ...
; Earlier: call GetDescriptionTextXPos, result in s0, then vmov.f32 s17,s0
```

**State 1 first line (0x0015f4e6 - 0x0015f4f6)**:
```
0015f4e6: vmov.f32 s1,0xc1a00000  ; s1 = -20.0f  (yPos)
0015f4ea: vmov.f32 s0,s18         ; s0 = xPos from GetDescriptionTextXPos (in s18)
...
0015f4f6: blx 0x000fd80c
```

**State 1/2 second line (0x0015f57a - 0x0015f58a)**:
```
0015f57a: vmov.f32 s1,0x41200000  ; s1 = +10.0f  (yPos)
0015f57e: vmov.f32 s0,s16         ; s0 = xPos from second GetDescriptionTextXPos call
...
0015f58a: blx 0x000fd80c
```

### Resolved constants table (Element 3)

| Address/register | Value          | Usage                                             |
|------------------|----------------|---------------------------------------------------|
| DAT_0015f53c     | 0.0f           | Description text yPos (case 0/3) and zPos args    |
| 0xC1A00000       | -20.0f         | State 1/2 first-line yPos                         |
| 0x41200000       | +10.0f         | State 1/2 second-line yPos                        |
| DAT_0015f540     | 160.0f (0x43200000) | Wrap width for description text                |
| 0x000fd80c       | --             | Description DrawString entry point (float x/y/z)  |
| 0x00104118       | --             | Title/Cost DrawString entry point (Vec3 pos)       |
| `ShopScreen::GetDescriptionTextXPos()` | 65.0f at full-open | Returns world-Y position of desc panel |

`GetDescriptionTextXPos` (0x0015c520) analysis:
```
DAT_0015c55c = 145.0f  (base X position)
DAT_0015c560 = 190.0f  (animation travel distance)
DAT_0015c564 = 80.0f   (centering offset)

if (this->field[+0x7C] < 1.0):   // ARM idiom fires when < 1.0
    xPos = 145 + (1.0 - alpha) * 190 * 1.5
else:
    xPos = 145.0f
return xPos - 80.0f
// At alpha=1.0 (fully visible): 145.0 - 80.0 = 65.0f
// At alpha=0.0 (hidden):        145.0 + 285.0 - 80.0 = 350.0f (off-screen right)
```

This returns the **world-space Y coordinate** of the description text panel (in the centered ortho
space where Y is horizontal, +240=right edge). The return value is passed as `xPos` in DrawString
because DrawString's first float arg in VFP (s0) maps to world X but in this layout the panel
slides horizontally (Y axis). See note below.

**IMPORTANT NOTE on X vs Y confusion**: The binary DrawString at 0x000fd80c takes s0=xPos (world
X), s1=yPos (world Y). In the centered ortho space, world-X is the VERTICAL axis (+160 top,
-160 bottom) and world-Y is the HORIZONTAL axis (-240 left, +240 right). So:
- Description text `xPos = GetDescriptionTextXPos() = 65.0f` means **vertical position** (65 units
  from the top, roughly center height)
- `yPos = 0.0f` means **horizontal center** of the screen.

The description text panel is a fixed centered panel in the middle of the screen, not per-row.

### Final position formula (Element 3)

```
xPos = ShopScreen::GetDescriptionTextXPos()
     = 65.0f (when panel fully open; ShopScreen::field[+0x7C] == 1.0)

Case 0 or 3 (normal description):
  DrawString(font, xPos, yPos=0.0f, zPos=0.0f, fontSize, wrapWidth=160.0f, ...)

Case 1 or 2, first line (cost-per-play / played-mode):
  DrawString(font, xPos, yPos=-20.0f, zPos=0.0f, fontSize*0.8, wrapWidth=160.0f, ...)

Case 1 or 2, second line (desc text):
  DrawString(font, xPos, yPos=+10.0f, zPos=0.0f, fontSize*scale2, wrapWidth=160.0f, ...)
  where scale2 = 0.9 (state 1) or 0.9 (state 2, after DAT_0015f538 multiplication)
```

The Y value is NOT `local_d0.y`. It is a FIXED world-space position: 0.0f (center), -20.0f,
or +10.0f -- these are offsets from the screen center Y, not from the row's pos.y.

### Diff against current port code (Element 3)

`src/hud/ShopListItem.cpp` lines 554-592:

All description draws currently use `Vec3(xPos, local_d0.y, local_d0.z)`. They should pass
`Vec3(xPos, 0.0f, 0.0f)` for case 0/3, and `Vec3(xPos, -20.0f, 0.0f)` / `Vec3(xPos, 10.0f, 0.0f)`
for the two lines of case 1/2.

```cpp
// PORT (wrong -- all cases):
Vec3 descPos(xPos, local_d0.y, local_d0.z);
font->DrawStringSized(descFontSize, 0.0f, 0.0f, descStr, descPos, descColour, 0xF);

// BINARY (correct, case 0/3):
Vec3 descPos(xPos, 0.0f, 0.0f);   // yPos=0.0f, zPos=0.0f
font->DrawStringSized(descFontSize, 0.0f, 0.0f, descStr, descPos, descColour, 0xF);

// BINARY (correct, case 1/2 first line):
Vec3 descLine1Pos(xPos, -20.0f, 0.0f);  // yPos=-20.0f (0xC1A00000)
font->DrawStringSized(descFontSize * 0.8f, 0.0f, 0.0f, costLineStr, descLine1Pos, baseColour, 3);

// BINARY (correct, case 1/2 second line):
Vec3 descLine2Pos(xPos, 10.0f, 0.0f);   // yPos=+10.0f (0x41200000)
font->DrawStringSized(descFontSize * 0.9f, 0.0f, 0.0f, descStr, descLine2Pos, whiteAlpha, 0xF);
```

---

## Recommended port edits (summary)

All edits are in `src/hud/ShopListItem.cpp`.

### Edit A: Fix local_d0 initialization (line 225)

```cpp
// Before:
Vec3 local_d0(pos.x, pos.y, pos.z);

// After:
Vec3 local_d0(pos.x + m_Size.x, pos.y + m_Size.y, pos.z);
// Binary: local_d0 = pos + m_Size; m_Size = (60.0f, 13.0f, 0.0f) from Create
```

### Edit B: Fix badge X to use titleScale not costScale (line 345)

```cpp
// Before:
float badgeX = (local_d0.x - fVar27 * costScale) - 4.0f;

// After:
float badgeX = (local_d0.x - fVar27 * titleScale) - 4.0f;
// Binary: s17 = fVar27 * s18 where s18 = titleScale integer cast to float
// (0x0015ec04: vmul.f32 s17,s0,s18; used at 0x0015ef26: vsub.f32 s17,s14,s17)
```

After Edit A, badge Y (line 348) `34.0f + local_d0.y` automatically becomes
`34.0f + (pos.y + 13.0 - 26.0) = pos.y + 21.0f` which is correct -- no separate change needed.

### Edit C: Fix second divider translate sign (line 485)

```cpp
// Before:
matDiv2.GlobalTranslate44(halfW2 - pos.x, halfH2 - pos.y, 0.0f);

// After:
matDiv2.GlobalTranslate44(pos.x - halfW2, pos.y - halfH2, 0.0f);
// Binary (0x0015f2bc-0x0015f2c2): operator-(result, lhs=&this->pos, rhs=half)
// = pos - half = (pos.x - 128.5, pos.y - 8.5, 0)
```

### Edit D: Fix description text Y position (lines 554-592, all descPos Vec3 constructions)

**Case 0/3 (line 556):**
```cpp
// Before:
Vec3 descPos(xPos, local_d0.y, local_d0.z);

// After:
Vec3 descPos(xPos, 0.0f, 0.0f);
// Binary: s0=xPos, s1=0.0f (DAT_0015f53c), s2=0.0f
```

**Case 1, first line (line 567):**
```cpp
// Before:
Vec3 descPos(xPos, local_d0.y, local_d0.z);

// After:
Vec3 descPos(xPos, -20.0f, 0.0f);
// Binary: s1=0xC1A00000=-20.0f (0x0015f4e6: vmov.f32 s1,0xc1a00000)
```

**Case 1, second line (line 572):**
```cpp
// Before:
Vec3 descPos2(xPos, local_d0.y, local_d0.z);

// After:
Vec3 descPos2(xPos, 10.0f, 0.0f);
// Binary: s1=0x41200000=+10.0f (0x0015f57a: vmov.f32 s1,0x41200000)
```

**Case 2, first line (line 582) and second line (line 587):** same pattern as case 1 (-20.0f and +10.0f).

---

## Final summary (round 1)

| Element | Bullet                                                                                              | Line(s)      | Confidence |
|---------|-----------------------------------------------------------------------------------------------------|--------------|------------|
| local_d0 base | Port initializes from `pos`; binary adds `m_Size=(60,13,0)` -> local_d0 = pos+(60,13,0)   | 225          | HIGH       |
| Divider 1 translate | Port: `pos + half` -- CORRECT, no change needed                                    | 462          | HIGH       |
| Divider 2 translate | Port: `half - pos`; binary: `pos - half` = (pos.x-128.5, pos.y-8.5, 0)            | 485          | HIGH       |
| Title position | After Edit A (local_d0 fix), title draws at `(pos.x+60, pos.y+13, z)` -- correct       | 261/268      | HIGH       |
| Badge X | Uses `titleScale` not `costScale`; after Edit A base is correct                              | 345          | HIGH       |
| Badge Y | After Edit A: `34 + (pos.y+13-26) = pos.y+21` -- correct                                   | 348          | HIGH       |
| Selected Y | After Edit A: `local_d0.y = pos.y-13` -- correct                                         | 379          | HIGH       |
| descText Y | Port uses `local_d0.y`; binary uses 0.0f (case 0/3) or -20.0f/+10.0f (cases 1/2)      | 556-589      | HIGH       |

All evidence comes from disassembly confirmation at specific instruction addresses; no
reliance on Ghidra decompile interpretation alone. Confidence is HIGH for all items.

---

## Position re-verification round 2 (2026-04-26T17:30)

Fresh-pass ARM disassembly audit. Instruction addresses cited for every assertion.
Context: dividers were already fixed (port now uses `pos +/- yAxisUnit * m_Height / 2`).
Focus: title position (local_d0 and shadow/fill), descText positions.

---

### Title position -- confirmed

**local_d0 init (0x0015eb70-0x0015eb7c)**:
```
0015eb70: adds r2,r5,#0x4       ; r2 = &this->pos   (Vec3 at +0x04)
0015eb72: str r2,[sp,#0x14]     ; [sp+14] = &this->pos (p_Var9)
0015eb74: add r0,sp,#0x338      ; r0 = &local_d0 (sp+0x338)
0015eb76: add.w r2,r5,#0x18     ; r2 = &this->m_Size (Vec3 at +0x18 = {60, 13, 0})
0015eb7a: ldr r1,[sp,#0x14]     ; r1 = &this->pos
0015eb7c: blx 0x00103290        ; operator+(r0=&local_d0, r1=&pos, r2=&m_Size)
                                 ; local_d0 = pos + m_Size = (pos.x+60, pos.y+13, pos.z)
```

r2 is `this+0x18` = m_Size field. The prior spec assertion "local_d0 = pos + m_Size" is CONFIRMED.

**Shadow Vec3 build (0x0015ec20-0x0015ec4a)**:
```
0015ec28: vmov.f32 s0,0x40800000 ; s0 = +4.0f
0015ec3c: vmov.f32 s1,0xc0800000 ; s1 = -4.0f
0015ec2c: vldr.32 s2,[pc,#0x294] ; s2 = 0.0f (from DAT near 0x0015eec4)
0015ec40: blx 0x000ff5ac          ; Vec3_init(sp+0x32c, 4.0f, -4.0f, 0.0f)
0015ec44: mov r2,r11              ; r2 = &Vec3(4,-4,0) at sp+0x32c
0015ec46: mov r0,r8               ; r0 = &shadowPos (sp+0x320)
0015ec48: add r1,sp,#0x338        ; r1 = &local_d0
0015ec4a: blx 0x00103290          ; shadowPos = local_d0 + Vec3(4,-4,0)
                                  ; = (pos.x+64, pos.y+9, pos.z)
```

**Fill Vec3 build (0x0015ecaa-0x0015ecd4)**:
```
0015ecb2: vldr.32 s0,[pc,#0x210] ; s0 = 0.0f
0015ecba: vmov.f32 s1,s0          ; s1 = 0.0f
0015ecc6: vmov.f32 s2,s0          ; s2 = 0.0f
0015ecca: blx 0x000ff5ac          ; Vec3_init(sp+0x314, 0, 0, 0) = zeroVec
0015ecce: mov r2,r11              ; r2 = &zeroVec
0015ecd0: mov r0,r10              ; r0 = &fillPos (sp+0x308)
0015ecd2: add r1,sp,#0x338        ; r1 = &local_d0
0015ecd4: blx 0x00103290          ; fillPos = local_d0 + Vec3(0,0,0) = local_d0
                                  ; = (pos.x+60, pos.y+13, pos.z)
```

**Shadow draw (0x0015ec62-0x0015ec90)**:
- r2 = &shadowPos = &(sp+0x320) -- confirmed.
- s0 = s18 = titleScale (float), s1 = 1.0f, s2 = 0.0f.
- r12 = 0xE = flags.
- `blx 0x00104118` -- Font::DrawString with Vec3* pos arg.

**Fill draw (0x0015ecce-0x0015ed0e)**:
- r2 = &fillPos = &(sp+0x308) = local_d0 copy -- confirmed.
- Same font/scale/flags.

**local_d0.y decrement (0x0015ed1e-0x0015ed3e)**:
```
0015ed1e: vmov.f32 s14,0x41d00000 ; s14 = 26.0f  (0x41D00000 confirmed = 26.0f)
0015ed22: vldr.32 s13,[sp,#0x33c] ; s13 = local_d0.y  (sp+0x33c = sp+0x338+4)
0015ed34: vsub.f32 s14,s13,s14    ; s14 = local_d0.y - 26.0f
0015ed3e: vstr.32 s14,[sp,#0x33c] ; write back -> local_d0.y -= 26.0f
```

Confirmed: decrement is 26.0f, applied to sp+0x33c (local_d0.y). No other fields touched.

**C++ pseudocode (title block)**:
```cpp
Vec3 local_d0(pos.x + m_Size.x, pos.y + m_Size.y, pos.z);  // pos + {60,13,0}

// Shadow
Vec3 shadowPos(local_d0.x + 4.0f, local_d0.y - 4.0f, local_d0.z);
Font::DrawString(font, titleStr, titleScale, 1.0f, 0.0f,
                 &shadowPos, Colour(0,0,0,64), vec2, 0xE, 0);

// Fill
Font::DrawString(font, titleStr, titleScale, 1.0f, 0.0f,
                 &local_d0, itemColour, vec2, 0xE, 0);

local_d0.y -= 26.0f;   // 0x41D00000
```

**Diff vs port (src/hud/ShopListItem.cpp)**:
Current port (line 228): `Vec3 local_d0(pos.x + m_Size.x, pos.y + m_Size.y, pos.z)` -- CORRECT.
Shadow (line 264): `Vec3 shadowPos(local_d0.x + 4.0f, local_d0.y - 4.0f, local_d0.z)` -- CORRECT.
Fill (line 270): passes `local_d0` directly -- CORRECT.
Decrement (line 278): `local_d0.y -= 26.0f` -- CORRECT.

**Recommendation: KEEP. All title positions are correct.**

---

### NEW FINDING: integer pixel-snapping on badge and selected-ring translate

Both badge (Part 3) and selected ring (Part 4) run their translate coordinates through
`vcvt.s32.f32 / vcvt.f32.s32` before calling GlobalTranslate44. Title/cost draws do NOT snap.

**Badge snap (0x0015ef90-0x0015efac)**:
```
0015ef90: vcvt.s32.f32 s2,s2     ; z = truncate(z) -> float
0015ef94: add r6,sp,#0x58
0015ef98: vcvt.s32.f32 s0,s0     ; x = truncate(x)
0015ef9c: vcvt.s32.f32 s1,s1     ; y = truncate(y)
0015efa0: vcvt.f32.s32 s0,s0     ; x back to float (integer grid)
0015efa4: vcvt.f32.s32 s1,s1     ; y back to float
0015efa8: vcvt.f32.s32 s2,s2     ; z back to float
0015efac: blx 0x00107b80          ; GlobalTranslate44(snap_x, snap_y, snap_z)
```

**Selected ring snap (0x0015f064-0x0015f06c)**:
```
0015f064: vcvt.s32.f32 s0,s0     ; x only (y and z not snapped for selected ring)
0015f068: vcvt.f32.s32 s0,s0     ; back to float
0015f06c: blx 0x00107b80          ; GlobalTranslate44(snap_x, s1=local_d0.y, s2=0)
```

Note: for the selected ring, only X is snapped; Y is passed as-is (s1 = local_d0.y from
the vldr at 0x0015f040, not through vcvt).

**Current port**: Neither badge nor selected ring snaps coordinates.

**Recommendation: CHANGE.** Add integer snapping:
```cpp
// Badge (Part 3):
float bx = (float)(int)(local_d0.x - fVar27 * titleScale - 4.0f);
float by = (float)(int)(34.0f + local_d0.y);   // cache = 0
float bz = 0.0f;
matBadge.GlobalTranslate44(bx, by, bz);

// Selected ring (Part 4):
float sx = (float)(int)(local_d0.x - cachedCostW - 32.0f);  // only X snapped
float sy = local_d0.y;     // NOT snapped
matSel.GlobalTranslate44(sx, sy, 0.0f);
```

---

### Description text position -- case 0/3 confirmed

**GetDescriptionTextXPos call for case 0/3 (0x0015f6de-0x0015f6f8)**:
```
0015f6ec: ldr r0,[r5,#0x58]      ; r0 = m_pShopScreen
0015f6ee: blx 0x00101484          ; GetDescriptionTextXPos() -> s0
0015f6f2: ldr.w r0,[r5,#0x278]   ; r0 = ItemInfo* (for IsLocked check after)
0015f6f6: vmov.f32 s17,s0         ; s17 = xPos
0015f6fa: blx 0x00101364          ; IsLocked()
...
; -> branch to colour setup (brown or white)
; -> DrawString call:
0015f5b6: vldr.32 s1,[pc,#-0x7c] ; s1 = DAT_0015f53c = 0.0f  (yPos)
0015f5bc: vmov.f32 s0,s17         ; s0 = xPos from GetDescriptionTextXPos()
0015f5ce: vmov.f32 s3,s16         ; s3 = descFontSize (from shrink loop, in s16)
0015f5de: blx 0x000fd80c          ; DrawString(font, str, colour, flags=0xF,
                                  ;   s0=xPos, s1=0.0f, s2=0.0f, s3=fontSize,
                                  ;   s4=160.0f, s5=0.0f, s6=0.0f, [sp+0]=0)
```

`vldr.32 s1,[pc,#-0x7c]` target: PC=0x0015f5ba, aligned=0x0015f5b8. 0x0015f5b8-0x7c = 0x0015f53c = 0.0f.
CONFIRMED: yPos = 0.0f for case 0/3.

**Diff vs port**: Line 566 `Vec3 descPos(xPos, 0.0f, 0.0f)` -- CORRECT.
**Recommendation: KEEP.**

---

### Description text position -- cases 1/2

**Shared structure for both states (0x0015f4a6-0x0015f4f6 and 0x0015f544-0x0015f58a)**:

State dispatch happens at 0x0015f3e2-0x0015f3f6: the binary first checks IsLocked() and
purchaseState before entering the cost/played-mode draw path. The condition to enter
the red/green path at 0x0015f3fa is: IsLocked()==false AND state != 0 AND state != 3.
If IsLocked()==true OR state==0 OR state==3, the code jumps to 0x0015f6de (normal desc path).

**Case 1 setup (0x0015f40a-0x0015f446)**:
- State==1 path at 0x0015f414.
- Calls `blx 0x00103458` (IsDeviceUpsideDown?): selects GETSTRING(0xC3 or 0xC2).
- 0x0015f446: `vmov.f32 s17,s16` -- **s17 = s16 = descFontSize** (for line 2).

**Case 2 setup (0x0015f44c-0x0015f460)**:
- State==2 path at 0x0015f44e.
- 0x0015f45c: `vldr.32 s15,[pc,#0xd8]` -- target: PC=0x0015f460, 0x0015f460+0xd8=0x0015f538=0.9f.
- 0x0015f460: `vmul.f32 s17,s16,s15` -- **s17 = descFontSize * 0.9f** (for line 2).

Both paths then merge at 0x0015f498 and share:

**First line draw (0x0015f4b0-0x0015f4f6)**:
```
0015f4b0: ldr r0,[r5,#0x58]          ; m_pShopScreen
0015f4b2: blx 0x00101484              ; GetDescriptionTextXPos() -> s0
0015f4b6: vmov.f32 s18,s0             ; s18 = xPos
...
0015f4ca: vldr.32 s15,[pc,#0x5c]     ; s15 = DAT at 0x0015f4cc+0x5c=0x0015f528=0.8f
0015f4d2: vmul.f32 s3,s16,s15         ; s3 = descFontSize * 0.8f  (line 1 font size)
...
0015f4e6: vmov.f32 s1,0xc1a00000      ; s1 = -20.0f  (yPos for line 1)
0015f4ea: vmov.f32 s0,s18             ; s0 = xPos
0015f4f6: blx 0x000fd80c              ; DrawString(font, costStr, colour, flags=3,
                                      ;   s0=xPos, s1=-20.0f, s2=0.0f,
                                      ;   s3=fontSize*0.8, s4=160.0f, ...)
```

**Second line draw (0x0015f544-0x0015f58a)**:
```
0015f544: ldr r0,[r5,#0x58]          ; m_pShopScreen
0015f546: blx 0x00101484              ; GetDescriptionTextXPos() -- SECOND call
0015f54a: vmov.f32 s16,s0             ; s16 = xPos2 (OVERWRITES descFontSize in s16!)
...
0015f564: vldr.32 s15,[pc,#-0x30]    ; s15 = 0x0015f568-0x30=0x0015f538=0.9f
0015f56c: vmul.f32 s3,s17,s15         ; s3 = s17 * 0.9f  (line 2 font size)
...
0015f57a: vmov.f32 s1,0x41200000      ; s1 = +10.0f  (yPos for line 2)
0015f57e: vmov.f32 s0,s16             ; s0 = xPos2 (from second GetDescriptionTextXPos)
0015f58a: blx 0x000fd80c              ; DrawString(font, descStr, whiteColour, flags=0xF,
                                      ;   s0=xPos2, s1=+10.0f, s2=0.0f,
                                      ;   s3=s17*0.9, s4=160.0f, ...)
```

**CRITICAL: line 2 font size differs between state 1 and state 2:**
- State 1: s17 = descFontSize (set at 0x0015f446), so line2_scale = descFontSize * 0.9f.
- State 2: s17 = descFontSize * 0.9f (set at 0x0015f460), so line2_scale = descFontSize * 0.81f.

The prior spec incorrectly stated both cases use `descFontSize * 0.9f` for line 2.

**ALSO: GetDescriptionTextXPos is called TWICE for states 1/2** -- once at 0x0015f4b2
(result in s18 for line 1) and again at 0x0015f546 (result in s16 for line 2). In normal
usage both calls return the same value (since the ShopScreen panel doesn't animate between
two consecutive draws). The port calls it once and reuses the result -- this is a harmless
deviation in practice but differs from the binary.

**C++ pseudocode (states 1 and 2)**:
```cpp
// State 1:
float xPos1 = m_pShopScreen->GetDescriptionTextXPos();
float line1Scale = descFontSize * 0.8f;
float line2Scale = descFontSize * 0.9f;       // state 1: s17=descFontSize, *0.9

// State 2:
float xPos1 = m_pShopScreen->GetDescriptionTextXPos();
float line1Scale = descFontSize * 0.8f;
float line2Scale = descFontSize * 0.81f;      // state 2: s17=descFontSize*0.9, *0.9 = *0.81

// First line draw (both states):
DrawString(font, costStr, colour, flags=3,
           xPos1, -20.0f, 0.0f, line1Scale, 160.0f, ...);

// Second line draw (both states):
float xPos2 = m_pShopScreen->GetDescriptionTextXPos();  // second call
DrawString(font, descStr, whiteAlpha, flags=0xF,
           xPos2, +10.0f, 0.0f, line2Scale, 160.0f, ...);
```

**Diff vs port (lines 578-601)**:
```cpp
// PORT -- state 1 (wrong: uses descFontSize * 0.9f for both states):
font->DrawStringSized(descFontSize * 0.9f, 0.0f, 0.0f, descStr, descPos2, ...);

// BINARY -- state 1 (correct):
font->DrawStringSized(descFontSize * 0.9f, 0.0f, 0.0f, descStr, descPos2, ...); // ok
// BINARY -- state 2 (correct):
font->DrawStringSized(descFontSize * 0.81f, 0.0f, 0.0f, descStr, descPos2, ...);
// note: 0.81 = 0.9 * 0.9 -- the binary multiplies descFontSize*0.9 by 0.9 again
```

**Y positions**: -20.0f (0xC1A00000) and +10.0f (0x41200000) confirmed by `vmov.f32` literals
at 0x0015f4e6 and 0x0015f57a respectively. These are NOT relative to pos.y.

**Recommendation: CHANGE state 2 line 2 font scale from 0.9f to 0.81f (= 0.9f * 0.9f).**
Y positions (0.0f, -20.0f, +10.0f) and xPos usage are CORRECT in the current port.

---

### Divider position -- re-confirmed after user fix

The current port (commit after user fix) uses:
```cpp
float halfRowH = m_Height * 0.5f;
matDiv.GlobalTranslate44(pos.x, pos.y + halfRowH, 0.0f);   // divider 1
matDiv2.GlobalTranslate44(pos.x, pos.y - halfRowH, 0.0f);  // divider 2
```

From the binary at 0x0015f1c4-0x0015f1f0:
- r9 = this+0x24 = &m_Height.
- `blx 0x001028b8` = Vec3 * scalar (yAxisUnit * m_Height).
- `vstr.32 s15,[sp,#0x39c]` s15=2.0f, then `blx 0x000f42fc` = Vec3 / 2.0f.
- Result = yAxisUnit * m_Height / 2 = (0, m_Height/2, 0).
- Divider 1: `operator+(result, &this->pos, half)` = pos + (0, halfH, 0) = (pos.x, pos.y+halfH, 0).
- Divider 2: `operator-(result, &this->pos, half)` = pos - (0, halfH, 0) = (pos.x, pos.y-halfH, 0).

Port matches. **KEEP.**

---

## Position re-verification round 2 -- final summary

| Element | Port currently (post round-1 fixes) | Binary (correct) | Status |
|---------|--------------------------------------|------------------|--------|
| local_d0 base | `pos + m_Size = (pos.x+60, pos.y+13, z)` | Same | CORRECT -- keep |
| Title shadow pos | `local_d0 + (4,-4,0)` | Same | CORRECT -- keep |
| Title fill pos | `local_d0` | Same | CORRECT -- keep |
| local_d0.y decrement | `-= 26.0f` | Same | CORRECT -- keep |
| Badge X/Y | `(local_d0.x - fVar27*titleScale - 4, 34 + local_d0.y)` | Same | CORRECT -- keep |
| Badge translate snapping | No snap | vcvt int-snap on X, Y, Z before GlobalTranslate44 | WRONG -- change |
| Selected ring X | `local_d0.x - cachedCostW - 32.0f` | Same | CORRECT -- keep |
| Selected ring Y | `local_d0.y` | Same | CORRECT -- keep |
| Selected ring X snapping | No snap | vcvt int-snap on X only before GlobalTranslate44 | WRONG -- change |
| descText case 0/3: Y | `0.0f` | `0.0f` (DAT_0015f53c) | CORRECT -- keep |
| descText case 1: line 1 scale | `descFontSize * 0.8f` | Same | CORRECT -- keep |
| descText case 1: line 1 yPos | `-20.0f` | `-20.0f` (0xC1A00000) | CORRECT -- keep |
| descText case 1: line 2 scale | `descFontSize * 0.9f` | `descFontSize * 0.9f` | CORRECT -- keep |
| descText case 1: line 2 yPos | `+10.0f` | `+10.0f` (0x41200000) | CORRECT -- keep |
| descText case 2: line 2 scale | `descFontSize * 0.9f` | `descFontSize * 0.81f` (= 0.9*0.9) | WRONG -- change |
| descText case 2: line 2 yPos | `+10.0f` | `+10.0f` | CORRECT -- keep |
| Divider 1 translate | `(pos.x, pos.y + m_Height/2, 0)` | Same | CORRECT -- keep |
| Divider 2 translate | `(pos.x, pos.y - m_Height/2, 0)` | Same | CORRECT -- keep |

Two bugs found: (1) missing integer pixel-snap on badge and selected ring translate
coordinates; (2) state 2 second-line font scale should be `descFontSize * 0.81f`,
not `descFontSize * 0.9f`. All other positions are correct.
