<!-- Analysed: 2026-04-26T12:00 -->

## Open TODO (2026-04-26)

- **Cannot scroll to bottom of list.** Drag works for upper portion but the list refuses to reach `maxOff = m_TotalHeight - m_Height` (= 1120 for shop). Likely the snap-step or a Phase 7 invariant is clamping max-offset early. May relate to closest-item snap pulling offset back toward near-zero. Investigate the relationship between `m_SnapDist` (now signed) and the bottom-of-list edge case.

# ScrollingMenu::Update — Scroll Math Deep Dive

Binary: `ScrollingMenu::Update @ 0x0015b744` (377 lines Ghidra decompile)

---

## 1. Coordinate Convention Table

| Axis | World unit | Screen direction | Touch convention |
|------|-----------|-----------------|-----------------|
| World X | +160 = top of landscape screen, -160 = bottom | vertical | touch.x = game X (from raw portrait-Y) |
| World Y | -240 = left of landscape screen, +240 = right | horizontal | touch.y = game Y = `319 - raw.x * 320/480` |
| **Scroll** | Y axis only | horizontal on device | **swipe physical UP → raw.x decreases → game touch.y INCREASES** |

### Touch Y direction (grounded in `GlesForm::TransformTouchPos`)

```
Physical UP on landscape device = portrait-LEFT = raw.x near 0
game.y = 319 - raw.x * (320/480)
Physical UP → raw.x small → game.y large (≈319)
Physical DOWN → raw.x near 480 → game.y small (≈-1)
```

**Swipe UP (finger moves up on landscape device) → `currTouchY` INCREASES.**

---

## 2. Field Name Map (Update-relevant)

| Binary field | Offset | Port name | Semantics |
|---|---|---|---|
| `field_0xd8` | +0xd8 | `m_Velocity.y` | TRUE scroll offset (items positioned relative to this) |
| `field_0x88` | +0x88 | `m_AnchorOffset.y` | scroll offset latched at finger-down |
| `field_0x7c` | +0x7c | `m_TouchAnchorPos.y` | touch Y latched at finger-down |
| `field_0x94` | +0x94 | `m_PendingVelocity.y` | drag-computed pending velocity, friction-decayed |
| `field_0x9c` / `field59_0x9c` | +0x9c | `m_Width` | init 320.0f (DAT_0015b468) |
| `field60_0xa0` | +0xa0 | `m_Height` | init 240.0f (DAT_0015b46c) |
| `field62_0xa8` | +0xa8 | `m_TotalHeight` | sum of item heights, default 0.0f |
| `field77_0xc0` | +0xc0 | `m_DragTargetIdx` | -1 = no target; >= 0 = index of drag target |

---

## 3. Phase 3B (Drag) — Binary Formula vs Port Formula

### Binary (lines ~216-220 of decompile)

```c
*(float*)&this->field_0x94 =
    (field_0xd8                                           // current scroll offset
     - (field_0x88                                        // anchor scroll (at press)
        - (touch_slots[m_TouchId].y - field_0x7c)))      // touch delta since press
    * -0.5f;
```

Expanding:
```
new_pending = (current_scroll - anchor_scroll + (currTouchY - anchorTouchY)) * -0.5
```

### Port (ScrollingMenu.cpp line 263)

```c
float newOffset = (anchorScrollY - (currentY - anchorY)) * DRAG_DELTA_FACTOR;
// = (anchor_scroll - touch_delta) * (-0.5)
```

### Sign mismatch table

| State | Binary result | Port result |
|-------|--------------|-------------|
| offset=0, swipe up (delta>0) | `(0 - 0 + delta) * -0.5 = -delta*0.5` (negative) | `(0 - delta)*(-0.5) = +delta*0.5` (positive) |
| offset=-500, swipe up (delta>0) | `(-500 - 0 + delta)*-0.5 = 250 - delta*0.5` | `(0 - delta)*(-0.5) = +delta*0.5` |

**At offset=0, the port's sign is OPPOSITE to the binary.** When the user swipes up (delta > 0), the binary drives pending.y NEGATIVE; the port drives it POSITIVE.

### Root cause of the bug

The binary formula includes the CURRENT scroll offset (`field_0xd8`) in the computation. This means `m_PendingVelocity.y` = the ABSOLUTE target offset (not a delta). The entire `(current_scroll - anchor_scroll)` term carries the scroll position; the touch delta modulates it. The port only uses the touch delta term and ignores the scroll position.

Additionally: the port assigns `newOffset` to `m_PendingVelocity.y` and then tries to ADD it to `m_Velocity.y` in Phase 4. But the binary assigns to `field_0x94` (PendingVelocity) which is then ACCUMULATED into `field_0xd4` (Velocity). Because the binary's formula already contains the absolute target, the velocity integration in Phase 4 (with 0.9 friction decay) causes it to smoothly converge on the target offset — it is NOT a delta/impulse, it is a SET-THEN-CONVERGE pattern.

---

## 4. Phase 4 — Velocity Integration

Binary:
```c
Vec3Scale_ScrollMenu(&field_0x90);           // field_0x90 (PendingVelocity) *= 0.9
_Stack_5c = field_0x90 * 1.0 (identity);    // copy
field_0xd4 += _Stack_5c;                    // m_Velocity += friction_decay(pending)
_Stack_68 = field_0xd4 - pos;              // layout cursor = new_velocity - pos
```

**Critical:** `_Stack_68` is NOT `m_PendingVelocity - pos`. It is `m_Velocity - pos` (using the UPDATED velocity after adding pending). `_Stack_68.y` is the starting Y for the item layout cursor.

---

## 5. Phase 5 — Item Layout Loop

### Loop initialization

```c
_Stack_68.y = m_Velocity.y - pos.y   // = scroll_offset - menu_center_Y
rangeTop = -160.0f   (DAT_0015be04 = 0xc3200000, default)
rangeBot = +160.0f   (DAT_0015be08 = 0x43200000, default)
```

If `m_bConstrainedView`:
```c
rangeBot = pos.y           // bottom of constrained window
rangeTop = pos.y - m_Height  // top of constrained window (smaller Y = further up)
```

### Per-item layout (cursor starts at `_Stack_68.y = scroll_off - pos.y`)

```c
for each item i:
    halfH = item->GetHeight() * 0.5f       // e.g. 40.0f for 80-unit items
    _Stack_68.y -= halfH                    // move to item center
    item->Move(_Stack_68)                   // position item at cursor
    // SetOnscreen test:
    if (rangeBot < _Stack_68.y - halfH   // item's top edge past bottom of viewport
     || _Stack_68.y + halfH < rangeTop)  // item's bottom edge past top of viewport
        SetOnscreen(false)
    else
        SetOnscreen(true)
    _Stack_68.y -= halfH                    // advance cursor to next item top
```

### ARM idiom decoded

`if ((int)((uint)(_Stack_68.y + halfH < rangeTop) << 0x1f) < 0)` fires when `_Stack_68.y + halfH < rangeTop`.

**onscreen=false when:**
- `_Stack_68.y - halfH > rangeBot` (item bottom is BELOW the viewport bottom, i.e., > +160)
- OR `_Stack_68.y + halfH < rangeTop` (item top is ABOVE the viewport top, i.e., < -160)

### Visible window semantics

- `rangeBot = +160.0f` = world Y coordinate of the viewport's **lower** edge (more positive = further along the positive Y axis = physically leftward in landscape, which means... toward the LEFT edge of the landscape screen). Wait — this needs recalibration.

Given the coordinate system: Y = -240 = LEFT of landscape, Y = +240 = RIGHT. The visible window is `[-160, +160]`. This is a 320-unit wide window centered at Y=0. Items outside `[-160, +160]` are off-screen.

- `rangeTop = -160.0f` = LEFT side of the viewport
- `rangeBot = +160.0f` = RIGHT side of the viewport

**No up/down semantics — the scroll is LEFT-RIGHT on the landscape screen.** Items that exceed +160 (too far right) or go below -160 (too far left) are offscreen.

### Item layout diagram at various offsets

With `pos.y = 40.0f` (LIST_POS_Y from ShopScreen), items of height 80 each:

| scroll_off (`m_Velocity.y`) | item[0].y | item[1].y | item[2].y | visible range |
|---|---|---|---|---|
| 0.0 | (0 - 40) - 40 = **-80** | -80 - 80 = **-160** | -160 - 80 = **-240** | [-160, +160] — item[0] and item[1] fully visible |
| +80.0 | (80 - 40) - 40 = **0** | 0 - 80 = **-80** | -160 | items shifted +80 (rightward) |
| -80.0 | (-80 - 40) - 40 = **-160** | -160 - 80 = **-240** (off) | -320 (off) | only item[0] partially visible |

**At offset=0:** item[0] is near center-left, item[1] is at the left edge (-160). Items are laid out from the center toward the LEFT as index increases.

**Positive offset = items shift RIGHT (toward +Y) = reveals items to the LEFT of the current view.**

So the VALID scroll range:
- `offset = 0` = TOP of list visible (item[0] near center-left, item[1] at left edge)
- `offset = totalScrollH = m_Height - m_TotalHeight` = BOTTOM of list visible

Wait: `totalScrollH = m_Height - m_TotalHeight`. With m_Height=240, m_TotalHeight=17*80=1360: `totalScrollH = 240 - 1360 = -1120`. This is NEGATIVE.

So valid range is `[totalScrollH, 0] = [-1120, 0]`.

- `offset = 0` = **top of list** (item[0] near viewport center, list starts from there)
- `offset = -1120` = **bottom of list** (last item near viewport center)

The Phase 7 spring fires when `offset > 0` (past top = spring toward 0) or `offset < totalScrollH` = `offset < -1120` (past bottom = spring toward -1120).

---

## 6. Phase 7 — Bounds and Spring (Corrected)

Binary (lines 348-377):

```c
float offset = m_Velocity.y;
if (offset <= 0 || m_DragTargetIdx >= 0) {
    float totalScrollH = m_Height - m_TotalHeight;   // e.g. 240 - 1360 = -1120
    if (offset >= totalScrollH || m_DragTargetIdx >= 0) {
        // IN-BOUNDS: still in valid range [-1120, 0]
        if (m_TouchId != -1) return;    // finger down, skip spring
        // Snap step: nudge toward closest item
        m_Velocity.y = offset + snapDist * 0.1f;
        return;
    }
    // PAST BOTTOM (offset < -1120): spring toward -1120
    m_Velocity.y = offset + (totalScrollH - offset) * 0.25f;
} else {
    // PAST TOP (offset > 0): spring toward 0
    m_Velocity.y = offset * 0.75f;
}
```

**Invariant:** Valid scroll range = `[m_Height - m_TotalHeight, 0]`. At startup `offset=0` = top. `offset > 0` = past top (spring fires immediately, pulling back to 0 with factor 0.75).

---

## 7. The Port's Drag Bug — Root Cause and Fix

### Why the binary's formula APPEARS to have factor -0.5 but acts as a target setter

The binary sets `m_PendingVelocity.y` to:
```
(current_scroll - anchor_scroll + touch_delta) * -0.5
```

This is NOT a velocity impulse. It is an absolute target attractor: each frame during drag, `m_Velocity.y` converges toward the target via the 0.9 friction decay on pending. The term `current_scroll - anchor_scroll` grows over time as the accumulated scroll moves away from the anchor, stabilizing the computation.

### Why -0.5 with the port's formula inverts the scroll

Port formula: `(anchor_scroll - touch_delta) * (-0.5) = (touch_delta - anchor_scroll) * 0.5`

At offset=0: `touch_delta * 0.5`. Swipe up → delta > 0 → positive pending → velocity.y grows positive → Phase 7 springs back (offset > 0 is "past top"). The content immediately bounces back. This matches exactly the observed bug.

### Why +0.5 makes content disappear

With +0.5 as factor: `(anchor_scroll - touch_delta) * 0.5 = anchor_scroll*0.5 - touch_delta*0.5`. At offset=0: `-touch_delta * 0.5`. Swipe up → delta > 0 → negative pending → velocity.y decreases → items shift more negative Y → items move past the LEFT edge of the viewport → all items get `SetOnscreen(false)` → content disappears.

This is the second observed bug.

### Correct port formula

The port must replicate the binary formula exactly:

```cpp
// ScrollingMenu.cpp Phase 3B
// Binary: field_0x94 = (field_0xd8 - (field_0x88 - (curr_y - anchor_y))) * -0.5
// Where field_0xd8 = m_Velocity.y (current), field_0x88 = m_AnchorOffset.y
float touchDelta = currentY - anchorY;  // currTouchY - anchorTouchY
m_PendingVelocity.y = (m_Velocity.y - (anchorScrollY - touchDelta)) * -0.5f;
// Equivalent: = (m_Velocity.y - anchorScrollY + touchDelta) * -0.5f
```

**File:** `src/hud/ScrollingMenu.cpp`, line 263 (currently):
```cpp
float newOffset = (anchorScrollY - (currentY - anchorY)) * DRAG_DELTA_FACTOR;
```

**Must become:**
```cpp
float newOffset = (m_Velocity.y - (anchorScrollY - (currentY - anchorY))) * -0.5f;
```

(Remove `DRAG_DELTA_FACTOR` constant — it no longer applies. The -0.5f is baked in directly.)

### Also fix Phase 4 velocity integration

Binary Phase 4:
```c
Vec3Scale_ScrollMenu(&field_0x90);       // pending *= 0.9
field_0xd4 += field_0x90;               // velocity += pending (after scale)
_Stack_68 = field_0xd4 - pos;           // layout cursor = velocity - pos
```

The port currently (lines 302-310) applies friction THEN adds to velocity THEN clears pending to zero. Clearing pending to zero after integration is WRONG — the binary does NOT clear pending after integration. The friction decay (×0.9 each frame) causes the pending to converge toward zero naturally over ~20 frames. The port must NOT zero out pending after integrating.

**File:** `src/hud/ScrollingMenu.cpp`, lines 301-311:

Remove:
```cpp
m_PendingVelocity = Vec3(0.0f, 0.0f, 0.0f);  // DELETE THIS LINE
```

The pending velocity should retain its value across frames and decay naturally via the 0.9 friction.

### Phase 5 layout cursor initialization

Binary: `_Stack_68 = m_Velocity - pos` (after friction + integration).

Port (line 325): `float curY = m_Velocity.y;`

This is missing `- pos.y`. The cursor should start at `m_Velocity.y - pos.y`, not `m_Velocity.y`.

**File:** `src/hud/ScrollingMenu.cpp`, line 325:

Current:
```cpp
float curY = m_Velocity.y;
```

Must be:
```cpp
float curY = m_Velocity.y - pos.y;
```

---

## 8. Summary of All Required Port Fixes

| # | File | Line (approx) | Current code | Fix |
|---|------|---------------|-------------|-----|
| 1 | `ScrollingMenu.cpp` | ~263 | `(anchorScrollY - (currentY - anchorY)) * DRAG_DELTA_FACTOR` | `(m_Velocity.y - (anchorScrollY - (currentY - anchorY))) * -0.5f` |
| 2 | `ScrollingMenu.cpp` | ~310 | `m_PendingVelocity = Vec3(0,0,0)` | DELETE — pending must NOT be zeroed (it friction-decays naturally) |
| 3 | `ScrollingMenu.cpp` | ~325 | `float curY = m_Velocity.y` | `float curY = m_Velocity.y - pos.y` |

---

## 9. Runtime Assertion

To catch future drift, add in `ScrollingMenu::Update` at the end of Phase 7 (after spring/snap):

```cpp
// ASSERT: valid scroll range at rest
// offset=0 = top of list; offset = m_Height - m_TotalHeight = bottom
float lo = m_Height - m_TotalHeight;
float hi = 0.0f;
// When not dragging and not springing, offset should be in [lo, hi]
// Allow small overshoot during spring animation
assert(m_TouchId != -1 || m_Velocity.y <= hi + 1.0f);
assert(m_TouchId != -1 || m_Velocity.y >= lo - 1.0f);
```

---

## 10. Touch Y-Axis Sign Convention — Root Cause of Scroll Direction Bug

### Full pipeline trace (binary)

The `slot.y` value used by `ScrollingMenu::Update` Phase 3B (`slot[+0xa4]`) is produced by this chain:

```
rawPortraitX  (Bada portrait X, 0..479 — physical left=0, right=479)
  |
  v  GlesForm::TransformTouchPos (0x0018327c)
  |    pixel_y = 319 - int(rawPortraitX * 320.0f / 480.0f)
  |    range: rawPortraitX=0 → pixel_y=319; rawPortraitX=479 → pixel_y≈0
  |
  v  GlesForm::OnTouchPressed (0x0018334c)
  |    __UpdateInternal(touchId, true, float(pixel_x), float(pixel_y), 0.0f)
  |
  v  Mortar::Touch::___UpdateInternal (0x00195314)
  |    states2[slot].field9_0xc = (int)pixel_y
  |
  v  SendIndividualTouchCallbacks
  |    AxisEvent(axis = 0xa9+N, value = float(pixel_y))
  |
  v  PointerMoveCallback (0x0016a4b4)
       s15 = float(pixel_y)
       s14 = 320.0f                  (window height at runtime)
       s15 = s15 + (-0.5f * s14)    (vmla: pixel_y - 160)
       s15 = -s15                    (vneg: 160 - pixel_y)
       slot[slot_idx*12 + 0xa4] = s15
```

**Formula: `slot.y = 160.0f - pixel_y`**

### Concrete values

| Device gesture | rawPortraitX | pixel_y | slot.y |
|---------------|-------------|---------|--------|
| Landscape TOP (portrait left) | 0 | 319 | -159 ≈ **-160** |
| Landscape CENTER | ~240 | ~159 | **0** |
| Landscape BOTTOM (portrait right) | 479 | ≈0 | **+160** |

Swipe UP on the landscape device (finger moves from bottom to top):
- rawPortraitX DECREASES
- pixel_y INCREASES (toward 319)
- slot.y DECREASES (toward -160)

**Binary convention: TOP of landscape screen = slot.y ≈ -160 (negative). Swipe UP → slot.y decreases.**

### Window size confirmation

`PointerMoveCallback` calls `DisplayManager::GetWindowSize` (vtable +0x30) which returns `m_WindowRect`. The `DisplayManagerBada` constructor calls `InitRect800_Engine` setting the default to `{0, 0, 800, 480}`, but `SetWindowSize` is called at runtime to set `{0, 0, 480, 320}`. Only `height=320` produces the correct world X range `[-160, +160]` for slot.y. Runtime window height = 320 confirmed.

### Port comparison

Port `SDLInputTranslator.cpp` line 83 (and line 89 in `TransformTouchNormalized`):
```cpp
gy = (float)(FN_SCREEN_H / 2) - ny * (float)FN_SCREEN_H;
```
At landscape TOP: `ny≈0` → `gy = +160` (POSITIVE)
At landscape BOTTOM: `ny≈1` → `gy = -160` (NEGATIVE)

**Port convention: TOP = +160 (positive). Swipe UP → gy INCREASES. This is OPPOSITE to the binary.**

### Binary vs port comparison table

| Location | Landscape TOP | Landscape BOTTOM | Swipe UP |
|----------|-------------|-----------------|----------|
| Binary `slot.y` | -160 | +160 | DECREASES |
| Port `gy` | +160 | -160 | INCREASES |

The sign is inverted. This is the root cause of all scroll direction issues.

### The single surgical fix

**File:** `src/platform/SDLInputTranslator.cpp`

**Line 83** (TouchDown/TouchMove handler) and **line 89** (TransformTouchNormalized):

Current:
```cpp
gy = (float)(FN_SCREEN_H / 2) - ny * (float)FN_SCREEN_H;
```

Must become:
```cpp
gy = ny * (float)FN_SCREEN_H - (float)(FN_SCREEN_H / 2);
```

This makes TOP = -160 (matches binary), BOTTOM = +160 (matches binary). Swipe UP → gy decreases (matches binary).

**This fix is at the SDL→Mortar input boundary — a single change location fixes the convention for ALL systems consuming `slot.y` (ScrollingMenu, SlashEntity, etc.).**

### Interaction with Phase 3B/4/5 fixes

This sign fix is necessary but not sufficient to fix the scroll by itself. The three Phase 3B/4/5 fixes in Section 8 of this document must also be applied together:

1. Phase 3B formula (includes current scroll offset, binary-exact)
2. Phase 4 remove zero-clear of pending velocity
3. Phase 5 layout cursor initialized to `m_Velocity.y - pos.y`

With BOTH the sign fix (SDLInputTranslator.cpp) AND the three ScrollingMenu fixes applied, the scroll direction and bounce behavior should match the binary exactly.

---

## 11. Constants Resolved

| DAT address | Hex bytes (LE) | Float value | Name in Update |
|---|---|---|---|
| `DAT_0015ba10` | `00 24 74 49` | ~762880.0f | initial min-dist sentinel |
| `DAT_0015ba14` | `00 00 00 00` | 0.0f | initial fVar18 in snap loop (unused in port) |
| `DAT_0015ba18` | `66 66 66 3f` | 0.9f | friction decay factor |
| `DAT_0015ba1c` | `cd cc 4c bd` | -0.05f | fling velocity lower gate |
| `DAT_0015ba20` | `cd cc 4c 3d` | +0.05f | fling velocity upper gate |
| `DAT_0015ba2c` | `00 40 1c 46` | ~10000.0f | initial distance sentinel (snap-to-item) |
| `DAT_0015be00` | `6f 12 83 3a` | ~0.001f | drag detection threshold |
| `DAT_0015be04` | `00 00 20 c3` | -160.0f | rangeTop (default visible region top/left) |
| `DAT_0015be08` | `00 00 20 43` | +160.0f | rangeBot (default visible region bottom/right) |
| `DAT_0015be0c` | `00 00 00 00` | 0.0f | fVar18 initial in Phase 5 (snap dist acc) |
| `DAT_0015be10` | `00 40 1c 46` | ~10000.0f | CLOSEST_SENTINEL for Phase 5 |
| `DAT_0015be14` | `cd cc cc bd` | -0.1f | VEL_NEAR_ZERO_LO |
| `DAT_0015be20` | `cd cc cc 3d` | +0.1f | VEL_NEAR_ZERO_HI (also snap step factor) |

---

## Scroll-bottom-limit + snap behavior (2026-04-26T12:00)

### 12.1 Field-offset table corrections (+0x9c .. +0xac)

The existing field-name map in Section 2 had field+0xa0 described as "init 240.0f". This was wrong. Binary
disassembly of the three setter functions corrects all four offsets:

| Offset | Field tag | Binary setter | Setter address | Binary instruction | Port name | Port constructor default |
|--------|-----------|---------------|----------------|--------------------|-----------|--------------------------|
| +0x9c  | field59   | SetItemHeight | 0x001479d4     | `vstr.32 s0,[r0,#0x9c]` | m_ItemHeight | 0.0f |
| +0xa0  | field60   | SetHeight     | 0x00147998     | `vstr.32 s0,[r0,#0xa0]` | m_Height    | 320.0f (constructor) |
| +0xa4  | field61   | SetWidth      | 0x001479a0     | writes field+0xa4 etc.  | m_Width     | 0.0f |
| +0xa8  | field62   | (AddItem acc) | 0x00147b04     | item GetHeight() accumulated into [r4,#0xa8] | m_TotalHeight | 0.0f |

The binary **runtime** value of m_Height for the shop list is **80.0f**, set by ShopScreen::Init, not the
constructor default.  See Section 12.2 below.

### 12.2 Diagnosis: why last 2 shop items are unreachable in the port

**Root cause: `CreateShopList` never calls `SetHeight(80.0f)`.**

Binary ShopScreen::Init @ 0x0015f7ac performs the following calls on the freshly created ScrollingMenu object:

```
0x0015f820  ldr  r3,[r3,#0x4c]       ; vtable_ptr[0x4c/4 = 19] = ScrollingMenu::SetHeight
            blx  r3                   ; SetHeight(80.0f)   -- arg in s0 from DAT_0015f9c8
```

SetHeight (0x00147998) executes `vstr.32 s0,[r0,#0xa0]`, so field+0xa0 (m_Height) = **80.0f** at runtime.

The shop list has 17 items, each 80.0f tall (SetItemHeight also passes 80.0f).  The Phase 7 spring-back
boundary is computed as:

```
totalScrollH = m_Height - m_TotalHeight          ; field+0xa0 - field+0xa8
             = 80.0f - (17 * 80.0f)
             = 80.0f - 1360.0f = -1280.0f
```

Valid scroll range for velocity.y is [totalScrollH, 0] = [-1280, 0].

Snap targets are `velocity.y = -i * itemHeight` for i = 0..16:
- Item 14: -1120.0f  (inside range)
- Item 15: -1200.0f  (inside range)
- Item 16: -1280.0f  = totalScrollH (exactly at boundary -- reachable)

**Port behavior with m_Height = 240.0f (constructor default, never overridden):**

```
totalScrollH = 240.0f - 1360.0f = -1120.0f
```

Valid range [-1120, 0].  Snap targets for items 15 and 16 are -1200 and -1280, both < -1120.  Phase 7
spring-back clamps velocity.y to totalScrollH = -1120 every frame, and the snap never reaches those items.
Exactly **2 items are perpetually unreachable**, matching the observed symptom.

**Fix (src/screens/ShopScreen.cpp, CreateShopList):**

After `m_pShopList = new ScrollingMenu()` (or equivalent construction), add:

```cpp
m_pShopList->SetHeight(80.0f);   // binary ShopScreen::Init vtable_ptr[19]=SetHeight(80.0f) @ 0x0015f820
```

This is the only change needed to restore the correct bottom-limit.

### 12.3 Phase 5 snap-distance formula (binary citations)

Phase 5 iterates items to find the closest snap target.  The critical expression is at ARM 0x0015bcf6:

```
0x0015bc5a  vsub.f32  s15,s15,s14   ; cursor = pos.y - velocity.y
            ...
0x0015bcde  vldr.32   s16,[r4,#0xd8]; s16 = velocity.y  (field+0xd8 = m_Velocity.y)
0x0015bce2  vldr.32   s15,[r2,#0x0] ; s15 = curY (cursor Y for item i)
0x0015bcf6  vsub.f32  s16,s15,s14   ; s16 = curY - pos.y
```

Here s14 = pos.y (loaded earlier), s15 = curY for the current item.
So **s16 = curY - pos.y**.

With cursor = pos.y - velocity.y, and curY_i = cursor - i * itemHeight:

```
s16 = curY_i - pos.y
    = (pos.y - velocity.y - i*itemH) - pos.y
    = -velocity.y - i*itemH
```

This is the signed distance from the snap target `-i*itemH` to the current velocity:

```
snapDist_i = -velocity.y - i*itemH   (negative when scrolled toward item i)
```

**Port bug at ScrollingMenu.cpp line 359:**

```cpp
// WRONG (current port):
m_SnapDist = curY - (pos.y - m_Velocity.y);
// evaluates to: curY - pos.y + velocity.y
//             = (pos.y - vel - i*itemH) - pos.y + vel  =  -i*itemH
// Missing: -velocity.y term -- snap always acts as if velocity=0
```

```cpp
// CORRECT (matches binary s16 = curY - pos.y):
m_SnapDist = curY - pos.y;
// evaluates to: (pos.y - vel - i*itemH) - pos.y  =  -vel - i*itemH
```

### 12.4 Phase 7 spring-back and snap-step formulas (binary citations)

**Snap gate: which field is checked for "velocity small enough to snap"?**

Binary at 0x0015bddc:

```
0x0015bddc  vldr.32   s14,[r4,#0x94]   ; s14 = field+0x94 = m_PendingVelocity.y
0x0015bde0  vldr.32   s15,[r5,#0x0]    ; s15 = VEL_NEAR_ZERO_HI (+0.1f, DAT_0015be20)
0x0015bde4  vcmpe.f32 s14,s15           ; compare s14 vs s15
            ...                          ; branch structure: snap fires when |m_PendingVelocity.y| < 0.1
```

The gate reads **field+0x94 = m_PendingVelocity.y**, NOT m_Velocity.y (field+0xd8).

**Port bug at ScrollingMenu.cpp line 439:**

```cpp
// WRONG (current port):
float vel = m_Velocity.y;               // reads field+0xd8
// CORRECT (binary 0x0015bddc reads [r4,#0x94]):
float vel = m_PendingVelocity.y;        // reads field+0x94
```

Consequence: when the user releases after a long drag, m_Velocity.y may be -80 (one full item height) while
m_PendingVelocity.y is ~0.05 (the tail of the drag gesture).  The binary fires snap because 0.05 < 0.1.
The port does not fire snap because 80 >> 0.1.  Snap is never triggered in the port.

**Snap step formula:**

Binary at 0x0015be2c (Phase 7 snap step, after gate passes):

```
0x0015be2c  vldr.32   s14,[r4,#0x94]   ; s14 = m_PendingVelocity.y
0x0015be30  vldr.32   s16,[r5,#0x0]    ; s16 = VEL_NEAR_ZERO_HI = +0.1f
0x0015be34  vmla.f32  s14,s16,s15      ; s14 = s14 + s16*s15  (VMLA: acc + src1*src2)
                                        ;      = m_PendingVelocity.y + 0.1f * snapDist
0x0015be38  vstr.32   s14,[r4,#0xd8]   ; store result into m_Velocity.y (field+0xd8)
```

Where s15 = snapDist = the chosen closest-item snap distance (s16 from Phase 5 stored to stack).

So the snap step is:

```
m_Velocity.y  =  m_PendingVelocity.y  +  0.1f * snapDist
```

With snapDist = -pending_vel - i*itemH (the corrected formula):

```
m_Velocity.y  =  pending_vel  +  0.1f * (-pending_vel - i*itemH)
             =  pending_vel * 0.9  -  0.1f * i * itemH
```

Fixed point (pending_vel steady-state): velocity converges to `-i*itemH` over successive frames.  The snap
step is a single-frame move; it does not loop -- convergence depends on Phase 4 friction decay continuing
to call this branch each frame until |pending_vel| >= 0.1 no longer holds.

**Port line 443 is structurally correct for the step formula:**

```cpp
m_Velocity.y = offset + snapDist * VEL_NEAR_ZERO_HI;
// offset = m_PendingVelocity.y, VEL_NEAR_ZERO_HI = 0.1f
// structurally matches binary VMLA
// wrong only because snapDist was computed incorrectly (see 12.3)
```

### 12.5 Summary: three port edits required

| # | File | Location | Current | Correct | Binary citation |
|---|------|----------|---------|---------|-----------------|
| 1 | `src/screens/ShopScreen.cpp` | `CreateShopList()`, after `new ScrollingMenu()` | (call missing) | `m_pShopList->SetHeight(80.0f);` | ShopScreen::Init 0x0015f820, vtable_ptr[19]=SetHeight |
| 2 | `src/hud/ScrollingMenu.cpp` | Phase 5 snap-distance, ~line 359 | `curY - (pos.y - m_Velocity.y)` | `curY - pos.y` | ARM 0x0015bcf6: `vsub.f32 s16,s15,s14` where s14=pos.y |
| 3 | `src/hud/ScrollingMenu.cpp` | Phase 7 snap gate, ~line 439 | `float vel = m_Velocity.y` | `float vel = m_PendingVelocity.y` | ARM 0x0015bddc: `vldr.32 s14,[r4,#0x94]` (field+0x94) |

Fix 1 alone restores the correct totalScrollH = -1280 and makes all 17 items reachable.
Fix 2 alone corrects the snap target computation (snap converges to correct item).
Fix 3 alone ensures the snap gate fires at the right moment (pending not velocity).
All three are independent and should be applied together.
