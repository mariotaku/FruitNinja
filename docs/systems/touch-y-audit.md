<!-- Analysed: 2026-04-26T15:00 -->

# Touch-Y Convention Audit

Binary: `FruitNinja.exe` (ARM32 LE, Samsung Bada, 480x320 landscape)

This document catalogues every port-side call site that reads a touch coordinate,
compares it against the binary's expectation, and specifies what (if anything) would
need to change if the port adopted the binary's Y-down touch convention.

Prior art referenced:
- `docs/systems/y-axis-convention.md` (established the two-convention design and
  recommended the SDLInputTranslator flip)
- `docs/screens/shop-scroll-debug.md` (Section 10 confirms slot.y = 160 - pixel_y;
  also notes the comment in `SDLInputTranslator.cpp` line 82-84 that the current
  discrepancy is handled locally by ScrollingMenu)

---

## Background: the two conventions

| Space | Port convention | Binary convention |
|-------|----------------|-------------------|
| Touch coord (slot.y / currY) | Y-UP: TOP = +160, BOTTOM = -160 | Y-DOWN: TOP = -160, BOTTOM = +160 |
| World ortho pos.y | Y-UP: TOP = +160, BOTTOM = -160 | Y-UP: TOP = +160, BOTTOM = -160 |

The binary applies a sign flip inside `GlesForm::TransformTouchPos` +
`PointerMoveCallback`, yielding `slot.y = 160 - pixel_y` (Section 2,
y-axis-convention.md). The port instead inverts in `SDLInputTranslator` to give
`gy = (FN_SCREEN_H/2) - ny * FN_SCREEN_H` = +160 at TOP, making touch coords
Y-UP -- the same sign as world coords.

This unified port convention was a deliberate simplification. The question this
document answers: what breaks (now or after migration) under each option?

---

## Table 1: every port-side touch consumer

| File:line | Function | Reads | Current port convention | Binary convention | Action needed |
|-----------|----------|-------|------------------------|-------------------|---------------|
| `src/platform/SDLInputTranslator.cpp:88` | `TransformTouch` | `gy` output from `(FN_SCREEN_H/2) - ny*FN_SCREEN_H` | Y-UP: TOP=+160 | Y-DOWN: TOP=-160 | Single sign flip: `gy = ny*FN_SCREEN_H - (FN_SCREEN_H/2)` -- **root of everything** |
| `src/platform/SDLInputTranslator.cpp:94` | `TransformTouchNormalized` | same formula for `gy` | Y-UP: TOP=+160 | Y-DOWN: TOP=-160 | Same flip as line 88 |
| `src/engine/input/Touch.cpp:58-59,78-79` | `Touch::OnPressed`, `Touch::OnMoved` | stores `(int)y` into `s.currX/Y` -- no conversion | Passthrough: stores whatever SDLInputTranslator produced | n/a -- just storage | No change; convention is set upstream |
| `src/engine/input/Touch.cpp:104-106,114-116` | `Touch::GetTouchInRegion` | `(float)s.currY` vs `[bottom, top]` bounds | Both currY and bounds are Y-UP; comparison is `bottom <= y && y <= top` | Binary's `GetTouchInRegion` also compares slot.y against [bottom,top]; but slot.y is Y-DOWN and the binary's bounds constants are also derived in Y-DOWN space | No change to loop logic; correctness follows from callers supplying matched bounds |
| `src/hud/MenuButton.cpp:507-510` | `MenuButton::Update` (hit-test) | `bottom = pos.y - hh - margin`, `top = pos.y + hh + margin` | Y-UP bounds: `bottom` < `top`; `GetTouchInRegion(left,right,bottom,top)` | Binary: same geometry but pos.y and bounds are in the SAME space as slot.y. Since binary touch Y and binary world Y are DIFFERENT spaces, the binary cannot be passing raw pos.y as-is to the region check. See Table 2. | See Table 2 analysis |
| `src/hud/MenuButton.cpp:536-539` | `MenuButton::Update` (release inside check) | `m_TouchY >= bottom && m_TouchY <= top` | m_TouchY is currY (Y-UP); bounds are Y-UP | Same issue as above | See Table 2 |
| `src/hud/MenuButton.cpp:556-557` | `MenuButton::UpdateTouchPosition` | `(float)s->currX`, `(float)s->currY` into `m_TouchX/Y` | Y-UP passthrough | Binary stores raw slot.y | No change; convention propagates from slot |
| `src/hud/ScrollingMenu.cpp:163-168` | `ScrollingMenu::Update` Phase 2 (acquire) | `TouchInRegion(pos.x+xMin, pos.x+xMax, pos.y+yMin, pos.y+yMax, -1)` | pos.y+offsets are Y-UP world coords | Binary also uses `pos.x + m_OuterRegion[1/2]` as Y bounds -- see Table 2 | See Table 2 |
| `src/hud/ScrollingMenu.cpp:184,194` | `ScrollingMenu::Update` Phase 2 (anchor latch) | `ts->currX`, `ts->currY` stored into `m_TouchAnchorPos.x/y` | Y-UP stored | Binary stores Y-DOWN slot.y | Sign flip here if migrating |
| `src/hud/ScrollingMenu.cpp:224,227-228` | `ScrollingMenu::Update` Phase 3 debug | `tsDbg->currX`, `tsDbg->currY` (diagnostic printf only) | Y-UP | Y-DOWN | No functional impact (printf only) |
| `src/hud/ScrollingMenu.cpp:265` | `ScrollingMenu::Update` Phase 3B (drag) | `(float)ts->currY` as `currentY` | Y-UP | Y-DOWN | If migrated: `currentY` and `anchorY` both flip, delta unchanged; DRAG_THRESHOLD test is on absolute delta -- **no net effect on drag formula** |
| `src/hud/ScrollingMenu.cpp:288-289` | `ScrollingMenu::Update` Phase 3B | `currentY - anchorY` absolute value for drag threshold | Delta is sign-agnostic | Delta is sign-agnostic | No change |
| `src/entities/SlashEntity.cpp:328,331` | `SlashEntity::Update` | `(float)s->currX`, `(float)s->currY` passed to `OnTouchActive` | Y-UP: currY = +160 at TOP | Y-DOWN: slot.y = -160 at TOP | **Blade draws upside-down** if gy flipped without adjusting trail/collision. See Blade section below. |
| `src/entities/SlashEntity.cpp:173-174` | `SlashEntity::OnTouchActive` | `delta.y = newPos.y - lastCenter.y` for movement threshold | Y-UP delta | Y-DOWN delta | Delta magnitude unchanged; direction irrelevant for threshold test |

Summary count: **2 upstream formula sites** (SDLInputTranslator:88 and :94) feed all
downstream consumers. The rest are passthrough readers.

---

## Table 2: hit-test bound computations

### MenuButton (src/hud/MenuButton.cpp:507-510, :516, :536-539)

```
bottom = pos.y - hh - m_AnimSpeed    // world Y-UP lower bound
top    = pos.y + hh + m_AnimSpeed    // world Y-UP upper bound
GetTouchInRegion(left, right, bottom, top, -1)
```

Port calls `GetTouchInRegion` with the port signature
`(left, right, bottom, top)` where `bottom < top`. The implementation checks
`bottom <= currY && currY <= top`.

**Both sides of the comparison are in the same space as whatever SDLInputTranslator
produces.** Under the current port convention (Y-UP), `pos.y = +X` means X units
toward landscape TOP, so `top > bottom`, and the hit-test is geometrically correct.

Under the binary's convention (Y-DOWN), `pos.y` is a world coordinate (+160 = TOP)
and `slot.y` is a touch coordinate (-160 = TOP). **They are in DIFFERENT spaces.**
The binary must perform some reconciliation. The binary's `GetTouchInReigion` (note
the typo) at 0x001954b4 iterates the slot table reading `states1[i].field9_0xc`
(= slot.y, Y-DOWN) and comparing against the passed bounds.

This means the binary's `MenuButton::Update` (0x0014e614) must be passing bounds
derived from `pos.y` into a function that will compare them against Y-DOWN slot.y.
Two possible reconciliations in the binary:

1. **The binary's MenuButton computes bounds in touch Y-DOWN space** -- i.e., it
   does something like `bottom_touch = -pos.y - hh` to invert world pos.y before
   calling GetTouchInRegion.
2. **The binary's GetTouchInRegion compares slot.y after its own coordinate
   transform** -- i.e., the bounds passed to it are in world Y-UP, and the function
   internally converts slot.y to world Y before comparing.

The existing RE of `GetTouchInReigion` (0x001954b4, Section from Touch.h) does NOT
document a coordinate conversion inside the function; it reads the slot directly.
Option 1 is more likely. This is an **RE gap** -- the binary's MenuButton bound
formula has not been verified from disassembly.

**Current port status:** The port avoids this ambiguity entirely by keeping both
touch coords and world coords in the same Y-UP space. The hit-tests work correctly
without any sign flip in the bounds. Migrating to binary Y-DOWN would require
verifying from Ghidra whether MenuButton::Update inverts pos.y for the bounds,
and then replicating that inversion. This adds significant complexity.

### ScrollingMenu outer-region (src/hud/ScrollingMenu.cpp:163-168)

```
TouchInRegion(pos.x + m_OuterRegion[0],   // x0 = pos.x + xMin_rel
              pos.x + m_OuterRegion[3],   // x1 = pos.x + xMax_rel
              pos.y + m_OuterRegion[1],   // y0 = pos.y + yMin_rel
              pos.y + m_OuterRegion[2],   // y1 = pos.y + yMax_rel
              -1)
```

The binary formula (confirmed via decompile) uses `pos.x + field100/102/103/101`.
The m_OuterRegion offsets are relative to pos.y. If pos.y is in world Y-UP and
slot.y is touch Y-DOWN they are still comparable under the port's unified convention.

Under migration: same issue as MenuButton -- would need to verify whether
m_OuterRegion values are stored as signed world-Y offsets or touch-Y offsets.

**Current port status:** Correct and self-consistent under port Y-UP convention.

---

## Table 3: static Y constants in screen files

These are world-coordinate positions used to place buttons. They do NOT feed
directly into touch comparisons (MenuButton::Update computes its own bounds from
`pos.y` at runtime). The question is whether flipping touch-Y would require
flipping these constants.

**Answer: No -- these are WORLD Y-UP positions and they must not change under any
touch-Y convention change. World ortho is Y-UP in both port and binary; these
constants are already correct.**

| File:line | Constant | Value | Matches binary? | Flip if touch-Y changes? |
|-----------|----------|-------|-----------------|--------------------------|
| `src/screens/MainScreen.cpp:56` | `POS_PLAY_BUTTON` | `(16, -66, 0)` | Yes (binary RE'd) | **No** -- world Y, unaffected |
| `src/screens/MainScreen.cpp:57` | `POS_DOJO_BUTTON` | `(-144, -65, 0)` | Yes | **No** |
| `src/screens/MainScreen.cpp:58` | `POS_QUIT` | `(182, -106, 0)` | Yes | **No** |
| `src/screens/MainScreen.cpp:59` | `POS_MORE_GAMES` | `(182, -106, 0)` | Stubs defunct feature | **No** |
| `src/screens/MainScreen.cpp:60` | `POS_SOUND_TOGGLE` | `(216, 135.5, 0)` | Yes | **No** |
| `src/screens/MainScreen.cpp:61` | `POS_MUSIC_TOGGLE` | `(176, 135.5, 0)` | Yes | **No** |
| `src/screens/DojoScreen.cpp:40` | `POS_BACK_BUTTON` | `(185, -106, 0)` | Yes | **No** |
| `src/screens/DojoScreen.cpp:41` | `POS_SHOP_BUTTON` | `(-18, -15, 0)` | Yes | **No** |
| `src/screens/DojoScreen.cpp:42` | `POS_ABOUT_BUTTON` | `(145, 42, 0)` | Yes | **No** |
| `src/screens/ShopScreen.cpp:71` | `POS_BACK_BUTTON` | `(185, -105, 0)` | Yes (DAT_0015e55c/560) | **No** |
| `src/screens/ShopScreen.cpp:77` | `POS_EQUIP_BUTTON` | `(145, 104, 0)` | Yes (DAT_0015e564/568) | **No** |
| `src/screens/ShopScreen.cpp:83` | `POS_BACK_BUTTON_NEW` | `(185, -105, 0)` | Yes (DAT_0015e918/91c) | **No** |
| `src/screens/ShopScreen.cpp:98` | `LIST_POS_Y` | `40.0f` | Yes (DAT_0015ead8, confirmed Section 4 y-axis-convention.md) | **No** |
| `src/screens/AboutScreen.cpp:39` | `POS_BACK_BUTTON` | `(185, -106, 0)` | Yes | **No** |
| `src/screens/AboutScreen.cpp:44` | `POS_OFN_BUTTON` | `(0, 480, 0)` | Yes (offscreen stub) | **No** |
| `src/screens/GameModeScreen.cpp:32` | `POS_BACK` | `(195, -110, 0)` | Yes (DAT_0013ea04/08/0c) | **No** |
| `src/screens/GameModeScreen.cpp:33` | `POS_CLASSIC` | `(-70, 71, 0)` | Yes (DAT_0013ea18/1c/0c) | **No** |
| `src/screens/GameModeScreen.cpp:34` | `POS_ZEN` | `(88, 48, 0)` | Yes (DAT_0013ea58/5c/60) | **No** |
| `src/screens/GameModeScreen.cpp:35` | `POS_ARCADE` | `(19, -76, 0)` | Unverified (DAT address not confirmed) | **No** |

None of the screen-position constants feed into touch comparisons directly; they
set `MenuButton::pos` which is then inflated by half-sizes at runtime inside
`MenuButton::Update`. No Y constants need to change under any touch-Y convention.

---

## Section: SlashEntity blade

**Binary:** `SlashEntity::Update` (0x17D664) polls `Mortar::Touch::GetSlot(0)` and
calls `OnTouchActive(slot.x, slot.y)`. In the binary, slot.y = -160 at landscape TOP.
`OnTouchActive` stores `newPos = Vec3(x, y, 0)` and interpolates trail points at
`POINT_SPACING=64` unit intervals along the delta vector. The delta is purely spatial
(used for threshold and interpolation step, not for direction sign semantics). The
resulting trail points `m_Points[i].center` have the same sign as slot.y.

`RebuildGeometry` builds vertex strips by computing a perpendicular to the blade
direction. The blade is rendered with world-space coords via the same ortho projection
as everything else. If slot.y = -160 at TOP, blade points at the top of the screen
have `center.y = -160`, which (under the ortho Y-UP projection) maps to the BOTTOM of
the clip space -- producing an upside-down blade.

However, the binary works correctly on the Bada device. The explanation:
`CollideWithSphere` (0x17B570) uses the blade line segment in the same coordinate
space as fruit `pos` (world Y-UP). If blade points are in touch Y-DOWN (-160 = TOP)
but fruit `pos` is in world Y-UP (+160 = TOP), slicing at the top of the screen would
use blade center.y = -160 to intersect fruit.pos.y = +160: they never match, so no
fruit could be cut near the top.

This means the binary's blade MUST be in world Y-UP space, not touch Y-DOWN space. The
binary's slot.y (-160 at TOP) matches world Y-UP (-160 at bottom) ONLY IF there is an
additional transformation happening. This is consistent with the y-axis-convention.md
finding in Section 6 that world Y = -240 at LEFT, +240 at RIGHT, which matches the
slot.y range of approximately [-160,+160] across the 320-unit screen axis.

**Actually the coordinate spaces DO match for the landscape X axis (world X = [-160,+160]
corresponds to the device's height axis):** slot.y in the binary spans [-160,+160] along
the landscape HEIGHT axis (physical portrait X-axis), and world X also spans [-160,+160]
along the landscape height axis. BUT the binary's slot.y measures along the LANDSCAPE
HEIGHT (what Ghidra calls X in world space) and world Y measures along the LANDSCAPE
WIDTH. Let me re-read the coordinate system docs:

From y-axis-convention.md Section 3:
> X: +160 (top of landscape) to -160 (bottom) -- 320 units
> Y: -240 (left of landscape) to +240 (right) -- 480 units

So world X is the HEIGHT axis. The binary's slot.y is the PIXEL Y value after the
portrait-to-landscape rotation -- which is along the landscape HEIGHT axis.

This means binary `slot.y` corresponds to world `pos.x`, NOT world `pos.y`.

This is a **fundamental finding**: the binary's touch pipeline produces:
- `slot.x` = landscape X (= world pos.x axis, range ±160)
- `slot.y` = landscape Y (= world pos.y axis, range ±240)

Revisiting `TransformTouchPos`:
```
result.x = int(rawPortraitY * 480 / 800)       -- landscape X, range [0,479] -> [0,480]
result.y = 319 - int(rawPortraitX * 320 / 480) -- landscape Y, inverted
```

After `PointerMoveCallback`: `slot.y = 160 - pixel_y`
- pixel_y range [0, 319] -> slot.y range [-159, +160]

And `slot.x` is presumably `pixel_x - 240` (not detailed in docs but analogous).

So `slot.y` after PointerMoveCallback maps to world Y (the 480-unit horizontal axis).
This confirms: **slot.y and world pos.y are in the SAME axis.** The sign is what differs:
- Port: slot.y = +160 at landscape RIGHT (same as world pos.y = +160 at right edge).
- Binary: slot.y = 160 - pixel_y. At landscape RIGHT (pixel_y near 319): slot.y = -159.
  At landscape LEFT (pixel_y near 0): slot.y = +160.

Wait, that gives slot.y = +160 at landscape LEFT and -160 at landscape RIGHT. But world
pos.y = -240 at LEFT and +240 at RIGHT. The signs are OPPOSITE. This is the same
conclusion as y-axis-convention.md.

**SlashEntity trail conclusion:** The blade points are in whatever coordinate space
`OnTouchActive` receives for `y`. Under the port's Y-UP convention, the blade points
have `center.y > 0` when the finger is toward landscape RIGHT, matching world pos.y
for fruit collision geometry. This is correct and self-consistent. If the port switched
to Y-DOWN (TOP=-160), the blade would still work correctly for COLLISION because
SlashEntity only uses `center.y` for collision sphere intersection tests in the same
coordinate space as `entity->pos.y` -- but only if fruit pos.y is in the same space as
the blade's center.y. Fruit pos.y is world Y-UP. So:

- Port Y-UP for blade: blade.center.y matches fruit.pos.y space. **Correct.**
- Binary Y-DOWN for blade: blade.center.y would be -entity.pos.y. **Upside-down collision.**

The binary does NOT have this problem only if `SlashEntity::OnTouchActive` in the binary
receives `slot.y` which is ALREADY in world-Y-UP space for the collision-relevant axis.
But we showed slot.y = 160 - pixel_y = +160 at landscape LEFT = world Y = -240... they
don't match. Unless there is ANOTHER coordinate transform before `OnTouchActive` that I
have not seen.

**This is an unresolved RE gap.** The binary's blade-to-fruit collision axis alignment
has not been confirmed by decompiling `SlashEntity::Update`'s actual input path in the
binary. Changing the port's touch Y convention without first confirming the binary's
blade input pipeline could break fruit slicing.

**Practical implication:** The port's unified Y-UP convention means blade points and
fruit positions are in the same coordinate space. This works. Do not migrate until the
blade path in the binary is confirmed.

---

## Section: cross-screen impact summary

### Will need fix when touch-Y flips (if migration proceeds)

- **`src/platform/SDLInputTranslator.cpp` lines 88 and 94** -- the two formula sites
  that generate `gy`. All other consumers are downstream of these.
- **`src/hud/ScrollingMenu.cpp` lines 184,194** -- anchor latch stores `ts->currY`.
  If gy flips, `m_TouchAnchorPos.y` would be Y-DOWN. The drag formula delta
  `currentY - anchorY` would then be in Y-DOWN. The sign analysis in
  shop-scroll-debug.md Section 3 was derived assuming the binary's Y-DOWN convention;
  the port's current DRAG_DELTA_FACTOR formula was written assuming Y-UP. After the
  flip, the binary-exact formula must be used verbatim. This is already documented as
  the "three fixes" in y-axis-convention.md Section "Recommended Port Changes".
- **`src/entities/SlashEntity.cpp` lines 328,331** -- blade tracks `s->currY` directly
  into world-space trail points used for collision. Flipping gy would put blade points
  in Y-DOWN space while fruit `pos.y` remains Y-UP, breaking all fruit-slicing.
  **This is the largest risk.** Mitigation: negate `s->currY` in `OnTouchActive` call,
  OR verify from binary that the blade input is the same space as fruit.pos.

### Will not be affected

- All `src/screens/*.cpp` button position constants -- these are world Y-UP positions
  that do not change.
- `MenuButton::Update` bounds formula (`pos.y +/- hh`) -- bounds are computed in world
  Y-UP at runtime, and as long as both the bounds and currY are in the same space,
  the comparison is correct. Under port unified Y-UP this already works; under binary
  split convention it would need investigation (RE gap: has the binary's bounds formula
  been verified from disassembly?).
- `src/hud/MissControl.cpp` -- no direct touch reads found.
- `src/screens/DojoScreen.cpp`, `src/screens/AboutScreen.cpp`,
  `src/screens/GameModeScreen.cpp` -- no direct touch reads; all touch input goes
  through `MenuButton::Update` (which they instantiate).
- `src/screens/ShopScreen.cpp` -- touch reads only via `m_bTouchProcessed` (a flag set
  by `ScrollingMenu`), not direct slot reads.
- `src/screens/MainScreen.cpp` -- no direct touch reads (MenuButton handles it).
- `src/hud/TutorialControl.cpp` -- not confirmed to read touch directly; uses MenuButton.

### Unsure -- needs runtime test or further RE

- **`MenuButton::Update` hit-test bounds vs binary** -- whether the binary passes
  world-Y-UP bounds directly to `GetTouchInRegion` when that function uses Y-DOWN
  slot.y. If the binary passes Y-UP bounds to a Y-DOWN comparison, buttons would hit-
  test incorrectly on the Y axis. The binary shipped and worked, so either:
  (a) there is a coordinate transform inside `GetTouchInReigion` not documented yet, or
  (b) `MenuButton::Update` flips its bounds before passing them.
  This RE gap must be closed before any migration.
- **SlashEntity input path in binary** -- what coordinate space does the binary's
  `SlashEntity::Update` use for the x/y it passes to `OnTouchActive`? Is it slot.y
  directly, or after some transformation? If slot.y = -160 at landscape RIGHT and
  fruit.pos.y = +240 at landscape RIGHT, they cannot match for collision detection.
  This has not been traced from binary.

---

## Section: recommended migration order

### The key question

Before committing to any migration, two RE gaps must be closed:

**Gap 1:** Decompile `MenuButton::Update` (0x0014e614) hit-test block. Confirm whether
the Y bounds passed to `GetTouchInReigion` are raw `pos.y +/- halfHeight` or whether
they are sign-inverted. If the binary passes `-(pos.y + hh)` as the top bound (i.e.
negates to convert world Y-UP to touch Y-DOWN), then the port's MenuButton bounds
formula also needs to invert, and migrating is non-trivial.

**Gap 2:** Trace the exact path from `slot.y` (output of `PointerMoveCallback`) to
`SlashEntity::OnTouchActive`'s `y` parameter. Does it pass through any coordinate
transform? Specifically, does `CollideWithSphere` compare blade centers against fruit
`pos.y` directly, and does the binary blade center end up in the same space as
`pos.y`? If not (if there is a sign flip in the collision path), then migrating touch-Y
will require a compensating flip in the blade's collision math.

### If both gaps confirm binary uses Y-DOWN for blade input AND inverted bounds:

Migrating to binary Y-DOWN would require changes at:
1. `SDLInputTranslator.cpp:88,94` -- flip formula (2 lines).
2. `ScrollingMenu.cpp` -- the three fixes already documented in y-axis-convention.md
   remain correct (they were derived in binary Y-DOWN space).
3. `SlashEntity.cpp:331` -- negate `s->currY` before passing to `OnTouchActive`, or
   add a sign flip inside `OnTouchActive`. This is required because blade points must
   remain in world Y-UP for collision with fruits to work.
4. `MenuButton.cpp:507-510` -- if the binary negates pos.y for bounds, add equivalent
   logic. If the binary passes raw pos.y (which would mean it relies on both being
   Y-UP world coords inside GetTouchInRegion), no change needed -- but then the binary
   must have GetTouchInRegion compare against world Y, meaning it converts slot.y to
   world Y internally, which contradicts current understanding.

### If gaps confirm binary also uses Y-UP internally (i.e. the "Y-DOWN" label is
   a documentation artifact of the pixel pipeline, but by the time coords reach the
   game logic layer they are Y-UP):

Then the port is already correct. The `gy = (FN_SCREEN_H/2) - ny*(FN_SCREEN_H)`
formula simply matches the binary's final output (after the double-flip in
TransformTouchPos + PointerMoveCallback that happens to produce Y-UP for the game-
logic range). This seems plausible because `slot.y = 160 - pixel_y` and `pixel_y`
after the portrait rotation already runs from 0 (at landscape LEFT) to 319 (at
landscape RIGHT), so slot.y runs from +160 at LEFT to -159 at RIGHT -- which IS
inverted relative to world.y (+160 at RIGHT). This means the "Y-DOWN" label in the
docs refers to the physical landscape screen axis orientation, not the game-world sign.

### Firm recommendation: **stay with port convention, fix ScrollingMenu locally**

Given:

1. The port's unified Y-UP convention is internally consistent and geometrically
   correct for all currently working systems (MenuButton hit-tests, blade collision).
2. The ScrollingMenu scroll bugs documented in shop-scroll-debug.md have been
   attributed to three Phase 3B/4/5 formula errors, NOT to the Y convention per se.
   The drag delta `currentY - anchorY` is sign-agnostic to the absolute convention
   (both sides flip together). The formula errors are about the STRUCTURE of the
   formula (missing m_Velocity.y term, premature pending clear, wrong cursor init).
3. Migrating touch-Y would require closing two RE gaps (MenuButton bounds, SlashEntity
   collision path) before it is safe to apply, and carries significant risk of
   breaking the currently-working blade slicing.
4. The comment already written in `SDLInputTranslator.cpp:82-84` correctly identifies
   the discrepancy and states it is handled locally.

**Apply the three ScrollingMenu fixes from y-axis-convention.md / shop-scroll-debug.md
Section 8 as standalone code changes. Do NOT flip gy in SDLInputTranslator at this time.**

If a future decision is made to migrate to binary Y-DOWN (e.g. for a fully faithful
re-implementation of the binary's touch layer), the safe migration order would be:

1. Close Gap 1: decompile `MenuButton::Update` (0x0014e614) Y-bounds block.
2. Close Gap 2: trace `SlashEntity::Update` blade input path from slot.y.
3. Only if both gaps are closed and the impact is understood: flip `gy` in
   `SDLInputTranslator:88,94`.
4. If needed, add a compensating negate in `SlashEntity::OnTouchActive` or inline in
   `SlashEntity::Update` call site.
5. If needed, update `MenuButton::Update` bounds formula.
6. Verify ScrollingMenu still scrolls correctly with the binary-exact Phase 3B formula.

---

## Final summary

**Total call sites requiring change if touch-Y migrated to binary Y-DOWN:** 2 mandatory
(SDLInputTranslator:88,:94) + 1 high-risk (SlashEntity:331 -- blade collision breaks
without compensating negate) + 1 potentially required pending RE gap resolution
(MenuButton:507-510 bounds). The ScrollingMenu delta-based drag formula is sign-
agnostic and does NOT require changes to the formula structure; only the Phase 3B/4/5
structural fixes are required regardless of convention.

**Biggest risk areas:**
1. `SlashEntity` blade-to-fruit collision: if touch-Y flips, blade centers become
   Y-DOWN while fruit positions remain Y-UP. All fruit slicing breaks unless a
   compensating negate is added. Cannot be done safely without first confirming from
   binary whether the blade input is already transformed.
2. `MenuButton` hit-tests: unknown whether the binary's `GetTouchInReigion` internally
   converts coordinates or whether callers invert bounds. Working without RE confirmation
   could silently produce hit-test mismatches in the Y direction.

**Recommendation: STAY WITH PORT CONVENTION; fix ScrollingMenu locally.** Apply the
three formula fixes (Phase 3B missing m_Velocity.y term, Phase 4 remove pending-clear,
Phase 5 cursor init). The unified Y-UP convention is correct for the port and avoids a
high-risk migration that requires closing non-trivial RE gaps. Mark the discrepancy with
the existing `// DIFFERS:` comment in SDLInputTranslator.cpp.

---

## RE gap closure (2026-04-26T18:00)

<!-- Analysed: 2026-04-26T18:00 -->

This section closes the two RE gaps identified in the previous sections:
Gap 1 (MenuButton::Update bounds derivation) and Gap 2 (SlashEntity blade trail
coordinate system and collision alignment). Both gaps were resolved by decompiling
the relevant binary functions directly.

---

### Axis naming disambiguation (prerequisite)

Before reporting findings, a naming ambiguity must be clarified. The docs
(y-axis-convention.md, Section 3) state:

> X: +160 (top of landscape) to -160 (bottom) -- 320 units
> Y: -240 (left of landscape) to +240 (right) -- 480 units

However, from `_Matrix44::OrthoW` with `SetupOrtho(160, -160, -240, 240, ...)`:
- `data[0][0] = 2/(right-left)` is the OpenGL X-clip scale: right=+240, left=-240 --> ±240 is the OpenGL X-axis.
- `data[1][1] = 2/(top-bottom)` is the OpenGL Y-clip scale: top=160, bottom=-160 --> ±160 is the OpenGL Y-axis.

In OpenGL, X=+1 is the right edge, Y=+1 is the top edge of the display.
Therefore:
- What the docs call "pos.x" (the first Vec3 component at HUDControl+0x8 or Entity+0x10)
  maps to the OpenGL X-axis = the LANDSCAPE HORIZONTAL axis (±240, left/right).
- What the docs call "pos.y" (second Vec3 component at HUDControl+0xc or Entity+0x14)
  maps to the OpenGL Y-axis = the LANDSCAPE VERTICAL axis (±160, top/bottom).

The docs' naming ("X = top/bottom, Y = left/right") is inverted from the standard
OpenGL axis assignment. This is a documentation artifact. For the RE gap closure below,
"pos.x" refers to the horizontal ±240 axis and "pos.y" refers to the vertical ±160
axis, matching the binary struct field offsets.

This does NOT change any port code (the port uses the same HUDControl::pos field offsets),
but it resolves the apparent paradox of whether world-Y-up means pos.y > 0 is toward the
landscape top or toward the landscape right.

---

### Gap 1A: MenuButton bounds derivation (0x0014e614)

**Binary addresses analysed:**
- `MenuButton::Update` at `0x0014e614` -- decompiled and disassembled (422 instructions).
- `TouchInRegion` at `0x001691cc` -- decompiled.
- `PointerMoveCallback` at `0x0016a4b4` -- decompiled.

#### Bounds assembly (0x0014e9e4 -- 0x0014ea48)

The critical hit-test block in `MenuButton::Update`, fully reconstructed from disassembly:

```
s17 = this+0x08 = HUDControl::pos.x      // horizontal axis, ±240
s19 = this+0x0c = HUDControl::pos.y      // vertical axis, ±160
s12 = this+0x124 = m_TargetSize.x
s11 = this+0x128 = m_TargetSize.y
s14 = this+0x14c = m_AnimSpeed2          // horizontal margin (default 5.0)
s15 = this+0x150 = m_AnimSpeed           // vertical margin (default 5.0)
s18 = 0.5f,  s13 = -0.5f

s20 = pos.y + m_TargetSize.y * 0.5 + m_AnimSpeed     // yMax
s18 = pos.x + m_TargetSize.x * 0.5 + m_AnimSpeed2    // xMax
s19 = pos.y + m_TargetSize.y * -0.5 - m_AnimSpeed     // yMin
s17 = pos.x + m_TargetSize.x * -0.5 - m_AnimSpeed2    // xMin

// if no slot held (field_0xd8 == -1):
TouchInRegion(s0=xMin, s1=xMax, s2=yMin, s3=yMax, r0=-1)
```

**No sign flip on pos.x or pos.y.** The binary passes raw `pos +/- half +/- margin`
directly to `TouchInRegion`.

Summary formula:
```c
xMin = pos.x - m_TargetSize.x * 0.5f - m_AnimSpeed2;
xMax = pos.x + m_TargetSize.x * 0.5f + m_AnimSpeed2;
yMin = pos.y - m_TargetSize.y * 0.5f - m_AnimSpeed;
yMax = pos.y + m_TargetSize.y * 0.5f + m_AnimSpeed;
TouchInRegion(xMin, xMax, yMin, yMax, -1);
```

The port's current formula at `src/hud/MenuButton.cpp:507-510` matches this exactly.

#### Gap 1B: TouchInRegion slot field (0x001691cc)

`TouchInRegion` reads from the slot table:

```c
int TouchInRegion(float xMin, float xMax, float yMin, float yMax, int hint)
{
    slot = &slotArray[hint];  // or iterates all 16 slots
    // guard: slot.active (+0xa8) > 0
    return (xMin <= slot+0xa0 && slot+0xa0 <= xMax &&
            yMin <= slot+0xa4 && slot+0xa4 <= yMax) ? slot_index : -1;
}
```

Slot field offsets:
- `slot+0xa0` = X (horizontal, ±240): compared against [xMin, xMax] from pos.x
- `slot+0xa4` = Y (vertical, ±160): compared against [yMin, yMax] from pos.y
- `slot+0xa8` = active/pressed float (>0 = active)

#### Gap 1C: What does PointerMoveCallback write to slot+0xa0/0xa4?

From the decompile of `PointerMoveCallback` (0x0016a4b4):

```c
// code 0x99+idx (X move event):
SlashEntity::TouchMoveX(entity, event);
*(float *)(slot + 0xa0) = (float)event->m_mapper + windowWidth * -0.5;
// = pixel_x - 240   (range [-240, +239])

// code 0xa9+idx (Y move event):
SlashEntity::TouchMoveY(entity, event);
*(float *)(slot + 0xa4) = -((float)event->m_mapper + windowHeight * -0.5);
// = -(pixel_y - 160) = 160 - pixel_y
```

From `TransformTouchPos` (0x0018327c), `pixel_y = 319 - int(rawPortraitX * 320/480)`,
with `pixel_y = 319` at landscape TOP and `pixel_y = 0` at landscape BOTTOM.

Therefore:
```
slot+0xa4 = 160 - pixel_y
  landscape TOP  (pixel_y=319): slot+0xa4 = -159 approx -160
  landscape CTR  (pixel_y=160): slot+0xa4 = 0
  landscape BOT  (pixel_y=0  ): slot+0xa4 = +160
```

**slot+0xa4 = -160 at landscape TOP, +160 at landscape BOTTOM: Y-DOWN convention.**

#### Reconciliation: why does MenuButton hit-test work?

HUDControl::pos.y is set by the button initializer (e.g., `POS_PLAY_BUTTON = (16, -66, 0)`).
The value -66 for pos.y means 66 units toward landscape TOP (negative = up in Y-DOWN).
When the user's finger is at that screen location, `pixel_y = 160 - pos.y = 160 - (-66) = 226`,
giving `slot+0xa4 = 160 - 226 = -66`. The comparison
`yMin <= slot.y <= yMax` becomes `(-66 - halfH) <= -66 <= (-66 + halfH)` = TRUE.

**Conclusion for Gap 1:** Binary MenuButton bounds are in Y-DOWN space (negative = toward
landscape TOP). The bounds are derived directly as `pos.y +/- half +/- margin` with NO sign
inversion. This works because both `pos.y` and `slot+0xa4` are in Y-DOWN convention.

The `pos.y` constants in the binary (e.g., -66 for play button) are therefore
**in Y-DOWN coordinates**, not Y-UP. This is Possibility (A) from the earlier analysis
("the binary's pos.y for MenuButton is in TOUCH Y-DOWN coords"). The docs' claim that
"world pos.y=+160 = landscape TOP" was wrong for HUDControl menu elements.

---

### Gap 1 anchor latch in MenuButton (0x0014ea8e -- 0x0014eacc)

When `IsTouchDown(slot) == 0` (release check), the binary reads:
```
*(float *)(this+0xdc) = stored touch X   (field_0xdc)
*(float *)(this+0xe0) = stored touch Y   (field_0xe0)
```
These are compared directly against [xMin, xMax] and [yMin, yMax] for the "release
inside bounds" check.

The anchor touch X/Y is stored by `UpdateTouchPosition()` which (from prior port analysis)
reads `s->currX` and `s->currY` directly. Both are `slot+0xa0` and `slot+0xa4` values
(Y-DOWN). The latch stores Y-DOWN values and compares against Y-DOWN bounds. Consistent.

---

### Gap 2: SlashEntity blade trail coordinate system

**Binary addresses analysed:**
- `SlashEntity::TouchMoveX` at `0x0017c50c` -- decompiled.
- `SlashEntity::TouchMoveY` at `0x0017c490` -- decompiled.
- `PointerMoveCallback` at `0x0016a4b4` -- decompiled (see above).
- `SlashEntity::UpdateTouchDown` at `0x0017d2e4` -- decompiled.
- `SlashEntity::CollideWithEntity` at `0x0017b570` -- decompiled.
- `SlashEntity::UpdatePoints` at `0x0017b92c` -- decompiled.

#### Touch coord to SlashEntity pos (TouchMoveX/Y)

The `PointerMoveCallback` dispatches per-slot events to `SlashEntity::TouchMoveX` and
`TouchMoveY`. These write to `SlashEntity::field_0x10` (= Entity::pos.x) and
`field_0x14` (= Entity::pos.y):

```c
// TouchMoveX (0x0017c50c):
this->field_0x10 = (float)event->m_mapper + windowWidth * -0.5;
//  = pixel_x - 240   (horizontal, ±240)

// TouchMoveY (0x0017c490):
this->field_0x14 = -((float)event->m_mapper + windowHeight * -0.5);
//  = 160 - pixel_y   (Y-DOWN: -160 at landscape TOP)
```

Critically, `PointerMoveCallback` writes IDENTICAL values to `slot+0xa4` and
`SlashEntity::pos.y` -- both use `160 - pixel_y`. There is NO additional transformation
between the slot store and the entity position store.

#### Blade trail point coordinate space

`UpdateTouchDown` (0x0017d2e4) appends blade trail points using `SlashEntity::field_0x10/14/18`
as the head/tail/center positions. The trail point centers are written into `m_pLeftBuffer`
at offsets `[iVar9]` (x) and `[iVar9+4]` (y) from the AddPoint call.

From `SlashEntity__AddPoint` (0x0017ce0c):
```c
*(float *)(m_pLeftBuffer + iVar9)     = in_r1->x;   // center.x
*(float *)(m_pLeftBuffer + iVar9 + 4) = in_r1->y;   // center.y
```

The center passed to AddPoint is `SlashEntity::pos.x/y` (set by TouchMoveX/Y). Therefore:

**Blade trail point center.y is in Y-DOWN space: -160 at landscape TOP, +160 at landscape BOTTOM.**

#### UpdatePoints: collision segment from trail points

`UpdatePoints` (0x0017b92c) builds the collision line segment:
```c
m_Col[+0x4..+0xf]  = head position (from m_pLeftBuffer last vertex)
m_Col[+0x14..+0x1f] = tail position (from m_pLeftBuffer earlier vertex)
```

Both head and tail are read from `m_pLeftBuffer`, which stores Y-DOWN coords. The
collision segment `m_Col` is in Y-DOWN space.

#### CollideWithEntity: blade vs fruit (0x0017b570)

`CollideWithEntity` reads:
- Blade line: `m_Col+4` (head center) and the line defined by `m_Col` -- Y-DOWN
- Entity sphere center: `*(Vec3*)(entity+0x38)` -- the entity's collision sphere center

For a Fruit, the collision sphere is at `Fruit::m_Col->center`, which is updated from
`Fruit::pos_y` (offset 0x14 in Fruit struct, set by the physics engine using the same
coordinate conventions). Both the physics engine and the touch pipeline use the same
underlying coordinate space.

**Conclusion: blade trail points AND fruit positions are BOTH in Y-DOWN space. The
collision math works because both are in the same space.**

There is NO coordinate conversion between touch input and blade trail, and NO conversion
between blade trail and collision test. Everything is Y-DOWN throughout.

---

## Re-evaluation

### 1. What is the binary's actual touch-Y convention as observed by game code?

All game-logic components (MenuButton, SlashEntity, ScrollingMenu) operate in a
**single consistent coordinate system** for the vertical axis:

```
pos.y (and slot+0xa4) = 160 - pixel_y
  = -160 at landscape TOP
  = 0    at landscape CENTER
  = +160 at landscape BOTTOM
```

This is **Y-DOWN** for the vertical (landscape height) axis. The horizontal axis
(pos.x, slot+0xa0) is: `pixel_x - 240`, range [-240, +239], same sign as
left-to-right (no flip). Both axes are centered at screen center.

There is NO point in the binary pipeline where a Y-UP-to-Y-DOWN or Y-DOWN-to-Y-UP
conversion is applied for game-logic consumers. Every component from `PointerMoveCallback`
through `MenuButton::Update`, `SlashEntity::TouchMoveY`, and `CollideWithEntity` uses
the same Y-DOWN convention.

### 2. Was the previous claim "binary touch Y-DOWN, world Y-UP" correct?

**Partially wrong.** The previous documentation (y-axis-convention.md, Section 2/3)
correctly identified that binary slot.y = -160 at landscape TOP (Y-DOWN). However, it
incorrectly claimed that "world pos.y = +160 = landscape top" (Y-UP). The actual
finding from Gap 1 is:

- Binary HUDControl/Entity pos.y = **Y-DOWN** (same convention as slot.y, -160 at TOP).
- The "world Y-UP" claim was based on a misreading of SetupOrtho parameter labels:
  the docs labelled the ±160 vertical axis as "world X" (not "world Y"), leading to
  confusion about which Vec3 field corresponds to which physical axis. In fact,
  HUDControl::pos.y (the second field, at +0x0c) IS the vertical axis AND IS Y-DOWN.

The pos.y constants in screen files (e.g. `POS_PLAY_BUTTON.y = -66`) are therefore
in Y-DOWN space. The port's current constants match the binary values numerically
but have been interpreted as "Y-UP world coords" -- they actually work because the
same numbers appear on both sides of the comparison (pos.y vs slot+0xa4 = same formula).

### 3. Which approach is binary-faithful for the port?

**The port's current unified Y-UP convention does NOT match the binary.**

The port flips sign in `SDLInputTranslator`:
```cpp
gy = (FN_SCREEN_H/2) - ny * FN_SCREEN_H;   // port: +160 at TOP (Y-UP)
```
The binary produces:
```
slot+0xa4 = 160 - pixel_y = -160 at TOP (Y-DOWN)
```

The button position constants in the port (`POS_PLAY_BUTTON.y = -66`) happen to be
numerically identical to the binary, but their physical meaning in the port is
"66 units toward landscape BOTTOM" (Y-UP: negative = bottom), while in the binary
it means "66 units toward landscape TOP" (Y-DOWN: negative = top). The port's
`MenuButton::Update` compares the Y-UP currY against Y-UP bounds derived from the
same constants, so it accidentally works correctly despite the convention inversion.

**Option A: keep port's all-Y-up convention; apply ScrollingMenu fixes locally.**
- Risk: The button constants are physically inverted from the binary (port pos.y=-66
  means "below center" while binary pos.y=-66 means "above center") but since both
  sides of every comparison are inverted together, all hit-tests remain geometrically
  correct. ScrollingMenu drag formulas require the three documented formula fixes
  regardless of convention. SlashEntity blade-vs-fruit collision is correct because
  both are Y-UP in the port. This option works and requires minimal changes.
- Residual inaccuracy: the DIRECTION of the vertical axis is inverted for all
  positions, meaning a faithful binary dump of e.g. pos.y values would look sign-
  flipped relative to the port. For purely gameplay purposes this is irrelevant.

**Option B: switch port to binary's Y-DOWN convention; cascade-flip all touch consumers.**
- Risk: All button position constants, scroll offsets, and physics positions become
  physically correct. However, every caller of currY must change, `SDLInputTranslator`
  must flip, `SlashEntity::OnTouchActive` y-param stays as-is (Y-DOWN), and the three
  ScrollingMenu formula fixes still apply.
- The migration is now well-defined (no hidden conversion inside TouchInRegion or
  CollideWithEntity), so the risk is manageable. The main work is:
  1. Flip `gy` in `SDLInputTranslator.cpp` lines 88 and 94.
  2. Verify all button position constants still work (they will -- same values,
     same sign as binary).
  3. Blade collision: SlashEntity pos.y (Y-DOWN from TouchMoveY) would now match
     `s->currY` (Y-DOWN from flipped SDLInputTranslator). Currently port uses
     `s->currY` (Y-UP) to set blade positions. After flip, `s->currY` becomes Y-DOWN
     and blade positions become Y-DOWN -- matching binary exactly. **No additional
     negate is needed in SlashEntity::OnTouchActive.**
  4. Fruit positions: the physics engine uses `pos.y` directly. If the port's physics
     constants (gravity y-component, initial velocity y) were RE'd from the binary they
     are already in Y-DOWN sign. Verify before assuming.

### 4. Concrete recommendation

**Option A is the safer near-term choice; Option B is correct long-term.**

For the immediate ScrollingMenu fix, apply the three formula fixes from
y-axis-convention.md Section 8. These fixes are identical under both conventions
(the formulas involve velocity and anchor offset which are relative, not absolute).

The key new finding that changes the long-term picture: **migrating to binary Y-DOWN
(Option B) is now confirmed SAFE for SlashEntity**, because `TouchMoveY` already
produces Y-DOWN blade positions, and `CollideWithEntity` compares against Fruit pos
which is also Y-DOWN. Flipping `SDLInputTranslator` so that `s->currY` becomes Y-DOWN
would make the port's `SlashEntity::OnTouchActive` receive Y-DOWN input -- exactly
matching the binary's `TouchMoveY` output. No compensating negate is required.

Similarly, MenuButton::Update hit-tests require NO change under Option B: the bounds
formula uses `pos.y` (constants already in Y-DOWN values numerically) against
`s->currY` (which flips to Y-DOWN) -- identical to the binary.

**The only remaining risk under Option B** is non-touch physics (gravity, throw
velocity initial conditions). If any physics constant was RE'd and applied with a
sign assumption based on "Y-UP", it must be re-verified. This is outside the scope
of the touch pipeline.

---

## Correction to docs/systems/y-axis-convention.md

### Section 2 ("Binary's Touch Coordinate Convention"), line 147:

**Claim (line 147):** "Binary touch-Y convention: landscape TOP = slot.y approx -160 (negative)."

This is CORRECT for `slot+0xa4`, but the description applies equally to `world pos.y`
(HUDControl and Entity). The CORRECTION needed is in the downstream interpretation:

### Section 3 ("Binary's World Ortho Convention"), lines 210-217:

**Claim:** "world pos.y=+160 = top of landscape screen, world pos.y=-160 = bottom of landscape screen."

**Correction:** This describes the OpenGL Y-clip axis direction correctly (top=+160 in the
OrthoW parameters), but this is the DISPLAY/CLIP convention, not the game-logic convention.
The GAME-LOGIC pos.y (stored in HUDControl::pos.y and Entity::pos_y) is the VALUE SET BY
TOUCH CALLBACKS, which use Y-DOWN (pos.y = -160 at landscape TOP). The OrthoW `top=160`
parameter means the clip-Y=+1 boundary is at world Y=+160 for RENDERING purposes; it does
NOT mean game objects are placed with pos.y=+160 at the top. In practice, objects near the
landscape top have pos.y ≈ -160 (Y-DOWN), and the OrthoW renders them at clip-Y = -1
(BOTTOM of clip space). The display is ALSO rotated 90 degrees, so clip-Y=-1 maps to the
landscape top on the physical device.

The full chain: Touch at landscape TOP → pixel_y=319 → TouchMoveY → pos.y=-159 →
OrthoW renders pos.y=-159 at clip_y=-159/160 ≈ -1 → clip_y=-1 → physical device BOTTOM
of portrait view → which is the LANDSCAPE TOP (since the device is rotated 90 degrees
and the display output is also rotated to compensate). Everything is self-consistent.

### Summary of y-axis-convention.md errors:

| Section | Current claim | Correction |
|---------|---------------|------------|
| Section 3, lines 210-217 | "world pos.y=+160 = top of landscape" | Render parameter only; game-logic pos.y=-160 is at landscape TOP |
| Section 2 Summary Table, row "World ortho Y at landscape top" | "+160" | The OrthoW TOP parameter is +160 but game entities have pos.y=-160 at that location |
| Summary Table, row "pos.y > 0 means" | "toward landscape top" | "toward landscape BOTTOM" in game-logic space |
| Section 8, "HUDControl3d::Draw Offset" | "not affected by pivot" is still correct | No change needed |
| Recommended Port Changes, Change 1 | Claims flipping gy matches binary | NOW CONFIRMED CORRECT: binary Y-DOWN means gy should be negative at TOP. Change 1 remains the right fix but its rationale changes: the flip is needed so that currY matches game-logic pos.y (both Y-DOWN), not because slot.y is in a "different space from world". |
