# Coordinate System

<!-- Analysed: 2026-04-13T16:00 -->

How the binary and the SDL port store, transform, and render positions in screen space. This is the authoritative reference for the `+480/+320` offset hack and its planned removal.

## Binary ortho (canonical)

`FruitCamera::SetupPerspective` (0x00181200) calls:

```
MatrixManager::SetupOrtho(160, -160, -240, 240, 2000, -6000)
```

Param order is `(top, bottom, left, right, near, far)`. Labelled constants at `0x001813d8`:

| Constant | Value | Role |
|---|---|---|
| `ORTHO_X_LEFT`   | 160   | `top`    → GL Y max |
| `ORTHO_X_RIGHT`  | -160  | `bottom` → GL Y min |
| `ORTHO_Y_BOTTOM` | -240  | `left`   → GL X min |
| `ORTHO_Y_TOP`    | 240   | `right`  → GL X max |
| `ORTHO_NEAR`     | 2000  | `near`   |
| `ORTHO_FAR`      | -6000 | `far`    |

Resulting bounds after the ortho transform alone:

| GL axis | Range | Direction | Size |
|---|---|---|---|
| X | `[-240, +240]` | horizontal | 480 units |
| Y | `[-160, +160]` | vertical   | 320 units |

**The ortho is centred at `(0, 0)`** — positions are stored in a centred, symmetric coordinate space.

## The 90° screen-rotation matrix

<!-- Updated: 2026-04-15T18:30 — matches display-manager.md (90° CCW) -->

The Bada physical device is 480×800 portrait. The game runs in landscape (rotated 90°). The binary multiplies the projection by a static rotation matrix set **once** per run by `BeginFrame` at `DisplayManager + 0x54`. Per `display-manager.md`, the matrix is:

```
R = [  0  -1   0   0 ]
    [  1   0   0   0 ]
    [  0   0   1   0 ]
    [  0   0   0   1 ]
```

This is a **90° CCW** rotation around Z. A vertex `(vx, vy)` is transformed to `(-vy, vx)`. (The earlier note in this doc said CW with `(vy, -vx)` — that was wrong.)

`MatrixManager::_UploadCurrentMatrices` at `0x0019e2b4` multiplies `m_projection × R` before `glLoadMatrixf`, so the rotation is baked into the projection matrix the GPU sees.

### Important: stored-position convention

The previous version of this doc claimed game X is the short axis (±160) and game Y is the long axis (±240). **That is wrong** — the empirical port code uses positions like `Quit (182, -106)` where `X=182 > 160`, which is impossible if X were the short axis. The positions stored in MainScreen / DojoScreen / FruitCamera are already in the **post-rotation landscape** convention:

| Game axis | Range | Direction |
|---|---|---|
| `X` | `[-240, +240]` | horizontal (long edge) |
| `Y` | `[-160, +160]` | vertical   (short edge) — `+Y` is up |

The port renders these directly in a landscape ortho (`SetupOrtho(160, -160, -240, 240, 2000, -6000)`) with no rotation matrix applied, and they land in the right visual location.

Examples (verified from port code that displays correctly):

| Element | Stored pos | Visual location |
|---|---|---|
| Play button (`MainScreen`) | `(16, -66)` | slight right of centre, below centre |
| Dojo button | `(-144, -65)` | left of centre, below centre |
| Quit button | `(182, -106)` | far right, below centre |

### When you find a binary value that doesn't fit this convention

Some values dumped from the binary by `re-analyst` will be in the **pre-rotation** (portrait-stored) convention, especially for entities drawn through `BaseScreen::DrawBorders` / `UploadMatrices_Menu` paths that never received the post-rotation RE pass. For these, apply the rotation manually:

- pre-rotation `(vx, vy)` → port `(-vy, vx)` (90° CCW), OR
- empirically tune by negating both axes if the value lands in the opposite corner

The dojo sensei position dumped from `BaseScreen::DrawBorders @ 0x00130230` is one such example — the binary stores `(182, 137)` but the post-rotation visual is bottom-left, requiring the conversion above.

## `HUDControl3d::Draw` and the dead offset

The port author originally interpreted `HUDControl3d::Draw` at `0x0014428c` as adding a constant `(480, 320, 0)` offset. The actual formula is:

```c
drawPos = pos + (HUD_SCREEN_WIDTH, HUD_SCREEN_HEIGHT, HUD_SCREEN_Z) * pivot
//              (480,              320,              0)           × pivot
```

Where `pivot` is a `HUDControl` field (offset +0x14 from base). It is initialised to `(0, 0, 0)` by `CopyGlobalVec3_PauseScreen` during the `HUDControl` constructor. **For every standard HUD control the pivot is zero**, so the offset term multiplies out to `(0, 0, 0)` and `drawPos = pos` directly.

The `(480, 320, 0)` triple is therefore **dead code** for the main menu, shop, dojo, and game-over screens. It only activates when a subclass explicitly sets a non-zero pivot (none found so far).

**Constant pool at `0x001443dc`:**

| Address | Value | Name |
|---|---|---|
| `0x1443dc` | 182.0 | `ROT_SPEED` (Timer→SinIdx) |
| `0x1443e0` | 480.0 | `HUD_SCREEN_WIDTH` |
| `0x1443e4` | 320.0 | `HUD_SCREEN_HEIGHT` |
| `0x1443e8` |   0.0 | `HUD_SCREEN_Z` |

## The port's current (pre-refactor) state

The port shipped with a **port-specific** ortho + matching `+480/+320` translate on every entity draw:

- `FruitCamera::SetupPerspective`: `SetupOrtho(cy+hh, cy-hh, cx-hw, cx+hw)` where `cx = 480, cy = 320` and `hw, hh = window_size/2`. This is a screen-space ortho centred on `(480, 320)` — effectively `[0..960, 0..640]` for a 960×640 window.
- `Fruit::Draw`, `Bomb::Draw`: add `(pos.x + 480, pos.y + 320)` to translate.
- `HUDControl3d::Draw`: adds `Vec3(480 * hudScale.x, 320 * hudScale.y, 0) + pos`.
- `MainScreen::SetupQuadMatrix`: same `+480/+320 × hudScale` offset.
- `GameInit.cpp` background quad: `Translate(480, 320, -5599)`.
- `PSPParticleManager::Draw`: `+480/+320` added to match the rest.

The **intent** was to emulate whatever Bada's EGL pipeline does to make the (480, 320) offset work, but we now know that offset is dead code in the binary — the Bada pipeline doesn't compensate for anything because nothing is being added. The port's `+480/+320` convention is an invented workaround for a misread.

## Why the naive refactor fails

Replacing the port-specific ortho with the binary's literal centred ortho and removing the `+480/+320` offsets **should** work because MenuButton positions like `(182, -106)` already fit the binary's `|X| ≤ 240, |Y| ≤ 160` bounds.

However, `MainScreen` stores **some** positions in a different convention that only worked because of the old offset hack:

```cpp
// src/screens/MainScreen.cpp
m_WindowCenter = 320.0f / 2.0f + 160.0f;  // = 320.0
Vec3 ninjaDrawPos(m_LogoNinjaTextX, m_WindowCenter, field_0x100);
// = (60, 320, 0)
```

`Y = 320` is outside the binary's `[-160, +160]` Y range, so after the refactor the ninja-text logo renders off-screen. This value came from a port-specific formula (`windowHeight/2 + 160`) that assumed a 0-based vertical screen. The binary's actual value is almost certainly different — RE of `MainScreen::UpdateScreenElements` at `0x0014ad3c` is needed to recover it.

Other suspicious MainScreen values:

| Field | Port value | Binary source | Fits binary ortho? |
|---|---|---|---|
| `pos` | `(0, (320 - size.y) * 0.5, 0)` = `(0, 91, 0)` | unclear | Y=91 within [-160, 160] ✅ |
| `size` | `(480, 138, 1)` | from DAT? | size, not pos — always OK |
| `m_WindowCenter` initial | `windowHeight/2 + 160 = 320` | ❌ not RE'd | 320 > 160 ❌ |
| `m_LogoFruitTextPos.x` | `-120` (DAT_0014aed4) | matches binary | within [-240, 240] ✅ |
| `m_LogoFruitTextPos.y` | `pos.y + 18 = 109` | ? | within [-160, 160] ✅ |
| `m_LogoFruitPos` base | `(-175, 26, 0)` (DAT_0014aedc) | matches binary | ✅ |
| `m_LogoFruitPos` offset | `(-120, -17, 0)` | matches binary | ✅ |

Only `m_WindowCenter` is obviously wrong. Other values may also be subtly off once the ortho is changed.

## Plan to finish the refactor

1. RE `MainScreen::UpdateScreenElements` (0x0014ad3c) and the `MainScreen` constructor to recover the binary-accurate initial values for `m_WindowCenter`, `pos`, and the bounce animation state.
2. Decompile `MainScreen::Draw` (0x0014d4ec) to confirm how these fields are consumed — whether there's any per-frame offset subtraction the port is missing.
3. Audit other screens (`DojoScreen`, `ShopScreen`, `GameOverScreen`, `PowerUpShop`, `AboutScreen`) for similar port-specific position formulas.
4. Re-apply the ortho refactor (FruitCamera, Fruit, Bomb, HUDControl3d, GameInit, particle Draw, MainScreen::SetupQuadMatrix) now that position data matches.
5. Test each screen visually after each correction.

The previous refactor attempt (commit `16ea7f2`, reverted as `f93019b`) is the template for step 4.

### A note on the 90° rotation

The Bada binary needs the screen-rotation matrix because its framebuffer is portrait-oriented. The SDL port renders natively to a **landscape** window, so GL's `(top, bottom, left, right)` mapping already puts the binary's axes in the right place on screen. The port does **not** need to apply the rotation matrix — the same stored position value lands in the same visual location on both platforms.

This is why MenuButton positions in the range `(|X| ≤ 240, |Y| ≤ 160)` work directly in a centred-at-0 ortho without any rotation. The 90° rotation is a Bada-specific portrait→landscape framebuffer transform, orthogonal to the coordinate system of the game logic.

> **Caveat — applies to positions only, NOT rotation axes.** See the [Rotation-axis discrepancy](#rotation-axis-discrepancy-2026-04-27) section below.

<!-- Analysed: 2026-04-27T13:50, REVISED 2026-04-27T19:30 -->

## Rotation-axis discrepancy (2026-04-27, revised)

> **Revision note 2026-04-27T19:30**: an earlier draft of this section claimed the binary's `RotX44` / `RotZ44` post-multiply by `Rot_std(−α)` and that the bomb's "wrong axis" was caused by the missing `m_ScreenRotationMatrix` (Bug 3). Both claims were wrong:
>
> 1. **Direct disassembly of `_Matrix44::RotX44/Y44/Z44` shows they all PRE-multiply** the matrix by the standard CCW rotation `Rot_std(+α)`. The earlier re-analyst confused row-iteration with column-iteration in the Ghidra decomp (`data[col][row]` vs `data[row][col]`).
> 2. **`m_ScreenRotationMatrix` is real but irrelevant for the port.** The binary's full pipeline is `R_screen · P_ortho · V · M` rendered into a portrait framebuffer, then displayed via 90° device rotation; the net world-axis-to-user-screen mapping is **identical** to the port's `P_ortho · V · M` rendered directly to a landscape window. So R_screen does not need to be applied in the port.
>
> The remaining real bug is that the port's `RotX44/Y44/Z44` are **post-multiplications**, while the binary's are **pre-multiplications**. For the bomb's `Rx · Ry · Rz` chain this produces a different composed rotation, swapping the visible roles of `m_RotX` and `m_RotY`. Bug 2 (`OrthoW` cell swap) remains valid as documented below; Bug 1 has been re-stated correctly.

### Bug 1 (revised) — `RotX44/Y44/Z44` are pre-multiplications, not post

Binary `_Matrix44::RotX44` @ `0x00172f58` decomp (Ghidra `data[col][row]` indexing):
```c
fVar5..fVar8 = data[0..3][2]   // ROW 2 elements (m[2], m[6], m[10], m[14])
fVar1..fVar4 = data[0..3][1]   // ROW 1 elements
new data[c][2] = fVar(c+5)*cos + fVar(c+1)*sin   // new row 2 = sin*row1 + cos*row2
new data[c][1] = fVar(c+1)*cos - fVar(c+5)*sin   // new row 1 = cos*row1 - sin*row2
```

Iterates over rows (transforming entire rows 1 and 2 across all columns). This is the canonical **pre-multiplication** of the matrix by the standard `RotX_std(+α)`:

```
[ 1   0    0   0 ]
[ 0  cos -sin  0 ]
[ 0  sin  cos  0 ]
[ 0   0    0   1 ]
```

Equivalent disassembly + analysis for `RotY44` @ `0x00172fdc` and `RotZ44` @ `0x00144958` confirms identical pre-multiply convention with standard `RotY_std(+α)` and `RotZ_std(+α)`.

The port (before this revision) iterated over **columns** (transforming columns 1, 2 of the matrix), which is the **post-multiplication** by `Rot_std(±α)`. For an isolated single-rotation call, post-mul is just a different convention; for a chain (bomb's `Rx·Ry·Rz` followed by `Scale` and `Translate`), pre-mul vs post-mul produces **different composed rotations**:

| Convention | Bomb's draw chain composes to |
|---|---|
| Binary (pre-mul) | `M = Rz · Ry · Rx`. Apply to `v`: rotate by Rx first, then Ry, then Rz. |
| Old port (post-mul) | `M = Rx · Ry · Rz` (with mixed sign conventions for `Y`). Apply to `v`: rotate by Rz first, then Ry, then Rx. |

For the bomb's mesh `+Z` (long axis with fuse) at `m_RotX = m_RotY = 0`:

- Binary: `Rx(−90°) · v_+Z = (0, +1, 0)` (long axis points UP world). Then Ry(0)/Rz(0) leave it fixed. With α_Z growing, Rz pinwheels the (0, +1, 0) vector in the world XY plane → visible as in-plane pinwheel of the bomb's silhouette.
- Old port: Rz(α_Z) applied first to mesh +Z is a no-op (Z-rotation fixes Z). Ry(α_Y) sweeps `+Z` to `(sin α_Y, 0, cos α_Y)`. Rx(−90°) on that → `(sin α_Y, cos α_Y, 0)`. So the **long axis sweeps with α_Y (m_RotX) instead of α_Z (m_RotY)** — m_RotX (the slow wobble) drives the pinwheel in the port; m_RotY does nothing visible.

This is the user-reported "rotation around wrong axis": the visible spin is driven by m_RotX in the port (slow wobble) instead of m_RotY (the main spin), so the perceived spin is much slower and tied to a different parameter than the binary intends.

### Fix

Rewrite `RotX44/Y44/Z44` in `src/engine/math/Matrix44.h` to **iterate over columns** (mixing rows 0/1/2 within each column), matching the binary's pre-multiply convention. The signature stays `(sinA, cosA)`. Each function is now equivalent to `M_new = Rot_std(+α) · M_old`.

### Bug 2 — `OrthoW` `m[12]` / `m[13]` formulas are swapped

Binary `_Matrix44::OrthoW` @ `0x0019e7a8` is **standard GL ortho**. Disassembly trace (ARM AAPCS-VFP: s0=top, s1=bottom, s2=left, s3=right):

```
m[0]  = 2 / (right − left)              X scale, uses R/L
m[5]  = 2 / (top − bottom)              Y scale, uses T/B
m[10] = 1 / (far − near)                Z scale
m[12] = −(right + left) / (right−left)  X centring, uses R/L
m[13] = −(top + bottom) / (top−bottom)  Y centring, uses T/B
m[14] = near / (near − far)             Z centring
```

Port `src/engine/math/Matrix44.h:29-30`:
```cpp
out.m[12] = -(top + bottom) * invTB;    // BUG — should pair with R/L using invRL
out.m[13] = -(right + left) * invRL;    // BUG — should pair with T/B using invTB
```

The two centring cells are swapped, **and** each pairs with the wrong inverse factor. Latent: with the symmetric ortho `(160, −160, −240, 240)` both numerators are 0, so today the bug is invisible. Will silently corrupt any non-symmetric ortho call.

### Bug 3 (DISMISSED) — `m_ScreenRotationMatrix` is real but does not need to be applied in the port

Binary `MatrixManager::_UploadCurrentMatrices` @ `0x0019e2b4` left-multiplies the projection by `m_ScreenRotationMatrix` (`DisplayManager+0x54`, set in `DisplayManagerBada::BeginFrame` @ `0x0019dfec`). Direct read of the 16 float writes shows the matrix is `RotZ_std(−90°)` (CW 90° on clip-space `(vx, vy) → (vy, −vx)`) — verified by reading the literal float pattern at `this+0x54..+0x90` (`m[0]=0`, `m[1]=−1`, `m[4]=+1`, `m[5]=0`, identity in Z/W).

However: in the binary, this rotation compensates for the **portrait framebuffer + 90° device rotation** that the user views the game through. The user's net world-axis-to-landscape-pixel mapping is:

- World `+X` → landscape `+X` (right)
- World `+Y` → landscape `+Y` (up)
- World `+Z` → out of screen

In the port, with no R_screen and no portrait-to-landscape device rotation, but rendering directly to a landscape window with the same `P_ortho`, the world-axis-to-landscape-pixel mapping is **identical**. Verified empirically by tracing `Play button at (16, −66)` → landscape pixel `(256, 226)` in both pipelines (binary via R_screen + portrait fb + CCW device rotation; port via direct landscape ortho).

So **applying R_screen in the port (3a) is incorrect** — it adds an extra 90° rotation that the binary does not have in its user view. Confirmed empirically: a brief test commit (reverted as `79763fe`) added R_screen to the port's `SetupOrtho` and the menu rendered visibly rotated 90°.

### Bomb-specific worked example (revised — pre-mul fix)

`Bomb::Draw` chain in the binary (pre-multiply, applied right-to-left): `M = Rz(m_RotY·0xB6) · Ry(m_RotX·0xB6) · Rx(0xBFF4)`, with `0xBFF4 ≈ −90°` and `m_RotX ANGLE_SCALE = 0xB6 = 182` (1° per `m_RotX` unit).

Mesh long axis = local `+Z` (verified from `docs/model_gallery/models.json`: bbox `Z=[−47.4, +78.3]`, fuse tip at `+Z`).

For mesh `+Z = (0, 0, 1)`:

1. `Rx(−90°)`: rotates `+Z → +Y` world (long axis → UP). Confirmed via right-hand rule: `Rx(−α)` rotates `+Z` toward `+Y`.
2. `Ry(α_Y)`: rotates around world Y; long axis `(0, +1, 0)` is fixed (Y-axis rotation fixes the Y component).
3. `Rz(α_Z)`: rotates around world Z; sweeps `(0, +1, 0)` to `(−sin α_Z, cos α_Z, 0)` — pinwheel in world XY plane.

Result: long axis pinwheels with `α_Z = m_RotY·0xB6`. The fuse traces a CCW circle in the screen plane as `m_RotY` accumulates.

**Old port** (post-multiply, before this revision): chain composed as `M = Rx · Ry · Rz` (port's post-mul reverses the effective order). For mesh `+Z`:

1. `Rz(α_Z)`: Z-rotation fixes Z axis → `+Z` stays `+Z`.
2. `Ry(α_Y)`: rotates `+Z` around world Y → `(sin α_Y, 0, cos α_Y)`.
3. `Rx(−90°)`: rotates that → `(sin α_Y, cos α_Y, 0)`.

Result: long axis pinwheels with `α_Y = m_RotX·0xB6` instead. **The port's bomb pinwheels via `m_RotX` (the slow wobble) instead of `m_RotY` (the main spin)** — perceived as much slower spin and tied to the wrong parameter.

### Fix plan (revised)

1. **Rewrite `RotX44/Y44/Z44` to pre-multiply** (`src/engine/math/Matrix44.h:88-128`). Each function iterates over columns mixing rows 0/1/2 (vs old code which iterated over rows mixing columns) → equivalent to `M_new = Rot_std(+α) · M_old`. Restores the binary's effective composition order.

2. **Fix OrthoW m[12] / m[13] swap** (`src/engine/math/Matrix44.h:29-30`). Latent — no visible change today, but binary-faithful and avoids future drift if any caller passes an off-centre ortho. (Already applied.)

3. ~~**R_screen application**~~ — DISMISSED. Port's `P_ortho · V · M` net mapping already matches binary's `R_screen · P_ortho · V · M` (landscape pixel-equivalent) due to the framebuffer / device-rotation cancellation in the binary's user view.

After Fix 1 (pre-multiply rewrite), the bomb's draw chain composes the same way as the binary, including the `m_RotY` → world-Z spin axis. The visible spin axis should then match the binary exactly.

## See Also

- Port: `src/game/FruitCamera.cpp` — `SetupPerspective`
- Port: `src/hud/HUDControl3d.cpp` — the dead `(480, 320) * hudScale` offset
- Port: `src/screens/MainScreen.cpp` — `SetupQuadMatrix`, `UpdateScreenElements` (values need audit)
