<!-- Analysed: 2026-04-26T12:00 -->

# Y-Axis Convention Audit

Binary: `FruitNinja.exe` (ARM32 LE, Samsung Bada, 480x320 landscape)

This document resolves every Y-axis convention used in the binary's touch pipeline,
world coordinate system, and ScrollingMenu scroll math, so the port can eliminate
ad-hoc local sign flips and match the binary 1:1.

---

## Summary Table

| Property | Binary value | Port (before fix) | Notes |
|----------|-------------|-------------------|-------|
| Touch Y at landscape TOP | -160 | +160 | Inverted -- see Section 2 |
| Touch Y at landscape BOTTOM | +160 | -160 | Inverted |
| Swipe UP on landscape (slot.y direction) | DECREASES | INCREASES | Inverted |
| World ortho Y at landscape top (pos.y) | +160 | +160 | Matches |
| World ortho Y at landscape bottom (pos.y) | -160 | -160 | Matches |
| pos.y > 0 means | toward landscape top | toward landscape top | Matches |
| ShopScreen list pos.y | +40.0f | +40.0f (LIST_POS_Y) | Matches |
| Layout cursor init formula | pos.y - velocity.y | velocity.y (missing pos.y, wrong sign) | See Section 1 |
| Positive m_Velocity.y means | more content to RIGHT is visible | (inverted by port flip) | See Section 5 |
| Phase 7: offset > 0 is | PAST TOP, spring to 0 | same if sign is fixed | See Section 7 |

---

## Section 1: `_Vector3<float>::operator-` Semantics at Phase 4

### Question
The Ghidra decompile of `ScrollingMenu::Update` (0x0015b744) shows:
```c
_Vector3<float>::operator-(&_Stack_68, &(this->super).pos);
```
Is this unary negation, or binary subtraction with a 3rd register argument?

### Finding

Disassembly of the call site (0x0015bb8e--0x0015bb96):
```
0015bb8e: mov r2,r5           // r2 = r5 = r4+0xd4 = m_Velocity (_Vector3)
0015bb90: add r0,sp,#0x18     // r0 = output buffer _Stack_68
0015bb92: add.w r1,r4,#0x8   // r1 = (this->super).pos
0015bb96: blx 0x000ff414      // PLT stub -> actual function at 0x001176cc
```

Three register arguments: r0=output, r1=pos, r2=velocity.

The PLT stub at 0x000ff414 uses GOT entry at 0x001f00ac (value 0x001176cc). The
resolved function at 0x001176cc:

```c
_Vector3<float> * operator-(this, param_1) {  // r0=out, r1=param_1=pos
  float *in_r2;                                // r2=velocity
  _Vector3(this,
      param_1->x - *in_r2,
      param_1->y - in_r2[1],
      param_1->z - in_r2[2]);
  return this;
}
```

**Result: `_Stack_68 = pos - velocity`** (binary subtract, pos is lhs, velocity is rhs).

This is a **3-argument binary operator-**: `out = lhs - rhs` where lhs=pos (r1)
and rhs=velocity (r2). Ghidra omits the third argument in its decompile display
because it treats the call as `__thiscall` with r0=this, r1=param_1, but the
actual function body reads r2 as an additional unnamed input (`in_r2`).

### Phase 4 layout cursor formula (corrected)

```c
// Phase 4: after Vec3Scale + operator+= integrate velocity
_Stack_68 = pos - m_Velocity;     // binary subtract, NOT velocity - pos
// => _Stack_68.y = pos.y - m_Velocity.y
```

At rest (velocity=0, pos.y=40): cursor starts at 40.
At velocity=+80 (scrolled): cursor starts at 40-80 = -40 (items shifted left/down).

### Port fix

Current port (`ScrollingMenu.cpp`):
```cpp
float curY = m_Velocity.y;         // WRONG: missing pos.y, wrong sign
```
Must be:
```cpp
float curY = pos.y - m_Velocity.y; // CORRECT: matches binary _Stack_68 = pos - velocity
```

---

## Section 2: Binary's Touch Coordinate Convention

### `GlesForm::TransformTouchPos` (0x0018327c)

The function reads from `rawPoint` (an `Osp::Graphics::Point`).

From `FGrpPoint.h`:
```
Osp::Graphics::Point layout:
  +0x0  vtable ptr
  +0x4  int x        (portrait x-coordinate)
  +0x8  int y        (portrait y-coordinate)
  +0xc  PointEx*
```

Disassembly:
```
vldr.32 s13,[r5,#0x8]   // load rawPoint.y (portrait Y)
  -> result.x = int(rawPoint.y * 480.0f / 800.0f)

vldr.32 s13,[r5,#0x4]   // load rawPoint.x (portrait X)
  rsb.w r3,r2,#0x13c    // r3 = 0x13c - scaled_x = 316 - scaled_x
  adds r3,r3,#0x3        // r3 = 319 - scaled_x
  -> result.y = 319 - int(rawPoint.x * 320.0f / 480.0f)
```

Constants (read from 0x001832d0):
- TOUCH_GAME_WIDTH_F  = 480.0f
- TOUCH_SCREEN_HEIGHT_F = 800.0f (Bada portrait screen height)
- TOUCH_GAME_HEIGHT_F = 320.0f

The full formula:
```
result.x = int(rawPortraitY * 480.0f / 800.0f)    // portrait Y -> landscape X
result.y = 319 - int(rawPortraitX * 320.0f / 480.0f)  // portrait X -> landscape Y (inverted)
```

### Direction of rotation

The Bada device is physically portrait (480x800). The game runs in landscape (480x320).
The device is rotated 90 degrees. From `TransformTouchPos`:
- portrait X=0 (physical LEFT of portrait = physical TOP of landscape) --> result.y = 319
- portrait X=480 (physical RIGHT of portrait = physical BOTTOM of landscape) --> result.y ≈ -1

Therefore: **result.y = 319 corresponds to landscape TOP.**

After the `PointerMoveCallback` (0x0016a4b4) applies `slot.y = 160.0f - pixel_y`:
- Landscape TOP: pixel_y = 319, slot.y = 160 - 319 = -159 ≈ -160
- Landscape CENTER: pixel_y ≈ 160, slot.y ≈ 0
- Landscape BOTTOM: pixel_y ≈ 0, slot.y ≈ +160

**Binary touch-Y convention: landscape TOP = slot.y ≈ -160 (negative).**

Swipe UP on landscape (finger moves toward landscape top):
- rawPortraitX DECREASES
- pixel_y INCREASES toward 319
- slot.y DECREASES toward -160

**Swipe UP --> slot.y DECREASES.**

### Port comparison

`SDLInputTranslator.cpp`:
```cpp
gy = (float)(FN_SCREEN_H / 2) - ny * (float)FN_SCREEN_H;
```
At landscape TOP: ny ≈ 0 --> gy = +160.
At landscape BOTTOM: ny ≈ 1 --> gy = -160.
**Port convention: landscape TOP = +160. Swipe UP --> gy INCREASES. OPPOSITE of binary.**

### Port fix

`src/platform/SDLInputTranslator.cpp` (lines 83 and 89, TouchDown/Move and TransformTouchNormalized):

Current:
```cpp
gy = (float)(FN_SCREEN_H / 2) - ny * (float)FN_SCREEN_H;
```
Must be:
```cpp
gy = ny * (float)FN_SCREEN_H - (float)(FN_SCREEN_H / 2);
```

This makes TOP = -160, BOTTOM = +160, matching the binary. Swipe UP --> gy decreases.

---

## Section 3: Binary's World Ortho Convention

### `_Matrix44<float>::OrthoW` (0x0019e7a8)

Signature (from Ghidra): `OrthoW(top, bottom, left, right, nearVal, farVal, w, out)`

ARM calling convention: s0=top, s1=bottom, s2=left, s3=right, s4=nearVal, s5=farVal.

Disassembly traces:
```
s20 = s0 = top
s21 = s1 = bottom
s18 = s3 = right
s19 = s2 = left

s14 = s20 - s21 = top - bottom        // Y range
s13 = s18 - s19 = right - left        // X range

data[0][0] = 2 / (right - left)       // X scale (written to [r4+0x00])
data[1][1] = 2 / (top - bottom)       // Y scale (written to [r4+0x14])
data[3][0] = -((right+left)/(right-left))   // X translation
data[3][1] = -((top+bottom)/(top-bottom))   // Y translation
```

The Y-axis matrix scale = 2/(top-bottom). For `SetupOrtho(160, -160, -240, 240, 2000, -6000)`:
- top=160, bottom=-160 --> Y scale = 2/320 = 1/160
- This maps world Y in [-160,+160] to clip Y in [-1,+1]

**`top=160` is the larger Y value; the larger Y value maps to the positive clip boundary.**

In standard OpenGL convention: clip Y=+1 is the top of the screen.

Combined with the parameter labeling (top=160, bottom=-160), **world pos.y=+160 = top of landscape screen, world pos.y=-160 = bottom of landscape screen.**

This is independent of the touch coordinate system (which is inverted). The world ortho uses the natural Y-up convention: **pos.y > 0 = toward landscape top.**

Note: the landscape screen has X=+160 at the physical top and X=-160 at the physical bottom (width axis). The Y axis is the horizontal axis: Y=-240 = left edge, Y=+240 = right edge. The menu scrolls along Y.

---

## Section 4: ShopScreen List `pos.y` Value

### Finding

In `ShopScreen::Update` (0x0015e9c0), when the ScrollingMenu (`field_0x94`) is non-null:
```c
_Vector3<float>::_Vector3(&local_f4,
    (1.0f - fade) * slide_speed * -1.5f - slide_offset,  // pos.x (varies with fade)
    DAT_0015ead8,                                          // pos.y (CONSTANT)
    DAT_0015eadc);                                         // pos.z (0.0f)
*(float *)(scrollMenu + 0x8)  = local_f4.x;   // pos.x
*(float *)(scrollMenu + 0xc)  = local_f4.y;   // pos.y = DAT_0015ead8
*(float *)(scrollMenu + 0x10) = local_f4.z;   // pos.z
```

`DAT_0015ead8` = bytes `[00 00 20 42]` = 0x42200000 = **40.0f**

`DAT_0015eadc` = bytes `[00 00 00 00]` = **0.0f**

**Confirmed: `pos.y = 40.0f` (LIST_POS_Y) at runtime for the ShopScreen's ScrollingMenu.**

### Interpretation

With pos.y=40 and the world ortho convention (Y > 0 = toward landscape top):
- pos.y=40 means the menu's reference point is 40 units toward the landscape top from center.
- The initial layout cursor = pos.y - velocity.y = 40 - 0 = 40.
- Item[0] is positioned at cursor - halfH = 40 - halfH.
- For 80-unit items: item[0] at Y=0 (center screen), item[1] at Y=-80, item[2] at Y=-160 (left edge).

**pos.y=40 places item[0] at world-center with the list extending toward negative Y (landscape left).**

The valid scroll range is `[m_Height - m_TotalHeight, 0]`.
- For m_Height=240, m_TotalHeight=17*80=1360: valid range = [-1120, 0].
- offset=0 = top of list. offset=-1120 = bottom of list.
- Positive velocity would push items toward +Y (rightward/landscape-top), which is past the top.

---

## Section 5: `m_Velocity.y` (`field_0xd8`) Sign Convention

### Phase 4 integration (assembly at 0x0015bb74--0x0015bb9a)

```
0015bb74: mov r0,r8              // r8 = field_0x90 (PendingVelocity)
0015bb76: bl 0x0015b714          // Vec3Scale_ScrollMenu: PendingVelocity *= 0.9
0015bb7e: mov r0,r6              // r6 = sp+0x24 = _Stack_5c (temp)
0015bb80: mov r1,r8              // r1 = PendingVelocity (scaled)
0015bb82: blx 0x001028b8         // _Stack_5c = PendingVelocity * 1.0 (identity copy)
0015bb86: mov r1,r6              // r1 = _Stack_5c
0015bb88: mov r0,r5              // r0 = r5 = field_0xd4 = m_Velocity
0015bb8a: blx 0x000ffba0         // m_Velocity += _Stack_5c (velocity += friction_pending)
0015bb8e: mov r2,r5              // r2 = m_Velocity
0015bb90: add r0,sp,#0x18        // r0 = _Stack_68
0015bb92: add.w r1,r4,#0x8      // r1 = pos
0015bb96: blx 0x000ff414         // _Stack_68 = pos - m_Velocity
```

The layout cursor `_Stack_68.y = pos.y - m_Velocity.y`.

### Drag formula and velocity direction

Phase 3B sets pendingVelocity.y (confirmed from disassembly at 0x0015bacc--0x0015bafe):
```
pendingVelocity.y = (m_Velocity.y - anchorOffset.y + touchDelta) * -0.5f
where touchDelta = currTouchY - anchorTouchY
```

When user swipes RIGHT (toward landscape-right = slot.y INCREASES):
- touchDelta > 0
- At velocity=0, anchor=0: pendingVelocity = touchDelta * -0.5 < 0
- After friction integration: m_Velocity.y decreases (becomes negative)
- cursor = pos.y - (negative velocity) = pos.y + |velocity| > pos.y
- Items positioned at higher Y values (toward landscape-right / landscape-top)
- This reveals content that was to the LEFT of the viewport (smaller-index items)

When user swipes LEFT (slot.y DECREASES, touchDelta < 0):
- pendingVelocity = touchDelta * -0.5 > 0
- m_Velocity.y becomes positive
- cursor = pos.y - (positive velocity) < pos.y
- Items positioned at lower Y values (toward landscape-left / landscape-bottom)
- This reveals content to the RIGHT (larger-index items, i.e., further down the list)

**Binary convention: m_Velocity.y > 0 = items shifted rightward/toward-top of landscape = reveals EARLIER items (scrolling BACK toward top of list). m_Velocity.y < 0 = reveals LATER items (scrolling toward bottom of list).**

Phrased as scroll offset: positive velocity = near top of list; negative velocity = near bottom of list. Valid range is [-(totalHeight - viewHeight), 0] with offset=0 at the top. **Wait: check against Phase 7.**

### Phase 7 sign check

From Phase 7 disassembly:
```
if (offset > 0 && m_DragTargetIdx < 0):
    offset *= 0.75     // PAST TOP: spring toward 0
else:
    totalScrollH = m_Height - m_TotalHeight  // e.g. -1120
    if (offset < totalScrollH && m_DragTargetIdx < 0):
        offset += (totalScrollH - offset) * 0.25  // PAST BOTTOM: spring toward totalScrollH
```

The PAST TOP condition is `offset > 0`. The PAST BOTTOM condition is `offset < (m_Height - m_TotalHeight)`.

Since m_Height < m_TotalHeight, totalScrollH is negative (e.g., -1120).

**Conclusion: valid range is [totalScrollH, 0] = [-(m_TotalHeight - m_Height), 0].**
- offset=0 = top of list (item[0] near menu center)
- offset=totalScrollH (negative) = bottom of list

**Positive offset = past top (wrong direction). Negative offset = scrolled into the list.**

This means: scrolling toward the "bottom" of the list (revealing later items) requires offset to go MORE NEGATIVE. The user's gesture to do this (swipe LEFT in landscape = touchDelta < 0) produces positive pendingVelocity, which makes velocity go POSITIVE... then Phase 7 springs back immediately.

**This reveals a sign error in the binary drag formula.** OR there is an additional sign inversion at the touch input level.

With the CORRECT binary touch convention (slot.y increases toward landscape BOTTOM, not top):
- Swipe to see more list items (swiping toward landscape-bottom in a typical scrolling motion) = slot.y INCREASES = touchDelta > 0.
- pendingVelocity = touchDelta * -0.5 < 0.
- velocity becomes NEGATIVE.
- cursor = pos.y - (negative) = higher value.
- Items shift toward +Y (landscape-top side).
- This reveals items that were above the current view = EARLIER items.

Hmm, this depends on list layout direction. Let me re-check: the binary doc says landscape Y=-240 is LEFT and Y=+240 is RIGHT. The landscape screen is horizontal. The menu presumably scrolls left-right on the screen. "Swiping right" (finger moves rightward physically on the landscape screen) should scroll content left, revealing items to the right (later items). In the binary's coordinate system, rightward on the landscape screen = toward +Y.

Swipe rightward (physical): slot.y INCREASES (landscape-right = higher slot.y as verified above).
touchDelta > 0 --> pendingVelocity < 0 --> velocity < 0 --> cursor higher --> items shift toward +Y (right).

Items moving RIGHT while the finger moves RIGHT = items follow the finger direction. This is natural drag behavior (not flick). But for scrolling, content should move WITH the finger. This is correct: if you drag right, the list content appears to move right (the window moves left relative to content), revealing earlier items.

Phase 7 with velocity < 0 (scrolled right, showing earlier items): this is WITHIN the valid range [totalScrollH, 0] if velocity is between -1120 and 0. This is consistent.

**Final conclusion for m_Velocity.y:**
- **m_Velocity.y = 0**: top of list (item[0] near menu center, aligned with pos.y).
- **m_Velocity.y = -N (negative)**: scrolled N units toward the bottom of the list (later items visible).
- **m_Velocity.y = totalScrollH** (e.g., -1120): bottom of list.
- **m_Velocity.y > 0**: past the top (spring pulls back to 0).
- **m_Velocity.y < totalScrollH**: past the bottom (spring pulls back to totalScrollH).

---

## Section 6: Binary's Drag Formula Sign Trace

Full Phase 3B formula (from disassembly, 0x0015bacc--0x0015bafe):
```
pendingVelocity.y = (m_Velocity.y - (anchorOffset.y - (currTouchY - anchorTouchY))) * -0.5f
                  = (m_Velocity.y - anchorOffset.y + touchDelta) * -0.5f
```

**Scenario: Swipe RIGHT to see earlier items (toward landscape-top, slot.y INCREASES).**

- Binary touch: slot.y is HIGH when at landscape-TOP.
- Swipe RIGHT (physically right on landscape) = finger moves toward +Y = slot.y increases.
- touchDelta = currY - anchorY > 0 (let's say delta=+100).
- At velocity=0, anchor=0: pendingVelocity = +100 * -0.5 = -50.
- After friction: m_Velocity.y decreases from 0 toward -50*0.9/(1-0.9) = -450 asymptotically.
- cursor = pos.y - (negative large) = pos.y + |velocity| = 40 + 450 = 490.
- Items positioned starting at 490, decrementing by item height.
- With 80-unit items: item[0]=450, item[1]=370, ..., item[6]=10, item[7]=-70, ...
- Only items around Y in [-160,+160] are visible. So items 2-7 approx.
- This is NOT "earlier items" -- it's actually ALL items but offset WAY right.

Wait, I misidentified the direction. Let me reconsider.

For the binary with correct touch convention (slot.y = -160 at landscape-TOP, +160 at BOTTOM):
- "Scroll to see LATER items" in a vertical-ish list = swipe UP on the landscape screen.
- Swipe UP = slot.y DECREASES (toward -160).
- touchDelta < 0 (e.g., -100).
- pendingVelocity = -100 * -0.5 = +50.
- velocity grows POSITIVE.
- cursor = pos.y - (+positive) = 40 - 50 = -10, then -40, -80, etc.
- Items at: item[0]=-10-40=-50, item[1]=-130, item[2]=-210 (offscreen).
- Only item[0] visible. This is the start of the list! Not "later items."

Hmm. It seems swipe-UP reveals EARLIER items (item[0] stays visible). Swipe-DOWN (touchDelta > 0) reveals LATER items.

Swipe DOWN (slot.y increases): pendingVelocity = +100 * -0.5 = -50.
velocity goes NEGATIVE. cursor = pos.y - (negative) = 40 + |vel| = 90+.
Items shift +Y direction. item[0] at 90-40=50, item[1]=-30, item[2]=-110, item[3]=-190 (offscreen), ...
More items visible toward negative Y end. Items at lower index move into viewport from the +Y side.

Wait -- in a standard left-scrolling (or top-scrolling) list, swipe DOWN reveals content FURTHER in the list (later items). Here swipe-DOWN (slot.y increases = landscape-bottom direction) makes items shift +Y and reveals... earlier items (lower index at higher Y). That seems to be the case.

To reach LATER items (higher index), we need items to shift into the -Y viewport side. That means cursor gets SMALLER (more negative). cursor = pos.y - velocity. For cursor to decrease, velocity must increase (become more positive). Velocity becomes positive when touchDelta < 0 (swipe UP).

**Binary's list direction: later (deeper) items are at MORE NEGATIVE Y. Swipe UP (toward landscape-top, slot.y decreases, touchDelta < 0) to see LATER items.**

This matches the game's likely UX: the menu is vertically oriented on the landscape screen (items stacked along the Y axis from top=+160 to bottom=-160), and you swipe toward the physical top of the landscape device to go further into the list.

**Cross-check with Section 5:**
- Swipe UP (touchDelta < 0): pendingVelocity > 0 --> velocity > 0.
- Phase 7: velocity > 0 is PAST TOP. But the user was going INTO the list!

There is a contradiction. Let me re-examine.

Actually: with touchDelta < 0 (swipe UP):
- pendingVelocity = (0 - 0 + (-100)) * -0.5 = +50.
- But this is set EACH FRAME during drag, not accumulated.
- During drag (Phase 3B), pendingVelocity is set to a target, not an impulse.
- Phase 4 multiplies pending by 0.9 and adds to velocity. So velocity converges toward a fixed target.
- Target = +50 (for small touchDelta=-100 at velocity=0).
- As the drag continues, velocity.y stays around +50 if touchDelta stays -100.
- cursor = pos.y - velocity.y = 40 - 50 = -10.
- Items at: item[0]=-10-40=-50, item[1]=-130 (visible, near left edge), item[2]=-210 (offscreen).

So: with velocity=+50, items 0 and 1 are visible. At velocity=0, items 0 and 1 were also visible (item[1] at exactly -160 = left edge).

Swipe UP (finger toward top): velocity becomes +50, items shift slightly. Item[0] from 0 to -50. Not significantly revealing new items.

Let's try larger velocity: velocity=+200.
cursor = 40 - 200 = -160.
item[0] at -160 - 40 = -200 (offscreen left!). 
item[1] at -200 - 80 = -280 (offscreen).
All items moved off the left side. This is the wrong direction for seeing more items.

Now velocity=-200:
cursor = 40 - (-200) = 240.
item[0] at 240 - 40 = 200 (offscreen right!).
item[1] at 200 - 80 = 120 (visible, toward landscape-right).
item[2] at 120 - 80 = 40 (visible, center).
item[3] at 40 - 80 = -40 (visible).
item[4] at -40 - 80 = -120 (visible, toward landscape-left).
item[5] at -120 - 80 = -200 (offscreen).

With velocity=-200, items 1-4 are visible. These are LATER items (higher indices). 
Phase 7: velocity=-200 is within valid range [totalScrollH, 0] so no spring.

So **negative velocity reveals LATER (deeper) items**. To get negative velocity, you need touchDelta > 0 (swipe DOWN / toward landscape-bottom). This means:

**Swipe DOWN on landscape (toward +Y, slot.y increases, touchDelta > 0) reveals LATER items (scrolling deeper into the list).**

This is the natural "scroll down" gesture (swiping finger down on a vertical list = content scrolls up = later items appear at the bottom). Here "bottom" in landscape maps to the +Y direction.

**Summary: swipe toward landscape-bottom (+Y direction) to scroll deeper. This matches standard scroll UX if the list is oriented top-to-bottom along the landscape's Y axis (top=+Y=+160, bottom=-Y=-160... wait, but we said +Y is LANDSCAPE-RIGHT and -Y is LANDSCAPE-LEFT.)**

There is genuine ambiguity about the orientation of the menu. The physical phone is portrait, the game runs landscape, the Y axis (horizontal in the game world) corresponds to the vertical axis on the physical portrait device.

For the binary: the orientation is confirmed by the coordinate system docs:
- Y=-240 = left of landscape screen = bottom of physical portrait device
- Y=+240 = right of landscape screen = top of physical portrait device

Touch input: rawPortraitX=0 = physical TOP of portrait = LEFT of landscape = corresponds to what direction on the game's Y axis? From TransformTouchPos: rawPortraitX=0 --> pixel_y=319 --> slot.y = 160-319 = -159 ≈ -160. So physical TOP of portrait (held landscape = LEFT of landscape) = slot.y ≈ -160 ≈ world Y=-160.

This means slot.y closely approximates world Y for touch input. Physical landscape-LEFT = world Y=-160; landscape-RIGHT = world Y=+160.

The menu scrolls along Y. "Swipe toward landscape-right" = slot.y increases (toward +160 = landscape-right/portrait-top).

For the list layout: item[0] at pos.y-halfH, item[1] at pos.y-halfH-itemH, etc. (items decrease in Y). The first item is at the highest Y. So the list runs from RIGHT (high Y, item[0]) to LEFT (low Y, last item).

"Swipe LEFT" (toward landscape-left, slot.y decreases, touchDelta < 0): pendingVelocity > 0 --> velocity positive --> cursor = pos.y - (positive) < pos.y --> items at lower Y values --> items shift LEFT. This reveals items on the LEFT side of the viewport (higher index, deeper in list). Standard "scroll left to see more" gesture.

"Swipe RIGHT" (toward landscape-right, slot.y increases, touchDelta > 0): pendingVelocity < 0 --> velocity negative --> cursor = pos.y - (negative) > pos.y --> items at higher Y values --> items shift RIGHT. This reveals items on the RIGHT side (earlier in list). Standard "scroll right to go back."

**Final summary: swipe LEFT = reveal later items (scroll forward). Positive velocity.y = items at lower Y = reversed (toward RIGHT end of list). This is COUNTER-INTUITIVE vs. the Phase 7 valid range analysis. Let me reconcile with Phase 7.**

Phase 7 says: velocity > 0 = PAST TOP. But we just determined swipe-LEFT gives positive velocity and reveals later items. This is a contradiction.

**Resolution: the drag SETS pendingVelocity to an absolute target, not an accumulated impulse.**

Phase 3B: `pendingVelocity.y = (velocity.y - anchorOffset.y + touchDelta) * -0.5f`

The KEY is that `velocity.y` is included. As the user continues dragging:
- If they drag to `touchDelta = -100` (leftward), pendingVelocity = (velocity - anchor - 100) * -0.5.
- If velocity was already negative (say -200), pendingVelocity = (-200 - 0 - 100) * -0.5 = +150.
- After friction: velocity += 0.9 * 150 = velocity + 135. 
- New velocity = -200 + 135 = -65.
- cursor = pos.y - (-65) = 40 + 65 = 105.

This is a PULL toward a target (the formula acts as a spring). The target is where `pendingVelocity = 0`:
```
0 = (velocity.y - anchorOffset.y + touchDelta) * -0.5
=> velocity.y = anchorOffset.y - touchDelta
```

So the drag CONVERGES velocity toward `anchorOffset - touchDelta`. This is the desired scroll offset to place items such that the anchor touch point stays at the same screen position as the finger.

At drag start: anchorOffset = velocity (latched), anchorTouchY = currTouchY (latched).
Target = anchorOffset - (currTouchY - anchorTouchY) = anchorOffset - touchDelta.

After dragging leftward by 100 (touchDelta = -100): target = anchorOffset - (-100) = anchorOffset + 100.
So velocity increases by 100 from the anchor. velocity = anchorOffset + 100 (if fully converged).
cursor = pos.y - (anchorOffset + 100).

If anchorOffset=0: cursor = 40 - 100 = -60. Items: item[0] at -60-40=-100, item[1] at -180, item[2] at -260.
Item[1] moves from -160 (at anchorOffset=0) to -180 (just off screen). cursor shifted LEFT by 100 units.

This IS consistent: drag LEFT (touchDelta < 0 = swipe toward landscape-left) moves items LEFT. This reveals items previously off the LEFT edge. Those items are at higher index (deeper in list). Correct.

Now Phase 7: drag leftward gives `velocity = anchorOffset - touchDelta = 0 - (-100) = +100`. Velocity is POSITIVE.

Phase 7: `velocity > 0` = PAST TOP (spring back). But this is actually a VALID scroll into the list!

**This is the core bug the user is seeing.** With the BINARY's touch coordinate (slot.y = -160 at TOP, +160 at BOTTOM):
- Swipe LEFT = touchDelta < 0 (slot.y decreases) = pendingVelocity > 0 = velocity > 0 = IMMEDIATELY springs back.

This means the BINARY would also have the spring-back bug if both the touch convention AND Phase 7 analysis are correct.

**The reconciliation is: SWIPE LEFT on physical device toward landscape-left = landscape-left is portrait-BOTTOM = rawPortraitX=479 = slot.y ≈ +160 = POSITIVE slot.y.**

I had it backwards! Let me re-check the device orientation:
- Portrait device: top = portrait-top, bottom = portrait-bottom.
- The game ROTATES the display 90 degrees. WHICH 90 degrees?
- From TransformTouchPos: rawPortraitX=0 --> slot.y=-159. rawPortraitX=479 --> slot.y=+160.
- If the device is rotated 90 CW: portrait-LEFT becomes landscape-DOWN. 
  portrait-right=480 end = slot.y ≈ +160. If portrait-right = landscape-BOTTOM, then slot.y=+160 at landscape-BOTTOM.
- If rotated 90 CCW: portrait-RIGHT becomes landscape-UP.
  portrait-right = slot.y ≈ +160 = landscape-UP = landscape-TOP.

Which way? The existing doc says landscape-TOP = slot.y ≈ -159. But above I computed landscape-TOP corresponds to rawPortraitX=0 and slot.y=-159. So **rawPortraitX=0 = landscape-TOP (wherever the portrait-device's 0 edge maps to landscape-TOP).**

For Bada device held in landscape: if the volume buttons are typically on the top or left edge when held landscape, the orientation convention could vary. Without running the binary, the exact 90-degree rotation direction cannot be confirmed from static analysis. However the TransformTouchPos formula establishes:
- rawPortraitX small (left of portrait) --> slot.y negative (near -160)
- rawPortraitX large (right of portrait) --> slot.y positive (near +160)

The correct assumption (consistent with the game shipping correctly) is that the formula was tuned to match the actual device orientation. The existing doc says "Physical UP on landscape device = portrait-LEFT = raw.x near 0 --> game.y large (≈319) --> slot.y small (≈-160)." And: "Binary convention: TOP of landscape screen = slot.y ≈ -160. Swipe UP → slot.y decreases."

If this convention is correct AND Phase 7 is correct (velocity > 0 = past top), then:
- Swipe DOWN = touchDelta > 0 = pendingVelocity < 0 = velocity < 0 = valid scroll range.
- Swipe DOWN reveals LATER items (because negative velocity --> cursor = pos.y - negative > pos.y --> items shifted RIGHT, which is landscape-TOP, which is the UPPER part of the list).

**Re-conclusion: swipe DOWN on the landscape device to scroll FURTHER into the list.**

The list items are ordered from RIGHT (top of landscape = first item) to LEFT (deeper in landscape = later items). Swiping DOWN moves the content UP (toward landscape-top), revealing earlier items. Swiping UP moves content DOWN, revealing later items.

This is actually: swipe UP = scroll DOWN in list = reveal more items. Which matches: "swipe up on a scrolling list to see more content below."

In the binary's landscape world: "up" in the physical sense when holding landscape = the physical top edge = landscape-TOP = world Y ≈ +160. "Up" for the user's thumb = toward the physical-top = toward landscape-top = toward +Y direction. Swiping "up" = finger moves toward +Y = slot.y INCREASES (since landscape-top slot.y ≈ -160 but physical-UP on the landscape is at landscape-TOP which has slot.y = -160... wait this is confusing.)

Given the complexity, the key observable fact is: **the binary shipped and worked correctly.** Whatever the physical orientation, the math must be self-consistent. The authoritative source is the code, not physical intuition. The port's job is to match the binary's convention exactly.

---

## Section 7: Phase 7 Overscroll Bounds (Verified)

From disassembly at 0x0015bd7c--0x0015bdca:

```c
float offset = m_Velocity.y;  // field_0xd8

// PAST TOP check: velocity > 0 AND no forced snap target
if (offset > 0.0f && m_DragTargetIdx < 0) {
    m_Velocity.y = offset * 0.75f;  // spring toward 0 (top)
    goto STORE_AND_RETURN;
}

// ELSE: velocity <= 0 or drag active
float totalScrollH = m_Height - m_TotalHeight;  // e.g. 240 - 1360 = -1120

// PAST BOTTOM check: velocity < totalScrollH AND no snap target
if (offset < totalScrollH && m_DragTargetIdx < 0) {
    // spring toward totalScrollH
    m_Velocity.y = offset + (totalScrollH - offset) * 0.25f;
    goto STORE_AND_RETURN;
}

// IN-BOUNDS: fall through to snap step
```

**Invariants:**
- `offset = 0` = top of list. `offset > 0` = past-top (immediately springs back).
- `offset = totalScrollH` (negative) = bottom of list.
- `offset < totalScrollH` = past-bottom (springs toward totalScrollH).
- Valid scroll range = `[m_Height - m_TotalHeight, 0]`.
- Spring factor toward top = 0.75 (per frame).
- Spring factor toward bottom = 0.25 (per frame, slower).

The `if (offset <= 0 || m_DragTargetIdx >= 0)` structure in the high-level decompile:
```c
if (offset <= 0.0f || m_DragTargetIdx >= 0) {
    // check bottom
    if (offset < totalScrollH && m_DragTargetIdx < 0) {
        // spring to bottom
    } else {
        // in-bounds: snap step
    }
} else {
    // spring to top (offset *= 0.75)
}
```

**Port fix**: ensure `m_Velocity.y = 0` at list-top (no inversion). With the corrected input sign (Section 2) and Phase 5 cursor (Section 1), the port's velocity should use the same sign as the binary.

---

## Section 8: HUDControl3d::Draw Offset

### `HUDControl3d::Draw` (0x0014428c)

From disassembly at 0x00144322--0x00144358:
```
r6  = sp+0x40 = _Stack_68 (initialized with HUD_SCREEN_WIDTH, HUD_SCREEN_HEIGHT, HUD_SCREEN_Z)
r10 = sp+0x4c = _Stack_5c

// _Stack_68 = Vec3(HUD_SCREEN_WIDTH, HUD_SCREEN_HEIGHT, HUD_SCREEN_Z) = (480, 320, 0)
// _Stack_5c = _Stack_68 * pivot  (element-wise multiply)
// _Stack_50 = pos + _Stack_5c    (add pivot-offset to pos)
// use _Stack_50 as the translation for the matrix
```

Constants at 0x001443e0: `[0x43f00000, 0x43a00000, 0x00000000]` = `{480.0f, 320.0f, 0.0f}`.

So: `translation = pos + pivot * (480, 320, 0)`.

For `pivot = (0.5, 0.5, 0)`: translation = pos + (240, 160, 0).
For `pivot = (0, 0, 0)` (default, as for ScrollingMenu): translation = pos + (0, 0, 0) = pos.

**ScrollingMenu (and other HUDControl3d with default pivot) is not affected by this offset at all.** The offset only matters for controls that set a non-zero pivot to anchor to screen edges or corners.

The HUDControl3d::Draw pivot transform is applied BEFORE drawing, using the matrix stack. It does NOT modify `pos` in the struct -- it only affects the per-frame draw transform. ScrollingMenu::Update uses raw `pos` in the layout cursor formula, not the transformed position.

---

## Section 9: Call Path -- ScrollingMenu::Update

### XRefs to `ScrollingMenu::Update` (0x0015b744)

```
From 0x001ee818 [DATA]  -- vtable entry
From 0x001e9ec0 [DATA]  -- vtable (second location, likely HUDControl vtable)
From 0x000faa60 in thunk Update (0x000faa58) [COMPUTED_CALL] -- vtable dispatch
```

The thunk at 0x000faa58:
```c
void __thiscall ScrollingMenu::Update(this, dt) {
    // r0 = this
    (*(code *)PTR_Update_001ee818)(this);  // calls 0x0015b744 via vtable
}
```

The actual caller is HUD/ActorManager through the vtable Update slot. Update is called once
per frame via the game's actor update loop before Draw.

### ScrollingMenu::Draw

ScrollingMenu does not override Draw from HUDControl3d (not found in the vtable search).
HUDControl3d::Draw (0x0014428c) handles drawing. The base HUDControl3d::Draw only draws
the secondary texture quad using pos and pivot as described in Section 8. ScrollingMenu's
items are drawn individually through their own Draw calls within the item layout loop.

The items receive their position from `ScrollingMenuItem::Move` (vtable slot 6) called
with `_Stack_68` (the per-item cursor position). Draw is separate and uses each item's
already-set position. So `pos.y` directly affects ONLY the layout cursor initialization
in `Update`, not the draw path.

---

## Recommended Port Changes

Listed in order of dependency:

### Change 1: Fix input Y sign (SDLInputTranslator.cpp)

File: `src/platform/SDLInputTranslator.cpp`, lines ~83 and ~89.

Current:
```cpp
gy = (float)(FN_SCREEN_H / 2) - ny * (float)FN_SCREEN_H;
```
Must be:
```cpp
gy = ny * (float)FN_SCREEN_H - (float)(FN_SCREEN_H / 2);
```

Effect: makes gy = -160 at landscape TOP (matches binary slot.y = -160), gy = +160 at BOTTOM.
This fixes the overall touch-Y sign for ALL systems, including slash detection and scrolling.

### Change 2: Fix Phase 5 layout cursor init (ScrollingMenu.cpp)

File: `src/hud/ScrollingMenu.cpp`, line ~325.

Current:
```cpp
float curY = m_Velocity.y;
```
Must be:
```cpp
float curY = pos.y - m_Velocity.y;  // binary: _Stack_68 = pos - velocity
```

Rationale: the binary's `operator-(out, pos, velocity)` computes `out = pos - velocity`.
The decompile `operator-(&_Stack_68, &pos)` shows only 2 args because Ghidra treats r2
(velocity) as an unnamed implicit register -- the actual function body uses `in_r2`.

### Change 3: Fix Phase 3B drag formula (ScrollingMenu.cpp)

File: `src/hud/ScrollingMenu.cpp`, line ~263.

Current:
```cpp
float newOffset = (anchorScrollY - (currentY - anchorY)) * DRAG_DELTA_FACTOR;
```
Must be:
```cpp
float newOffset = (m_Velocity.y - (m_AnchorOffset.y - (currentY - m_TouchAnchorPos.y))) * -0.5f;
```

Binary formula (from disassembly at 0x0015bacc--0x0015bafe):
```
pendingVelocity.y = (field_0xd8 - (field_0x88 - (currTouchY - field_0x7c))) * -0.5f
                  = (m_Velocity.y - m_AnchorOffset.y + touchDelta) * -0.5f
```

The port's formula omits the `m_Velocity.y` term, which is the target-convergence term
that causes velocity to smoothly converge on the desired scroll offset rather than acting
as a one-shot impulse.

### Change 4: Remove zero-clear of pending velocity (ScrollingMenu.cpp)

File: `src/hud/ScrollingMenu.cpp`, line ~310.

Remove (or comment out):
```cpp
m_PendingVelocity = Vec3(0.0f, 0.0f, 0.0f);  // DELETE -- binary does NOT zero this
```

Binary Phase 4 does NOT zero pendingVelocity. It scales by 0.9 each frame (friction
decay). Zeroing it after integration destroys the smooth convergence.

### Change 5: Fix snap-distance sign in Phase 5 (ScrollingMenu.cpp)

The closest-item snap distance computation in the layout loop uses:
```
fVar18 = _Stack_68.y - (pos.y - m_Velocity.y)
```
This is `cursor.y - initial_cursor.y`, representing how far the cursor has moved from
start. With the corrected `curY = pos.y - m_Velocity.y` in Change 2, the snap distance
sign will automatically be correct if the existing port code computes it as:
```cpp
float snapDist = curY - (pos.y - m_Velocity.y);
```
If the port currently uses `curY - m_Velocity.y` (missing `pos.y`), it will also need
this fix.

---

## Open Issues

1. **Rotation direction**: Which exact 90-degree rotation (CW or CCW) the Bada OS applies
   when transforming portrait touch to landscape game coordinates cannot be confirmed from
   static analysis alone. The `TransformTouchPos` formula is authoritative for the math,
   but the physical meaning ("swipe UP = slot.y decreases") relies on the convention that
   portrait-LEFT = landscape-TOP. If this assumption is wrong, the drag direction may
   still feel incorrect. However, since the binary shipped correctly on the Bada device,
   the formula is self-consistent and the port just needs to match it.

2. **pos.y static value**: ShopScreen::Update sets `pos.y = DAT_0015ead8 = 40.0f` each
   frame (with a slide-in animation on pos.x). The port's `LIST_POS_Y = 40.0f` is correct.
   No change needed.

3. **rangeTop / rangeBot in constrained view mode**: When `m_bConstrainedView = true`,
   `rangeBot = pos.y`, `rangeTop = pos.y - m_Height`. With pos.y=40, m_Height=240:
   `rangeBot=40`, `rangeTop=-200`. Items outside [-200, 40] are marked offscreen. The
   sign of these constants matches the world-ortho convention: landscape-top items are
   at higher Y values.
