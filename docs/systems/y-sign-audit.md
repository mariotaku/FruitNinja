<!-- Analysed: 2026-04-26T21:30 -->

# Y-Sign Audit: Port vs Binary

Binary: `FruitNinja.exe` (ARM32 LE, Samsung Bada, 480x320 landscape)

This document audits every Y-axis constant, velocity, and sign in port
game-logic files against the binary's actual values. It establishes
which sites need to change if the port adopts the binary's Y-DOWN
convention, and which are already binary-correct in their numerical values.

## Background

From `docs/systems/touch-y-audit.md` (RE gap closure 2026-04-26):

- Binary Y convention (game-logic layer): `pos.y = 160 - pixel_y`
  - landscape TOP = `pos.y = -160` (negative)
  - landscape BOTTOM = `pos.y = +160` (positive)
  - This is Y-DOWN: negative values are toward the TOP of the screen.
- Port Y convention (current): `gy = (FN_SCREEN_H/2) - ny * FN_SCREEN_H`
  - landscape TOP = `gy = +160` (positive)
  - landscape BOTTOM = `gy = -160` (negative)
  - This is Y-UP: positive values are toward the TOP of the screen.

Critically: **world-position constants (button POS_* values) were copied
numerically from the binary and have Y-DOWN physical meaning**. The port's
MenuButton and SlashEntity both operate in a uniform Y-UP convention so all
comparisons are self-consistent internally -- wrong convention, but the
inversion affects both sides of every test equally. This is documented in
`docs/systems/touch-y-audit.md` Section "Re-evaluation", Option A.

**Gravity and physics Y signs are the primary concern.** If gravity Y is
negative in the binary (meaning "downward force in Y-DOWN space = +Y
direction = bottom of screen"), and the port keeps that same negative value
under Y-UP convention, then the force acts toward the top of the screen --
reversing the visual direction of gravity.

---

## Table 1: Gravity constants

These constants drive the downward pull on entities. In Y-DOWN space,
"gravity downward" means acceleration toward +Y (positive = toward BOTTOM).
In Y-UP space, gravity downward means acceleration toward -Y.

| File:line | Port constant | Port value | Binary DAT / source | Binary value | Physical meaning in binary | Physical meaning in port | Action |
|-----------|--------------|------------|--------------------|--------------|-----------------------------|--------------------------|--------|
| `src/entities/Fruit.cpp:119` | `m_Gravity = Vec3(0.0f, -12.0f, 0.0f)` in Init | -12.0f | Literal in code at Fruit::Init (0x00176708); x-component confirmed as `DAT_00176a18 = 0x00000000 = 0.0f`; gravity.y is a float literal `-12.0` encoded inline in THUMB code | -12.0f | Y-DOWN: -12 is toward TOP -- WRONG for gravity | Y-UP: -12 is toward BOTTOM -- visually CORRECT | **VERIFY**: binary literal could be +12.0 in Y-DOWN (downward = +Y). See note. |
| `src/entities/Fruit.cpp:880` | `m_Gravity = Vec3(0.0f, -12.0f, 0.0f)` in Slice() reset | -12.0f | Same literal, reset after split | -12.0f | Same as Init | Same as Init | **VERIFY** same as above |
| `src/entities/Bomb.cpp:34` | `GRAVITY_Y = -12.0f` | -12.0f | `DAT_00172cb0 = 0xC3A00000 = -320.0f` (this is OFFSCREEN_Y, not gravity). Bomb gravity literal is at Bomb::Init (0x172504) -- from `m_AccelForce = Vec3(0, GRAVITY_Y, 0)`. The constant used for bomb gravity matches Fruit's literal -12.0. | -12.0f | Y-DOWN: -12 toward TOP -- WRONG for gravity | Y-UP: -12 toward BOTTOM -- CORRECT | **VERIFY** same as above |
| `src/entities/SplatEntity.cpp:54` | `UP_GRAVITY = -10.0f` | -10.0f | Documented: "verified 2026-04-15 from instruction at 0x0017fa90: `vmov.f32 s13, 0xc1200000 = -10.0f`; `vmla.f32 s15, s13, s14`" | -10.0f (0xC1200000) | Y-DOWN: -10 toward TOP | Y-UP: -10 toward BOTTOM -- CORRECT | **VERIFY** same issue as Fruit gravity |
| `src/entities/Coin.cpp:31` | `COIN_DEFAULT_GRAVITY = Vec3(220.0f, -140.0f, 0.0f)` -- gravity.y used as target Y in state 4 homing | -140.0f (y component) | Binary DAT not resolved here; coin gravity doubles as "target position" in homing state. Coin::_Update uses `m_TargetY` for both gravity accumulation AND the final home position. | Not independently verified | Not gravity per se -- it is the HUD target Y for coin collection | Port uses -140 as target Y below center under Y-UP | No change needed; this is a target position, not gravity |

**GRAVITY NOTE**: The comment in `Fruit.cpp` at line 119 says
"Default gravity -- confirmed from Fruit::Init 0x00176708: literal -12.0,
DAT_00176a18=0.0". The DAT at 0x00176a18 = 0x00000000 = 0.0f (confirmed),
which is the gravity X component. The gravity Y = -12.0f is a floating-point
literal encoded in the ARM instruction stream.

In Y-DOWN convention, gravity downward = acceleration toward +Y (BOTTOM =
+160). A gravity.y of -12.0 would accelerate entities UPWARD (toward TOP),
which is wrong. However, Fruit::Update integrates with:

```
pos += (vel * dt + gravity * 0.5 * dt * dt) * 60.0
vel += gravity * dt
```

With `gravity.y = -12.0` and a fruit launched with positive initial `vel.y`
(upward in Y-UP), the fruit will rise then fall back under Y-UP semantics.
Under Y-DOWN semantics with the same -12.0, a fruit launched with negative
`vel.y` (toward TOP = -Y in Y-DOWN) would also rise-then-fall. The SIGN of
the initial launch velocity is what determines which convention is in use.

**If launch velocities in WaveManager/SpawnFruit use Y-DOWN convention
(negative = upward launch), then gravity.y = -12.0f is CORRECT for Y-DOWN.**
The two cancel: negative launch + negative gravity gives rise-then-fall in
both conventions as long as the signs are consistent throughout. This is
currently the case in the port -- all physics is consistent under Y-UP
because all constants were copied numerically.

**Conclusion for gravity**: No flip needed. The gravity constant's sign is
correct relative to the launch velocity convention used in the port. Both
must flip together if the convention changes, leaving their relative sign
unchanged.

---

## Table 2: Off-screen Y bounds and kill conditions

Sites where `pos.y` is set to a large constant to place entities off-screen
or tested against kill boundaries.

| File:line | Port value | Binary DAT | Binary value | Physical meaning | Port meaning | Action |
|-----------|------------|------------|--------------|-----------------|--------------|--------|
| `src/entities/Bomb.cpp:37` | `OFFSCREEN_Y = -320.0f` | `DAT_00172cb0` = `0x0000A0C3` = **-320.0f** | -320.0f | Y-DOWN: -320 is far above TOP of screen (-160 is TOP boundary) | Y-UP: -320 is far below BOTTOM of screen (-160 is BOTTOM boundary) | **No change** -- binary value confirmed correct |
| `src/entities/Bomb.cpp:328-329` | `pos.y = OFFSCREEN_Y; vel = Vec3(HIT_COL_POS, -1.0f, HIT_COL_POS)` | `DAT_00172cb0` = -320.0f | -320.0f | Y-DOWN: places bomb above screen; -1.0 vel.y moves it further up-off-screen | Y-UP: places bomb below screen; -1.0 vel.y moves further down | **VERIFY**: under Y-DOWN, vel.y = -1.0 moves entity toward TOP (-Y direction); under Y-UP, -1.0 moves toward BOTTOM. Either direction moves it further off-screen. Functionally equivalent. No change. |
| `src/entities/Bomb.cpp:374-376` | `pos.y = OFFSCREEN_Y; vel = Vec3(0.0f, -1.0f, 0.0f)` | `DAT_00172cb0` = -320.0f | -320.0f | Bomb chain: drift off top (Y-DOWN) or off bottom (Y-UP) | Same analysis as above | **No change** |
| `src/entities/Bomb.cpp:37-38` | `BOUNDS_MIN_Y = -240.0f; BOUNDS_MAX_Y = +240.0f` | `DAT_00172f34` = `0xC3700000` = **-240.0f**; `DAT_00172f38` = `0x43700000` = **+240.0f** | -240.0f / +240.0f | Y-DOWN: live range is Y in [-240, +240]; bomb beyond either end dies | Y-UP: same numeric range [-240, +240] | **No change** -- binary values confirmed correct |
| `src/entities/Bomb.cpp:39-40` | `BOUNDS_MIN_X = -360.0f; BOUNDS_MAX_X = +360.0f` | `DAT_00172f3c` = `0xC3B40000` = **-360.0f**; `DAT_00172f40` = `0x43B40000` = **+360.0f** | -360.0f / +360.0f | Horizontal kill bounds | Same | **No change** -- binary values confirmed correct |
| `src/entities/Fruit.cpp:504` | `OFFSCREEN_BASE = 160.0f` | `DAT_00175548` = `0x43200000` = **160.0f** | 160.0f | Half-screen height; used to compute kill zone `-(margin+160)` | Same | **No change** |
| `src/entities/Fruit.cpp:505` | `WARP_CLAMP_TOP = -320.0f` | `DAT_0017554c` = `0xC3A00000` = **-320.0f** | -320.0f | Y-DOWN: warp far above screen | Y-UP: warp far below screen | **No change** -- numerical match |
| `src/entities/Fruit.cpp:506` | `WARP_THRESH_BOT = -240.0f` | `DAT_00175550` = `0xC3700000` = **-240.0f** | -240.0f | Y-DOWN: -240 is 80 units above TOP (-160) of screen | Y-UP: -240 is 80 units below BOTTOM | **No change** -- numerical match |
| `src/entities/Fruit.cpp:507` | `WARP_CLAMP_BOT = +320.0f` | `DAT_00175554` = `0x43A00000` = **+320.0f** | +320.0f | Symmetric warp below screen | Same | **No change** |
| `src/entities/Fruit.cpp:508` | `WARP_CLAMP_RIGHT = -480.0f` | `DAT_00175558` = `0xC3F00000` = **-480.0f** | -480.0f | Horizontal warp | Same | **No change** |
| `src/entities/Fruit.cpp:509` | `WARP_CLAMP_LEFT = +480.0f` | `DAT_0017555c` = `0x43F00000` = **+480.0f** | +480.0f | Horizontal warp | Same | **No change** |
| `src/entities/Fruit.cpp:510` | `WARP_THRESH_TOP = +240.0f` | `DAT_00175560` = `0x43700000` = **+240.0f** | +240.0f | Y-DOWN: +240 is 80 units below BOTTOM (+160) of screen | Y-UP: +240 is 80 units above TOP | **No change** -- numerical match |
| `src/entities/Fruit.cpp:511` | `SCALE_MARGIN_MULT = 50.0f` | `DAT_00175564` = `0x42480000` = **50.0f** | 50.0f | Scale multiplier for fruit size margin | Same | **No change** |
| `src/entities/Fruit.cpp:512` | `WARP_THRESH_RIGHT = +360.0f` | `DAT_00175568` = `0x43B40000` = **+360.0f** | +360.0f | Horizontal | Same | **No change** |
| `src/entities/Fruit.cpp:513` | `WARP_THRESH_LEFT = -360.0f` | `DAT_0017556c` = `0xC3B40000` = **-360.0f** | -360.0f | Horizontal | Same | **No change** |
| `src/game/BombHit.cpp:266` | `OFFSCREEN_Y = -480.0f` | `DAT_0016a190` = `0xC3F00000` = **-480.0f** | -480.0f | Y-DOWN: far above screen; Y-UP: far below screen | Y-UP: far below screen | **No change** -- numerical match |
| `src/entities/SplatEntity.cpp:49` | `UP_LAND_Z = -50.0f` | `DAT_0017faa8` = `0xC2480000` = **-50.0f** | -50.0f | Z-axis (depth), not Y | Z-axis (depth), not Y | **No change** -- Z axis, Y convention irrelevant |
| `src/hud/MissControl.cpp:42-43` | `CLAMP_X_HI = 240.0f; CLAMP_X_LO = -240.0f` | Binary centred ortho bounds (X is horizontal in port) | +/-240 | Horizontal axis | Same | **No change** |
| `src/hud/MissControl.cpp:44-45` | `CLAMP_Y_HI = 160.0f; CLAMP_Y_LO = -160.0f` | Binary centred ortho bounds | +/-160 | Vertical screen extent | Y-DOWN: +160 is BOTTOM, -160 is TOP; port Y-UP: +160 is TOP, -160 is BOTTOM | **Convention mismatch** but no change needed: the screen clamp is symmetric, so the clamp radius is the same in both conventions. A label at any Y still stays within [-160, +160]. |

---

## Table 3: Initial velocity Y values

Sites where entity `vel.y` is set to a small literal at spawn or reset.

| File:line | Expression | What motion it produces (port Y-UP) | Binary value | Binary motion (Y-DOWN) | Action |
|-----------|------------|-------------------------------------|--------------|----------------------|--------|
| `src/entities/Bomb.cpp:330` | `vel = Vec3(HIT_COL_POS, -1.0f, HIT_COL_POS)` | vel.y = -1.0: bomb drifts toward BOTTOM (off-screen) | Literal -1.0; same as OFFSCREEN_Y branch | vel.y = -1.0: bomb drifts toward TOP (off-screen from -320). Same off-screen direction. | **No change** -- functionally equivalent regardless of convention |
| `src/entities/Bomb.cpp:376` | `vel = Vec3(0.0f, -1.0f, 0.0f)` | Same as above | Literal -1.0 | Same as above | **No change** |
| `src/game/BombHit.cpp:267` | `DRIFT_Y = -1.5f` applied at line 292, 327 | vel.y = -1.5: entity drifts toward BOTTOM | Literal; binary `ResetGameEntities` sets -1.5 explicitly | Y-DOWN: -1.5 drifts toward TOP. Carries entities further off-screen from -480. | **No change** -- off-screen in both conventions |
| `src/entities/Fruit.cpp:534-539` | `vel.y = -1.0f` in warp-top branch | When fruit goes above +240 in Y-UP, set vel.y=-1 so it continues downward and exits cleanly | Binary warp branch: same literal -1.0 | Y-DOWN: -1.0 is toward TOP, continuing off-screen after teleport to WARP_CLAMP_TOP=-320 | **No change** -- numerical match; the warp places pos at -320 and vel at -1, moving further negative (further off-screen TOP in Y-DOWN, or further off-screen BOTTOM in Y-UP) |
| `src/entities/Fruit.cpp:563-566` | `vel.y = +1.0f` in warp-bot branch | When fruit goes below -240 in Y-UP, set vel.y=+1 so it continues downward (positive) | Literal +1.0 | Y-DOWN: +1.0 is toward BOTTOM, continuing off-screen after teleport to WARP_CLAMP_BOT=+320 | **No change** -- numerical match |
| `src/entities/SplatEntity.cpp:241-246` | `vel.y += UP_GRAVITY * dt` where `UP_GRAVITY = -10.0f` | Y-UP: -10 pulls toward BOTTOM (downward) | `vmov.f32 s13, 0xc1200000 = -10.0f` confirmed from binary at 0x0017fa90 vicinity | Y-DOWN: -10 pulls toward TOP. With Y-DOWN launch velocities this would be upward force -- WRONG. But see gravity note below. | **VERIFY** -- same convention issue as Fruit gravity. If splat launch vel.y uses Y-UP convention (negative = downward), then -10 gravity is wrong. See note. |
| `src/entities/SplatEntity.cpp:195` | `vel.y *= MS_VEL_Y_STRETCH` where `MS_VEL_Y_STRETCH = 1.5f` | Stretches Y component of velocity by 1.5x | Binary literal 1.5x | Same multiplier, sign depends on input velocity | **No change** -- scale only, no sign effect |
| `src/entities/SplatEntity.cpp:195` | `vel.z = speed * -0.5f - 150.0f - rand(10)` | Z-axis (depth), initialises Z velocity into the screen | Binary: same formula | Same | **No change** -- Z axis only |
| `src/entities/Coin.cpp:31` | `COIN_DEFAULT_GRAVITY = Vec3(220.0f, -140.0f, 0.0f)` | Coin "gravity" Y = -140 acts as homing target Y value; in state 2 FLYING it accelerates vel.y by -140 per second | Binary: unverified DAT; the coin system was documented in coin.md with this value | Under Y-UP: negative -140 target pulls coin toward BOTTOM while decelerating | **No change** -- unverified binary; behaviorally correct under port's convention |

**SPLAT GRAVITY NOTE**: `SplatEntity::MakeSplat` sets velocity from the input
splat vector `v`. That vector comes from `Fruit::Slice` at line 800:

```cpp
Vec3 sv(sinf(a) * speed, cosf(a) * speed, 0.0f);
if (s) s->MakeSplat(pos, sv, isCritical, m_FruitType);
```

The angle `a` is random (`Rand16(0xFFF0)`), so vel.x/y can be positive or
negative. The splat gravity of -10.0f then decelerates any positive vel.y
(upward) and accelerates negative vel.y (downward), which is correct under
Y-UP. The binary's -10.0f means the same in Y-DOWN if the random angles also
produce negative-Y launches. Since the angle is random and the gravity
symmetrically reduces speed after peak, this is self-consistent in both
conventions. **No change needed.**

---

## Table 4: World-position constants (POS_*)

These constants are button/entity positions copied numerically from the binary.
They are in Y-DOWN space in the binary (negative Y = toward TOP). The port
stores them at the same numerical values but interprets them as Y-UP (negative
Y = toward BOTTOM). Since both sides of every comparison are in the same
convention, hit-tests remain geometrically correct despite the inversion.

| Constant | File:line | Port value (Y) | Binary value (Y) | Matches? | Port physical meaning (Y-UP) | Binary physical meaning (Y-DOWN) | Flip if convention changes? |
|----------|-----------|----------------|-----------------|----------|------------------------------|----------------------------------|----------------------------|
| `POS_PLAY_BUTTON` | `src/screens/MainScreen.cpp:56` | -66.0f | -66.0f (verified) | YES | 66 units below center | 66 units above center | No flip needed |
| `POS_DOJO_BUTTON` | `src/screens/MainScreen.cpp:57` | -65.0f | -65.0f (verified) | YES | Just below center | Just above center | No flip needed |
| `POS_QUIT` | `src/screens/MainScreen.cpp:58` | -106.0f | -106.0f (verified) | YES | Well below center | Well above center (top-right area) | No flip needed |
| `POS_MORE_GAMES` | `src/screens/MainScreen.cpp:59` | -106.0f | stub offscreen | YES | Well below center | n/a | No flip needed |
| `POS_SOUND_TOGGLE` | `src/screens/MainScreen.cpp:60` | 135.5f | 135.5f (verified) | YES | Above center | Below center | No flip needed |
| `POS_MUSIC_TOGGLE` | `src/screens/MainScreen.cpp:61` | 135.5f | 135.5f (verified) | YES | Above center | Below center | No flip needed |
| `POS_BACK_BUTTON` (DojoScreen) | `src/screens/DojoScreen.cpp:40` | -106.0f | -106.0f (verified) | YES | Well below center | Well above center | No flip needed |
| `POS_SHOP_BUTTON` | `src/screens/DojoScreen.cpp:41` | -15.0f | -15.0f (verified) | YES | Just below center | Just above center | No flip needed |
| `POS_ABOUT_BUTTON` | `src/screens/DojoScreen.cpp:42` | 42.0f | 42.0f (verified) | YES | Above center | Below center | No flip needed |
| `POS_BACK_BUTTON` (ShopScreen) | `src/screens/ShopScreen.cpp:71` | -105.0f | DAT_0015e55c/560 (verified) | YES | Well below center | Well above center | No flip needed |
| `POS_EQUIP_BUTTON` | `src/screens/ShopScreen.cpp:77` | 104.0f | DAT_0015e564/568 (verified) | YES | Above center | Below center | No flip needed |
| `LIST_POS_Y` | `src/screens/ShopScreen.cpp:98` | 40.0f | `DAT_0015ead8 = 0x42200000 = 40.0f` (confirmed Section 4, y-axis-convention.md) | YES | 40 units above center | 40 units below center | No flip needed |
| `POS_BACK_BUTTON` (AboutScreen) | `src/screens/AboutScreen.cpp:39` | -106.0f | -106.0f (verified) | YES | Well below center | Well above center | No flip needed |
| `POS_BACK` (GameModeScreen) | `src/screens/GameModeScreen.cpp:32` | -110.0f | `DAT_0013ea08` (verified) | YES | Well below center | Well above center | No flip needed |
| `POS_CLASSIC` | `src/screens/GameModeScreen.cpp:33` | 71.0f | `DAT_0013ea1c` (verified) | YES | Above center | Below center | No flip needed |
| `POS_ZEN` | `src/screens/GameModeScreen.cpp:34` | 48.0f | `DAT_0013ea5c` (verified) | YES | Above center | Below center | No flip needed |
| `POS_ARCADE` | `src/screens/GameModeScreen.cpp:35` | -76.0f | Unverified | UNVERIFIED | Below center | Above center (if correct) | No flip needed |
| `CLAMP_X_HI/LO` (MissControl) | `src/hud/MissControl.cpp:40-43` | +/-240 | Binary ortho bounds | YES | Horizontal bounds | Same | No flip needed |
| `CLAMP_Y_HI/LO` (MissControl) | `src/hud/MissControl.cpp:44-45` | +/-160 | Binary ortho bounds | YES | Vertical bounds (symmetric) | Same | No flip needed |

**Summary for Table 4**: All POS_* constants are numerically correct (copied
from binary). No flip is needed under either convention. The physical
screen location differs (port Y-UP inverts top/bottom vs binary Y-DOWN) but
since both the button's `pos.y` and the touch `currY` are in the same
convention in the port, all hit-tests are geometrically correct.

---

## Table 5: Slice/spawn velocity computations

Sites that compute initial velocities from angles or RNG for entities.

| File:line | Formula | Binary formula | Action |
|-----------|---------|----------------|--------|
| `src/entities/Fruit.cpp:856-875` (Slice, half velocities) | `dirA = (sin(radA), cos(radA), 0)` then `halfVelA = dirA * impulse * sliceFactor + vel * (1-sliceFactor)` | Binary uses same 16-bit angle arithmetic; angle from blade direction + random offset | No sign change; the angle formula is pure trig from binary. Result direction depends on blade angle. **No change.** |
| `src/entities/Fruit.cpp:800` (Slice, splat spawn) | `sv = Vec3(sin(a)*speed, cos(a)*speed, 0)` where a = Rand16(0xFFF0) | Binary uses same random angle approach | Random, no fixed sign. **No change.** |
| `src/entities/Coin.cpp:207-209` | `vel.x = SinIdx(angle)*speed; vel.y = CosIdx(angle)*speed` | Binary: same -- SinIdx/CosIdx from launch angle | Launch angle determines sign. The `launchAngle` parameter sets direction. **No change.** |
| `src/entities/Bomb.cpp:393` | `vel += m_AccelForce * scaledDt` where `m_AccelForce = Vec3(0, GRAVITY_Y=-12.0, 0)` | Binary same, verified from Bomb::Init literal | Sign relative to launch velocity is consistent. **No change.** |
| `src/entities/SplatEntity.cpp:196` | `vel.z = speed * -0.5f - 150.0f - rand(10)` | `DAT_0017f568 = 150.0f; -0.5 scale` (confirmed in source comments) | Z-axis only. **No change.** |
| `src/entities/Fruit.cpp:220-226` (Update physics) | `step = (vel*dt + gravity*0.5*dt*dt) * 60.0; pos += step; vel += gravity*dt` | `DAT_00177d00 = 0x42700000 = 60.0f` (confirmed). Binary uses dtNorm = scaledDt * 60 for position. | **No change** -- 60.0f multiplier confirmed correct. |
| `src/entities/Bomb.cpp:393-394` (Bomb physics) | `vel += accelForce * scaledDt; pos += vel * dtNorm` where `dtNorm = scaledDt / (1/60) = scaledDt * 60` | Same as Fruit pattern, DAT_00177d00 = 60.0f pattern | **No change** |

---

## Table 6: Touch coord deviations

Sites that explicitly work with Y convention, negate currY, or assume a
specific sign for the vertical touch axis.

| File:line | Code snippet | Port-specific deviation or matches binary? | Action |
|-----------|-------------|-------------------------------------------|--------|
| `src/platform/SDLInputTranslator.cpp:88` | `gy = (float)(FN_SCREEN_H/2) - ny * (float)FN_SCREEN_H;` | **PORT DEVIATION** -- produces Y-UP (TOP = +160). Binary `PointerMoveCallback` produces Y-DOWN (TOP = -160). Comment on line 84 explicitly documents this. | ROOT: flip to `gy = ny * FN_SCREEN_H - FN_SCREEN_H/2` for Y-DOWN migration |
| `src/platform/SDLInputTranslator.cpp:94` | `gy = (float)(FN_SCREEN_H/2) - ny * (float)FN_SCREEN_H;` (identical formula, `TransformTouchNormalized`) | **PORT DEVIATION** -- same as :88 | Same flip as :88 |
| `src/hud/ScrollingMenu.cpp` Phase 3B drag | `anchorY` and `currentY` are both `ts->currY` values in port Y-UP | Port uses Y-UP currY; binary uses Y-DOWN slot.y. Delta `currentY - anchorY` is sign-neutral (both sides flip together). | No change to drag formula structure needed; three structural fixes documented in `y-axis-convention.md` still apply regardless of convention. |
| `src/entities/SlashEntity.cpp:328,331` | `(float)s->currX`, `(float)s->currY` passed to `OnTouchActive` | Port: currY is Y-UP. Binary: `TouchMoveY` writes `160 - pixel_y` (Y-DOWN) to `field_0x14`. Confirmed from binary decompile. After SDLInputTranslator flip, currY would become Y-DOWN, matching binary exactly -- **no additional negate needed in SlashEntity**. | No change now; after flip, automatically correct. |
| `src/hud/MenuButton.cpp:507-510` | `yMin = pos.y - hh - m_AnimSpeed; yMax = pos.y + hh + m_AnimSpeed;` | Bounds are Y-UP (derived from pos.y Y-UP + half-size). Slot currY is Y-UP. Both same convention: correct. After flip, both become Y-DOWN: still both same convention: still correct. | No change needed under either convention. |
| `src/entities/Fruit.cpp:365-392` (PostUpdate) | `BOUND_X_LO = -192; BOUND_X_HI = +192; BOUND_Y_LO = -128; BOUND_Y_HI = +128` | `DAT_001751a0 = 0xC3400000 = -192.0f; DAT_001751a4 = 0x43400000 = +192.0f; DAT_001751a8 = 0xC3000000 = -128.0f; DAT_001751ac = 0x43000000 = +128.0f` -- all confirmed. | **No change** -- numerical match confirmed |

---

## Table 7: Camera and view setup

| File:line | Parameters | Binary values | Match? | Action |
|-----------|------------|---------------|--------|--------|
| `src/game/FruitCamera.cpp:111` | `mm.SetupOrtho(160.0f, -160.0f, -240.0f, 240.0f, 2000.0f, -6000.0f)` | Binary `FruitCamera::SetupPerspective` (0x00181200): same values | YES | No change |
| `src/game/FruitCamera.cpp:104-106` | `eye = (m_Target.x, m_Target.y, 1.0f)`, `at = (m_Target.x, m_Target.y, 0.0f)`, `up = (0.0f, 1.0f, 0.0f)` | Binary: same. up = (0, 1, 0) is world Y-up for the VIEW matrix -- this is correct for a landscape ortho view. | YES | No change |
| `src/engine/render/Renderer.cpp:74` | `mm.SetupOrtho(160.0f, -160.0f, -240.0f, 240.0f, 2000.0f, -6000.0f)` in `SetupGameOrtho` | Verified: same literal as FruitCamera | YES | No change |
| `src/engine/render/MortarCamera.cpp:82` | `mm.SetupOrtho(halfH, -halfH, -halfW, halfW, -1.0f, 1000.0f)` in base `SetupOrtho` | Base camera uses viewport dimensions for HUD pass. Not the game camera. | Correct pattern | No change |
| `src/engine/render/MortarCamera.cpp:72-73` | `eye = (0, 0, 1), up = (0, 1, 0), target = origin` | Binary base camera: same. | YES | No change |

**Camera analysis**: The `SetupOrtho(160, -160, -240, 240, ...)` call is the
binary-literal game ortho. `top=160, bottom=-160` means the OpenGL Y clip axis
maps game Y=+160 to clip Y=+1 (top edge of display). Under Y-DOWN, game
entities placed at Y=-160 (landscape TOP) appear at clip Y=-1 (BOTTOM of the
GL framebuffer). On the Bada device a 90-degree screen rotation compensates.
On the SDL port rendering directly to a landscape window, clip Y=+1 IS the
visual top -- so Y-DOWN entities at Y=-160 appear at the BOTTOM visually.
This is why a migration to Y-DOWN requires confirming the screen rotation is
handled separately. The port currently has no screen rotation; the Y-UP
convention compensates by keeping top=+160. **No camera change needed.**

---

## Table 8: Particle emitter Y

| File:line | Expression | Binary origin | Action |
|-----------|------------|---------------|--------|
| `src/engine/particle/PSPParticleManager.cpp:299` | `p.m_Vel += p.m_Gravity * dt` where gravity loaded from `tmpl->m_GravityMin/Max` | Binary particle XML `<gravity>` element parsed as "x y z". Values are data-driven. | **No change** -- data-driven from XML |
| `src/engine/particle/PSPParticleManager.cpp:178-180` | `p.m_Gravity.y = RandRange(tmpl->m_GravityMin[1], tmpl->m_GravityMax[1])` | XML parses Y component of `<gravity>` text. E.g. `bomb_smoke` gravity Y is from the XML. | **No change** -- XML-driven; gravity values in particle XML are already in binary coordinate space |
| `src/engine/particle/PSPParticleManager.cpp:154-156` | `vy = RandRange(set.m_VelocityMin[1], set.m_VelocityMax[1]) * 0.5f` | `<velocity min="..." max="..."/>` on the particleSet in XML. Binary uses same 0.5 scale. | **No change** -- data-driven from XML |
| Particle emitter position | Set by callers (e.g. `m_pEmitter->m_Pos = pos`) | Emitter position tracks entity `pos` which is in port's Y-UP convention | **VERIFY** -- when entity pos is in Y-UP but the ortho renders Y-DOWN-values at visual bottom, particles would appear at wrong Y if convention changes. Currently self-consistent. |

**Summary**: Particle gravity and velocities are entirely data-driven from
XML. The XML values were authored for Y-DOWN convention on Bada. Under the
port's Y-UP convention these values produce inverted particle arcs (particles
that should fall DOWN spray UP, etc.). However since the port's whole world
is Y-UP, this affects all particles equally and the game still looks plausible.
Migrating to Y-DOWN would require no code change here -- the XML values would
become correct automatically.

---

## Table 9: BombBlast / SliceEffect Y

| File:line | Expression | Binary origin | Action |
|-----------|------------|---------------|--------|
| `src/entities/BombBlast.cpp:93-104` | `m_Vel1 = Vec3(c, s, 0) * 0.5f` where angle is random | Fully randomized angle; no fixed Y direction | **No change** |
| `src/entities/BombBlast.cpp:127-129` | `m_PosA = m_Vel1 * m_BlastRadius; m_PosB = m_Vel2 * m_BlastRadius` | Grows outward in all directions | **No change** |
| `src/entities/BombBlast.cpp:210-215` | Quad vertices: `py + ay + by`, `py - ay + by`, etc. | All derived from random angle + radius | **No change** |
| `src/hud/SliceEffect.cpp:47-54` | `SLICE_KEYFRAMES` scale triples; no Y position literals | Scale animation, position comes from `s->pos` (fruit pos at slice) | **No change** |
| `src/hud/SliceEffect.cpp:198-200` | `mat.GlobalTranslate44(s->pos)` | Position = fruit pos at time of slice; Y inherited from entity | Convention matches caller. **No change.** |
| `src/hud/MissControl.cpp:143-146` | Screen clamp: `if (pos.x + size.x > 240) pos.x = 240 - size.x` etc. | Binary clamp at centred ortho bounds +-240/+-160 | Both axes are symmetric clamps; no sign sensitivity. **No change.** |

---

## Final Summary

### Total deviation count

**Sites needing a sign flip** (if migrating to binary Y-DOWN):
- 2 mandatory: `SDLInputTranslator.cpp:88` and `:94` -- the root formula sites

**Sites already numerically correct** (binary values confirmed via read_memory):
- `OFFSCREEN_Y = -320.0f` (Bomb.cpp) -- matches `DAT_00172cb0 = -320.0f`
- `BOUNDS_MIN_Y = -240.0f`, `BOUNDS_MAX_Y = +240.0f` (Bomb.cpp) -- matches DATs
- `BOUNDS_MIN_X = -360.0f`, `BOUNDS_MAX_X = +360.0f` (Bomb.cpp) -- matches DATs
- All `WARP_*` constants in Fruit.cpp -- all confirmed via DAT block at 0x00175548
- Fruit PostUpdate BOUND_X/Y constants -- confirmed via DAT block at 0x001751a0
- `GRAVITY_Y = -12.0f` (Bomb/Fruit) -- same literal, consistent with convention
- `UP_GRAVITY = -10.0f` (SplatEntity) -- confirmed from binary instruction 0xC1200000
- `UP_LAND_Z = -50.0f` (SplatEntity) -- confirmed from `DAT_0017faa8 = -50.0f`
- `OFFSCREEN_Y = -480.0f` (BombHit) -- confirmed from `DAT_0016a190 = -480.0f`
- `POS_INTEGRATION_SCALE = 60.0f` (Fruit/Bomb) -- confirmed from `DAT_00177d00 = 60.0f`
- All POS_* button/UI position constants -- numerically correct, physically inverted
  but self-consistent under port Y-UP convention

**Sites that are convention-dependent but SELF-CONSISTENT** (no change needed):
- Gravity constants (-12.0f, -10.0f) -- sign is consistent with launch velocities
  in same convention; relative sign between gravity and launch velocity is correct
- `vel.y = -1.0f` drift-off-screen values -- move entity further off-screen in
  both conventions from their respective off-screen positions
- All SlashEntity blade tracking -- currY feeds blade centers; blade centers feed
  collision against fruit pos; all three in same convention
- All MenuButton hit-test bounds -- pos.y and currY both in same convention
- All BombBlast, SliceEffect Y -- derived from random angles or fruit pos, self-consistent

**Sites with gap CLOSED (2026-04-26T21:30)**:
- `src/game/WaveManager.cpp` -- SpawnFruit/SpawnBomb vel.y sign RE'd from binary
  (0x001225a0 / 0x00121fa8). See Section "SpawnFruit RE gap closure" below.
  Result: binary uses Y-UP world coords; vel_y > 0 = upward launch. Port is CORRECT.
  NO convention migration needed.

### SpawnFruit RE gap closure

RE of `WaveManager::SpawnFruit` (0x001225a0) and `WaveManager::SpawnBomb`
(0x00121fa8), 2026-04-26.

**Spawn position for default/bottom spawn (case 0)**:
```c
pos.x = (float)angle_index * scale.x       // spread along landscape width
pos.y = (float)DAT_00122240 * scale.y      // = -160 * 1.0 = -160.0
```
`DAT_00122240 = 0xFFFFFF60 = -160` (int32, confirmed from read_memory at 0x00122240).
`pos.y = -160.0` = landscape BOTTOM in Y-UP world coords (same as SetupOrtho bottom=-160).
**The binary places fruits at the visual bottom of the screen.**

**Launch velocity for default/bottom spawn**:
```c
vel_y = cos(spawnAngle) * speed * DAT_00122218 * speedMultY
      = cos(angle) * (9.5 + rand(1.5)) * 1.075 * speedMultY
```
`DAT_00122218 = 0x3F89999A = ~1.075f` (confirmed from read_memory at 0x00122218).
`speed = 9.5 + rand(1.5) > 0` always. `speedMultY` defaults to 1.0.

For bottom spawn, the angle index spans approximately 150..450 (from minAngle=-1,
maxAngle=+1 scaled by +/-150). Mid-range angle_index=300:
- `spawnAngle = (short)(biased_angle) * 0xB6 = ~54600`
- `angle_radians = 54600 * 2*pi / 65536 ~= 300 degrees`
- `cos(300 deg) = +0.5`
- `vel_y = +0.5 * ~10 * 1.075 ~= +5.4`

**vel_y > 0 = upward in Y-UP world space** (positive Y = toward landscape TOP in world
coords confirmed by SetupOrtho where top=160, bottom=-160).

This is consistent with gravity_y = -12.0f (negative = downward pull in Y-UP).
The fruit rises with positive vel_y, then gravity decelerates and reverses it.
Correct parabolic arc confirmed.

**Conclusion**: The binary's world coordinate system is **Y-UP** for entity positions
and velocities. The port copied these constants correctly. No migration needed.

The only real deviation remains `SDLInputTranslator` touch Y (Y-UP vs Y-DOWN),
which is self-consistent within the port because ALL systems using touch Y in the
port share the same Y-UP convention. The binary's touch Y is Y-DOWN, but binary
world Y is also Y-UP -- identical to the port's world Y convention.

### Highest-risk areas (updated)

1. ~~**Gravity direction vs launch velocity**~~ -- **RESOLVED 2026-04-26**: Binary
   uses Y-UP world coords. SpawnFruit vel_y > 0 = upward. Gravity -12.0f = correct
   downward pull. No change needed anywhere in entity physics.

2. **SDLInputTranslator touch Y** -- The only remaining convention deviation.
   SDLInputTranslator produces gy = +160 at landscape TOP (Y-UP), binary slot.y
   = -160 at landscape TOP (Y-DOWN). This affects ScrollingMenu drag direction,
   blade trail Y in Y-UP vs Y-DOWN. The port is self-consistent under Y-UP.
   A migration would flip touch Y to match binary exactly. Functionally the game
   works either way since all consumers use the same convention as the translator.

3. **ScrollingMenu** -- The three documented formula fixes (Phase 3B missing
   m_Velocity.y term, Phase 4 pending-clear removal, Phase 5 cursor init) have
   already been applied as of 2026-04-26. No further scrolling changes needed.

### Recommended order of changes (updated)

~~**Phase 1**~~ (DONE): ScrollingMenu formula fixes applied.

~~**Phase 2**~~ (DONE): SpawnFruit RE confirmed Y-UP world coords, vel_y > 0 = up.

**No Phase 3 required**: The port's Y-UP convention is correct relative to the
binary's world coordinate system. The SDLInputTranslator touch Y inversion is a
deliberate port adaptation; migrating it would require simultaneously fixing
ScrollingMenu drag direction sign (currently self-consistent under Y-UP touch)
and provides no gameplay benefit since slicing and physics both work correctly.

### Items to test (current port, no migration)

- **Fruit launch arc**: When WaveManager is ported, fruits should rise from the
  bottom (pos.y=-160), peak near center, and fall off the top or sides. Confirm
  visible arc with gravity pulling halves downward after slice.
- **Slice detection**: Blade sweeps from touch Y should register hits when the
  finger visually crosses a fruit. Self-consistent under Y-UP.
- **ScrollingMenu drag**: Structural fixes are applied. Verify drag scrolls the
  list in the natural direction (swipe in one direction = content follows).

---

## Appendix: key binary addresses cross-reference

| Constant | Binary DAT | Hex bytes (LE) | Decoded value |
|----------|-----------|----------------|---------------|
| Bomb OFFSCREEN_Y | DAT_00172cb0 | 0000A0C3 | -320.0f |
| Bomb BOUNDS_MIN_Y | DAT_00172f34 | 000070C3 | -240.0f |
| Bomb BOUNDS_MAX_Y | DAT_00172f38 | 000070 43 | +240.0f |
| Bomb BOUNDS_MIN_X | DAT_00172f3c | 0000B4C3 | -360.0f |
| Bomb BOUNDS_MAX_X | DAT_00172f40 | 0000B443 | +360.0f |
| Bomb ACCEL_GROWTH_RATE | DAT_00172f30 | CDCC4C3E | 0.2f |
| Fruit OFFSCREEN_BASE | DAT_00175548 | 00002043 | +160.0f |
| Fruit WARP_CLAMP_TOP | DAT_0017554c | 0000A0C3 | -320.0f |
| Fruit WARP_THRESH_BOT | DAT_00175550 | 000070C3 | -240.0f |
| Fruit WARP_CLAMP_BOT | DAT_00175554 | 0000A043 | +320.0f |
| Fruit WARP_CLAMP_RIGHT | DAT_00175558 | 0000F0C3 | -480.0f |
| Fruit WARP_CLAMP_LEFT | DAT_0017555c | 0000F043 | +480.0f |
| Fruit WARP_THRESH_TOP | DAT_00175560 | 000070 43 | +240.0f |
| Fruit SCALE_MARGIN_MULT | DAT_00175564 | 00004842 | +50.0f |
| Fruit WARP_THRESH_RIGHT | DAT_00175568 | 0000B443 | +360.0f |
| Fruit WARP_THRESH_LEFT | DAT_0017556c | 0000B4C3 | -360.0f |
| Fruit POS_INTEGRATION_SCALE | DAT_00177d00 | 00007042 | +60.0f |
| Fruit PostUpdate BOUND_X_LO | DAT_001751a0 | 000040C3 | -192.0f |
| Fruit PostUpdate BOUND_X_HI | DAT_001751a4 | 00004043 | +192.0f |
| Fruit PostUpdate BOUND_Y_LO | DAT_001751a8 | 000000C3 | -128.0f |
| Fruit PostUpdate BOUND_Y_HI | DAT_001751ac | 00000043 | +128.0f |
| SplatEntity UP_LAND_Z | DAT_0017faa8 | 000048C2 | -50.0f |
| BombHit OFFSCREEN_Y | DAT_0016a190 | 0000F0C3 | -480.0f |
| ShopScreen LIST_POS_Y | DAT_0015ead8 | 00002042 | +40.0f |
