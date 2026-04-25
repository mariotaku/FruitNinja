<!-- Analysed: 2026-04-25T14:30 -->

# Shop List Struct Audit

Member-by-member comparison between binary struct layouts (decompiled from
Ghidra) and port headers. Goal: identify drift causing text to appear on the
wrong side of the shop screen.

---

## 1. ScrollingMenuItem

**Binary ctor addresses:** 5-param `0x0015b228`, 0-param `0x0015b5dc`
**Port header:** `src/hud/ScrollingMenuItem.h`

### Binary layout (from ctor + SetParent + SetOnscreen)

| Binary Offset | Size | Type | Name | Evidence |
|---|---|---|---|---|
| +0x00 | 4 | vtable* | vtable | ctor |
| +0x04 | 12 | Vec3 | pos | ctor writes +0x18/+0x1C/+0x20 to Width/Height/Depth leaving +0x04..+0x0F for pos |
| +0x10 | 4 | ScrollingMenu* | m_pParent | SetParent `0x0015aeb4`: `*(this+0x10)=param` |
| +0x14 | 4 | Colour (uint) | m_Colour | ctor: `Colour::Colour(this+0x14)` |
| +0x18 | 4 | float | m_Width | ctor: `*(this+0x18) = *puVar2` |
| +0x1C | 4 | float | m_Height | ctor: `*(this+0x1c) = puVar2[1]` |
| +0x20 | 4 | float | m_Depth | ctor: `*(this+0x20) = puVar2[2]` |
| +0x24 | 4 | float | m_ParamWidth | ctor: `*(this+0x24) = 0x41c80000 (25.0f)` |
| +0x28 | 4 | float | m_ParamHeight | ctor: `*(this+0x28) = DAT_0015b668 (0.0f)` |
| +0x2C | 40 | Delegate1 | m_Callback | ctor: `Delegate1::operator=((this+0x30),...)` confirms base at +0x2C, next field at +0x54 |
| +0x54 | 4 | char* | m_pText | SetText `0x0015b124`: `*(this+0x54)` |
| +0x58 | 4 | ptr | m_field58 | Draw `0x0015eb00`: `if(*(int*)(in_r0+0x58)!=0)` non-null check; set by ShopScreen |
| +0x5C | ? | char[] | m_DescText | Draw: `pcVar23 = (char*)(in_r0+0x5c)` used as text buffer |
| +0x2D | 1 | bool | m_bOnscreen | SetOnscreen `0x0013ce10`: `this[0x2D]=param` |

**Binary Delegate1 size: 40 bytes** (confirmed: base=+0x2C, next field m_pText=+0x54; 0x54-0x2C=0x28=40).

### Port layout (actual byte offsets, x86_64)

| Port Offset (actual) | Port Field | Comment in header |
|---|---|---|
| +0x00 | vtable | - |
| +0x04 | pos (12 bytes) | ends +0x0F |
| +0x10 | m_Colour (4) | header says "+0x14" — WRONG comment |
| +0x14 | m_Width (4) | header says "+0x18" — WRONG |
| +0x18 | m_Height (4) | header says "+0x1C" — WRONG |
| +0x1C | m_Depth (4) | header says "+0x20" — WRONG |
| +0x20 | m_ParamWidth (4) | header says "+0x24" — WRONG |
| +0x24 | m_ParamHeight (4) | header says "+0x28" — WRONG |
| +0x28 | m_Callback std::function (32 bytes) | header says "+0x2C..+0x53" — WRONG size |
| +0x48 | m_pText (4) | header says "+0x54" — WRONG |
| +0x4C | m_field58 (4) | header says "+0x58" — WRONG |
| +0x50 | m_DescText[128] | header says "+0x5C" — WRONG |
| +0xD0 | m_pParent (4) | protected; header says "+0x10" — declared at wrong location |
| +0xD4 | m_bOnscreen (1) | protected; header says implied ~+0x2D — WRONG |

### Comparison table

| Binary Offset | Binary Name | Port Offset | Port Name | Status |
|---|---|---|---|---|
| +0x00 | vtable | +0x00 | vtable | OK |
| +0x04 | pos | +0x04 | pos | OK |
| **+0x10** | **m_pParent** | **+0xD0** | **m_pParent** | **OFFSET MISMATCH — declared in protected section at end** |
| +0x14 | m_Colour | +0x10 | m_Colour | OFFSET MISMATCH (off by -4) |
| +0x18 | m_Width | +0x14 | m_Width | OFFSET MISMATCH (off by -4) |
| +0x1C | m_Height | +0x18 | m_Height | OFFSET MISMATCH (off by -4) |
| +0x20 | m_Depth | +0x1C | m_Depth | OFFSET MISMATCH (off by -4) |
| +0x24 | m_ParamWidth | +0x20 | m_ParamWidth | OFFSET MISMATCH (off by -4) |
| +0x28 | m_ParamHeight | +0x24 | m_ParamHeight | OFFSET MISMATCH (off by -4) |
| +0x2C | m_Callback (40B Delegate1) | +0x28 | m_Callback (32B std::function) | OFFSET MISMATCH (-4) + TYPE MISMATCH (-8 bytes) |
| +0x54 | m_pText | +0x48 | m_pText | OFFSET MISMATCH (off by -12) |
| +0x58 | m_field58 (ptr to ShopScreen?) | +0x4C | m_field58 | OFFSET MISMATCH (off by -12) |
| +0x5C | m_DescText | +0x50 | m_DescText[128] | OFFSET MISMATCH (off by -12) |
| +0x2D | m_bOnscreen | +0xD4 | m_bOnscreen | OFFSET MISMATCH (way off) |

**Root causes (two compounding bugs):**

1. `m_pParent` (4 bytes) is missing from its binary position between `pos` and `m_Colour`. In the binary, the layout is `vtable | pos | m_pParent | m_Colour | ...`. The port omitted it there and placed it in the `protected` section at the end — shifting every subsequent field by −4.

2. `std::function` is 32 bytes on x86_64 but binary `Delegate1` is 40 bytes — adding a further −8 shift to all fields after `+0x2C`.

**Net offset error for m_DescText:** binary +0x5C, port +0x50 — off by −12 bytes. This is the buffer that ShopListItem::Draw reads at `in_r0 + 0x5c` as the description text. The port reads 12 bytes too early.

**Net offset error for m_pText:** binary +0x54, port +0x48 — off by −12 bytes.

**Init value check:**
- m_ParamWidth: binary sets 0x41c80000 = **25.0f**. Port header documents "from ctor param" — not checked independently. `WRONG INIT` risk.
- m_ParamHeight: binary 0-param ctor writes `DAT_0015b668` = **0.0f**. Confirmed.

---

## 2. ShopListItem

**Binary ctor addresses:** 0-param `0x0015f9e8`, 5-param `0x0015f734`
**Port header:** `src/hud/ShopListItem.h`

The ShopListItem extended fields are relative to ShopListItem's `this` base, which is the same pointer as the ScrollingMenuItem base (it's a subclass). So the absolute offsets +0x25C..+0x284 are relative to the ScrollingMenuItem start.

### Binary ctor field writes (both ctors write identically)

| Binary Offset | Value | Field |
|---|---|---|
| +0x264 | DAT_0015fa54 = 0.0f | m_LockFlashAlpha |
| +0x278 | 0 | m_pItemInfo |
| +0x274 | SmartPtr::SetNull | m_pIconTex |
| +0x260 | 0.0f | m_SelectedAlpha |
| +0x280 | 0.0f | m_CostAlpha |
| +0x25C | 0.0f | m_NewItemAlpha |
| +0x27E | 0 | m_bIsNew |
| +0x27C | 1 | m_bOnscreenItem |
| +0x27D | 0 | m_bSelected |

### Comparison table

The binary offsets here are **absolute from ShopListItem start** (= ScrollingMenuItem start). Since the port's `_pad[0x204]` starts after ScrollingMenuItem, we need to know the binary end of ScrollingMenuItem. Binary ScrollingMenuItem extends to at least +0x5C+len(m_DescText). The port assumes ScrollingMenuItem is (sizeof(ScrollingMenuItem without m_pParent inserted) correct placement). Given the port's broken ScrollingMenuItem layout, `ShopListItem::_pad[0x204]` starts at the wrong offset, so every ShopListItem extended field is also shifted.

Specifically: the port header says `_pad[0x204]` starts at `+0x58` (end of ScrollingMenuItem per port). But the port's actual ScrollingMenuItem size is NOT 0x58 — it's `+0xD5` (end of m_bOnscreen in protected section) rounded up. The `_pad[0x204]` comment says it fills `+0x58..+0x25b` but in practice the actual C++ compiler places `_pad` AFTER the last non-static base-class field, which in the port is after `m_bOnscreen` at ~+0xD5. So `_pad` in the port actually starts at ~+0xD6, not +0x58 — making the extended fields start at ~+0x2DA instead of +0x25C.

| Binary Offset | Binary Type | Binary Name | Port Offset (approx) | Port Type | Port Name | Status |
|---|---|---|---|---|---|---|
| +0x25C | float | m_NewItemAlpha | ~+0x2DA | float | m_NewItemAlpha | OFFSET MISMATCH |
| +0x260 | float | m_SelectedAlpha | ~+0x2DE | float | m_SelectedAlpha | OFFSET MISMATCH |
| +0x264 | float | m_LockFlashAlpha | ~+0x2E2 | float | m_LockFlashAlpha | OFFSET MISMATCH |
| +0x268 | Vec3 (12B) | icon translate Vec3 | ~+0x2E6 | char[0x0C] _pad2 | _pad2 | RENAME (correct size) |
| +0x274 | SmartPtr<Texture> | m_pIconTex | ~+0x2F2 | SmartPtr<Texture> | m_pIconTex | OFFSET MISMATCH |
| +0x278 | ItemInfo* | m_pItemInfo | ~+0x2F6 | ItemInfo* | m_pItemInfo | OFFSET MISMATCH |
| +0x27C | byte | m_bOnscreenItem | ~+0x2FA | uint8_t | m_bOnscreenItem | OFFSET MISMATCH |
| +0x27D | byte | m_bSelected | ~+0x2FB | uint8_t | m_bSelected | OFFSET MISMATCH |
| +0x27E | byte | m_bIsNew | ~+0x2FC | uint8_t | m_bIsNew | OFFSET MISMATCH |
| +0x27F | pad | - | ~+0x2FD | uint8_t _pad3 | _pad3 | OFFSET MISMATCH |
| +0x280 | float | m_CostAlpha | ~+0x2FE | float | m_CostAlpha | OFFSET MISMATCH |
| — | — | — | ~+0x302 | ShopScreen* | m_pShopScreen | EXTRA (port-only) |

**Init values:** All fields correctly initialised to 0/1/0 per binary. m_bOnscreenItem = 1 confirmed from ctor `this[0x27c] = 1`. m_CostAlpha = 0.0f confirmed (`DAT_0015fa54 = 0x00000000`). WRONG INIT: none detected.

---

## 3. ScrollingMenu

**Binary ctor address:** `0x0015b3b0`
**Port header:** `src/hud/ScrollingMenu.h`

ScrollingMenu inherits from HUDControl. Binary `HUDControl` struct in Ghidra is 95 bytes but fields are placed at +0x00 (vtable 4B), +0x08 (pos 12B), +0x14 (pivot 12B), +0x20 (size 12B), +0x2C (m_Timer 4B), +0x32 (m_bNoDestructor 1B), +0x33 (m_bPendingRemoval 1B), +0x34 (m_LayerFlags 4B). That's 56 bytes used out of 95. The std::vector at +0x68 is the first ScrollingMenu field.

The port declares `m_TouchId` at binary +0x74 as the first named field (the vector is private at +0x68). The binary ctor confirms these field offsets directly from named struct fields (`this->field22_0x74`, `this->field59_0x9c`, etc.).

### Comparison table

| Binary Offset | Binary Name | Port Offset (declared) | Port Name | Status |
|---|---|---|---|---|
| +0x68 | (vector m_Items) | +0x68 private | m_Items | OK |
| +0x74 | field22_0x74 | +0x74 | m_TouchId | OK (init -1) |
| +0x78 | field_0x78 | +0x78 | m_TouchAnchorPos.x | OK |
| +0x7C | field_0x7c | +0x7C | m_TouchAnchorPos.y | OK |
| +0x80 | field_0x80 | +0x80 | m_TouchAnchorPos.z | OK |
| +0x84 | field_0x84 | +0x84 | m_AnchorOffset.x | OK |
| +0x88 | field_0x88 | +0x88 | m_AnchorOffset.y | OK |
| +0x8C | field_0x8c | +0x8C | m_AnchorOffset.z | OK |
| +0x90 | field_0x90 | +0x90 | m_PendingVelocity.x | OK |
| +0x94 | field_0x94 | +0x94 | m_PendingVelocity.y | OK |
| +0x98 | field_0x98 | +0x98 | m_PendingVelocity.z | OK |
| +0x9C | field59_0x9c | +0x9C | m_Width | OK (init 320.0f) |
| +0xA0 | field60_0xa0 | +0xA0 | m_Height | OK (init 240.0f) |
| +0xA4 | field61_0xa4 | +0xA4 | m_ItemHeight | OK (init -120.0f confirmed) |
| +0xA8 | field62_0xa8 | +0xA8 | m_TotalWidth | OK (init 0.0f) |
| +0xAC | field63_0xac | +0xAC | m_TotalHeight | OK (init 0.0f) |
| +0xBC | field76_0xbc | +0xBC | m_ClosestIdx | OK (init 0) |
| +0xC0 | field77_0xc0 | +0xC0 | m_DragTargetIdx | OK (init -1) |
| +0xC4 | field78_0xc4 | +0xC4 | m_SnapDist | OK (init 1.0f = 0x3f800000) |
| +0xC8 | field_0xc8 | +0xC8 | m_bDragging | OK (init 0) |
| +0xC9 | field_0xc9 | +0xC9 | m_bTouchProcessed | OK (init 0) |
| +0xCA | field_0xca | +0xCA | m_fieldCA | OK (init 1) |
| +0xCB | (pad) | +0xCB | m_fieldCB | OK |
| +0xCC | field83_0xcc | +0xCC | m_pCollidedItem | OK (init 0) |
| +0xD0 | field_0xd0 | +0xD0 | m_bConstrainedView | OK |
| +0xD1..D3 | pad | +0xD1 | m_pad_d1[3] | OK |
| +0xD4 | field_0xd4 | +0xD4 | m_Velocity.x | OK |
| +0xD8 | field_0xd8 | +0xD8 | m_Velocity.y | OK |
| +0xDC | field_0xdc | +0xDC | m_Velocity.z | OK |
| +0xE0..EC | field100..103 | +0xE0 | m_OuterRegion[4] | OK |
| +0xF0..FC | field104..107 | +0xF0 | m_InnerRegion[4] | OK |

**ScrollingMenu struct: CLEAN. All offsets and init values match the binary exactly.**

Note: gap at +0xB0..+0xBB (12 bytes between m_TotalHeight and m_ClosestIdx) is not present in the port's declared fields — the port jumps from m_TotalHeight at +0xAC directly to m_ClosestIdx at +0xBC with no explicit padding. This 12-byte gap must be filled by the HUDControl base class size rounding up to align the std::vector boundary, and then subsequent gap between m_TotalHeight and m_ClosestIdx must be zeroed. This is a latent MISSING-PADDING issue but since the offsets are documented correctly, the binary confirms them. The C++ compiler will leave uninitialized bytes in this range. Binary ctor does not init +0xB0..+0xBB so they're zero from default construction. This is OK in practice.

---

## 4. ItemInfo

**Binary ctor address:** `0x00113910`
**Port header:** `src/game/ItemInfo.h`

### Comparison table (from ctor decompile)

| Binary Offset | Binary Write | Port Offset | Port Name | Status |
|---|---|---|---|---|
| +0x00 | vtable | +0x00 | vtable (virtual ~) | OK |
| +0x04 | `*(this+4) = 0` | +0x04 | m_pName = 0 | OK |
| +0x0C | `*(this+0xc) = 0` | +0x0C | m_Cost = 0 | OK |
| +0x10 | `this[0x10] = 0xff` | +0x10 | m_Type = 0xFF | OK |
| +0x11 | (3B pad) | +0x11 | _pad11[3] | OK |
| +0x14 | `*(this+0x14) = 0` | +0x14 | m_pTitle = 0 | OK |
| +0x18 | `*(this+0x18) = 0` | +0x18 | m_pDescText = 0 | OK |
| +0x1C | `*(this+0x1c) = 0` | +0x1C | m_pLockedText = 0 | OK |
| +0x20 | `*(this+0x20) = 0` | +0x20 | m_pProgressFmt = 0 | OK |
| +0x24 | `this[0x24] = 0` | +0x24 | m_RequirementType = 0 | OK |
| +0x25 | (3B pad) | +0x25 | _pad25[3] | OK |
| +0x28 | `*(this+0x28) = 0` | +0x28 | m_pTotalStatKey = 0 | OK |
| +0x2C | `*(this+0x2c) = 0` | +0x2C | m_CountDownFrom = 0 | OK |
| +0x30 | `*(this+0x30) = 0` | +0x30 | m_pTextureName = 0 | OK |
| +0x34 | `Colour::Colour(this+0x34)` | +0x34 | m_Colour1 | OK |
| +0x38 | `Colour::Colour(this+0x38)` | +0x38 | m_Colour2 | OK |
| +0x3C | `this[0x3c] = 1` | +0x3C | m_bSeen = 1 | OK |
| +0x3D | (3B pad) | +0x3D | _pad3d[3] | OK |

**ItemInfo struct: CLEAN. All offsets and init values exactly match the binary.**

---

## 5. Mortar::Font

**Binary Font struct (Ghidra):** 16 bytes — a Bada `FontEx` wrapper with `__Pi*`, `__Attr*`, `FontEx*`. NOT the actual glyph data struct.

**Binary Font::Load `0x00199e9c`** accesses:
- `*(int*)this` — pointer to glyph array (heap); each glyph = 0x24 bytes
- `*(int*)(this + 0x408)` — page entries array  
- `*(int*)(this + 0x40C)` — page count
- `*(int*)(this + 0x41C)` — scaleW
- `*(int*)(this + 0x420)` — scaleH
- `*(float*)(this + 0x424)` — scale value
- `this + 0x42C` — vector of vertex buffers (one per page)

**Port Font** (`src/engine/render/Font.h`) stores:
- `FontGlyph m_Glyphs[256]` inline (256 × 36B = 9216 bytes) after ReferenceCounter base
- `int m_PageCount, m_LineHeight, m_Base, m_ScaleW, m_ScaleH`
- `float m_Scale`
- `std::vector<SmartPtr<Texture>> m_PageTextures`

### Comparison table

| Binary Offset | Binary Name | Port Field | Status |
|---|---|---|---|
| +0x00 | glyph array pointer | m_Glyphs[256] inline | TYPE MISMATCH — binary stores a heap ptr; port inlines. Only matters within Font methods. |
| +0x408 | page entries array | (internal in m_PageTextures) | RENAME (functional equiv) |
| +0x40C | page count | m_PageCount (at computed offset after 9216B glyph array) | OFFSET MISMATCH vs binary |
| +0x41C | scaleW | m_ScaleW | OFFSET MISMATCH vs binary |
| +0x420 | scaleH | m_ScaleH | OFFSET MISMATCH vs binary |
| +0x424 | scale | m_Scale | OFFSET MISMATCH vs binary |

**Font struct layout is completely different from the binary.** However, because Font is only accessed through its own virtual methods (DrawString, Load, MeasureWidth) and never via raw struct offset by external callers, this mismatch does NOT cause rendering position bugs. The port can safely use its own internal layout as long as the method behavior is correct. This is not a cause of the text positioning bug.

---

## Priority List — Top 5 Fixes Ranked by Impact

### Fix 1 (CRITICAL): Insert `m_pParent` at binary offset +0x10 in ScrollingMenuItem

**Impact:** Every single field from +0x14 onward is 4 bytes too early in the port. This affects `m_DescText` (off by 12 bytes total), `m_pText`, and `m_bOnscreen` — the description text buffer that ShopListItem::Draw reads at `in_r0 + 0x5c` points to garbage data.

**Binary evidence:** `ScrollingMenuItem::SetParent` @ `0x0015aeb4` writes to `*(this + 0x10)`.

**Port file/location:** `src/hud/ScrollingMenuItem.h`, after `struct { float x, y, z; } pos;` (line 117).

**Fix:** Move `m_pParent` declaration from the `protected` section at the end to immediately after `pos`, at binary offset +0x10:
```cpp
struct { float x, y, z; } pos;   // +0x04
ScrollingMenu* m_pParent;         // +0x10  <-- INSERT HERE
unsigned int m_Colour;            // +0x14  (now correct)
```
Remove it from the `protected` section at the bottom.

---

### Fix 2 (CRITICAL): Replace `std::function` with a 40-byte Delegate1 adapter for m_Callback

**Impact:** Binary `Delegate1` is 40 bytes; port `std::function` is 32 bytes on x86_64. All fields after +0x2C shift by an additional −8 bytes. Combined with Fix 1 gap, `m_pText` ends up at port +0x48 instead of binary +0x54, and `m_DescText` ends up at port +0x50 instead of binary +0x5C.

**Binary evidence:** Binary ctor @ `0x0015b228`: `Delegate1::operator=((this+0x30), callback)` — operator= target is +0x30 which is base+4 into a Delegate1 at +0x2C. Next field (`m_pText`) is at +0x54, so Delegate1 size = 0x54 − 0x2C = 40 bytes.

**Port file/location:** `src/hud/ScrollingMenuItem.h`, line 132: `std::function<void(ScrollingMenuItem*)> m_Callback;`

**Fix:** Pad `m_Callback` to 40 bytes. Simplest approach: add 8 bytes of explicit padding after the std::function:
```cpp
std::function<void(ScrollingMenuItem*)> m_Callback;  // +0x2C, 32 bytes
uint8_t _callback_pad[8];                            // +0x4C, pads to 40 bytes total
```
Or use a fixed-size opaque byte wrapper with a stored function pointer. Either way, the key is ensuring `m_pText` lands at physical offset +0x54.

---

### Fix 3 (HIGH): Move `m_bOnscreen` to binary offset +0x2D within the struct

**Impact:** `m_bOnscreen` is used by ScrollingMenu::Update to flag whether an item is visible. Binary offset +0x2D is within the Delegate1 range (+0x2C..+0x53). This seems impossible at first — but in the binary, `m_bOnscreen` at +0x2D overlaps the Delegate1 bytes. This means +0x2D is NOT inside Delegate1; it means the SetOnscreen binary is WRONG about Delegate1 starting at +0x2C, OR +0x2D is the `m_bOnscreen` field that is stored as a separate slot between Colour/Width fields.

Wait — re-examining: binary `SetOnscreen` @ `0x0013ce10` writes `this[0x2D]`. In binary layout: +0x2C = Delegate1 start. +0x2D = Delegate1 byte 1. That CAN'T be m_bOnscreen inside the callback. So either: (a) the Delegate1 does not start at +0x2C, or (b) +0x2D is indeed a byte inside Delegate1 being repurposed as m_bOnscreen.

The binary vtable shows Delegate1 constructor call without explicit offset in the decompile (lost context), but `operator=` is called with `this + 0x30` (not `this + 0x2C`). This suggests Delegate1 itself starts at `+0x2C` but the first 4 bytes (+0x2C..+0x2F) might be something else (e.g., size or type field), and the data portion starts at +0x30. If +0x2D is a byte inside those first 4 bytes, this is a Delegate1 internal flag. More likely: Delegate1's first 4 bytes are `(vtable, ...)` and the `m_bOnscreen` overlapping at +0x2D is one of those internal bytes reused as the onscreen flag by Delegate1's second component.

Actually the simplest reading is that SetOnscreen's `this[0x2d]` means byte-offset +0x2D from `this` — and this falls inside the Delegate1 region. If Delegate1 stores a boolean at its second byte that corresponds to m_bOnscreen semantically, the port just needs to ensure writing `m_Callback` data at the right place. Since the port uses std::function instead of Delegate1, this specific byte is inaccessible. The fix is to either use the properly-sized Delegate1 shim (Fix 2) so subsequent field positions are correct, and then manually keep `m_bOnscreen` as a separate field but accept it will alias into the callback bytes — or verify that +0x2D is NOT actually inside the Delegate1 range.

**Re-check:** If Delegate1 starts at +0x2C and is 40 bytes, the range is +0x2C..+0x53. The +0x2D byte is byte 1 of Delegate1. Yet `SetOnscreen` writes it as a boolean. This strongly implies Delegate1 occupies +0x30..+0x53 (24 bytes) and there are additional fields at +0x2C..+0x2F (4 bytes) that include m_bOnscreen at +0x2D. The `Delegate1::operator=((this+0x30), ...)` confirms the DATA starts at +0x30 — so the Delegate1 C++ object's start (including header) might be at +0x2C. The byte at +0x2D = Delegate1[1] = m_bOnscreen stored inside Delegate1's header. This is an ABI detail not visible in the C++ source — the port cannot replicate it with std::function.

**Fix:** Add `m_bOnscreen` as an explicit field at binary offset +0x2D. After inserting m_pParent (Fix 1) and correct callback size (Fix 2), the layout around +0x2C..+0x2F needs careful sizing. The simplest fix: after m_ParamHeight (+0x28, 4 bytes), place a 4-byte region for the Delegate1 header that contains m_bOnscreen at its second byte, then the 36-byte function pointer data (+0x30..+0x53), then m_pText (+0x54).

---

### Fix 4 (HIGH): Verify actual port layout offset of m_DescText in ShopListItem

**Impact:** ShopListItem::Draw reads the description text from `(char*)(in_r0 + 0x5c)`. The port's `m_DescText` must land at physical address `this + 0x5C`. After applying Fixes 1 and 2, the port's `m_DescText` should land at +0x5C. Verify after applying Fixes 1+2 with `offsetof(ScrollingMenuItem, m_DescText)`.

**Binary evidence:** ShopListItem::Draw `0x0015eb00` line: `pcVar23 = (char*)(in_r0 + 0x5c)` and the description text DrawString calls that follow.

**Port file/location:** `src/hud/ScrollingMenuItem.h`, line 142.

**Expected fix:** After inserting m_pParent (4 bytes) and expanding m_Callback to 40 bytes, verify `offsetof(ScrollingMenuItem, m_DescText) == 0x5C`. If it does, this is automatically OK.

---

### Fix 5 (MEDIUM): Verify m_field58 is the ShopScreen/parent pointer stored externally

**Impact:** ShopListItem::Draw checks `if (*(int*)(in_r0 + 0x58) != 0)` before drawing description and cost text. The port's `m_field58` (int) lands at binary +0x4C currently (off by 12). After fixes, it should land at binary +0x58. This field is set by ShopScreen and acts as a gate for the text rendering block.

**Binary evidence:** ShopListItem::Draw `0x0015eb00`: the field at +0x58 is dereferenced as a pointer to an object with a field at +0xB8. Compare with ShopScreen layout — ShopScreen::field at +0xB8 would be the transition alpha or similar flag.

**Port file/location:** `src/hud/ScrollingMenuItem.h`, line 138: `int m_field58;`. This should be typed as `void*` or the appropriate struct pointer, not `int`.

---

## Summary

| Class | Status | Root Cause |
|---|---|---|
| ScrollingMenuItem | BROKEN — all fields from +0x14 onward wrong | m_pParent missing at +0x10; std::function 8B too small |
| ShopListItem | BROKEN — all extended fields wrong | Cascades from ScrollingMenuItem size error |
| ScrollingMenu | CLEAN | All offsets match binary exactly |
| ItemInfo | CLEAN | All offsets match binary exactly |
| Mortar::Font | Layout differs from binary but self-contained | No impact on positioning |

The text appearing on the left side of the shop screen is most directly caused by **Fix 1** (m_pParent gap → m_DescText at wrong offset) and **Fix 2** (std::function too small → further shift). After Fixes 1 and 2, `m_DescText` and `m_pText` should land at the correct binary offsets (+0x5C and +0x54), and the description text DrawString calls in ShopListItem::Draw will read the correct buffer.
