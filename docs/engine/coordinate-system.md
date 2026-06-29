# Coordinate System

<!-- Analysed: 2026-06-29T14:30 -->

How the binary and the SDL port store, transform, and render positions in screen space. The port now uses the binary's centered ortho; the `+480/+320` offset hack has been removed.

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

## The port's coordinate-system refactor (complete)

The centered-ortho refactor is **live and essentially complete**:

- `FruitCamera::SetupPerspective` (src/game/FruitCamera.cpp:213) uses the binary's centered ortho: `SetupOrtho(160, -160, -240, 240, 2000, -6000)` — X∈[-240,240], Y∈[-160,160].
- `Fruit::Draw` (Fruit.cpp:875), `Bomb::Draw` (Bomb.cpp:420), and background quad (GameInit.cpp:649 = `(0,0,-5599)`) draw in direct centered world space with no offset added.
- `ShopListItem` and other HUD elements draw at their stored positions directly.
- The `+480/+320` offsets in `HUDControl3d::Draw` (lines 56-62) and `HUDControl::Draw` (line 87) are **dead code** — multiplied by `m_HudScale = (0, 0, 0)`, they vanish.
- `PowerUpShop::Draw` was the final live `+480/+320` remnant; fixed in commit 3a350130 to draw at position directly.

Stored positions in menus/game now match the binary's [-240,240]×[-160,160] bounds faithfully.

## MainScreen coordinate verification

`MainScreen` stores positions faithfully to the binary:

- `pos = (0, 91, 0)` — verified within binary's [-160, 160] Y range.
- `m_BounceY = ninjaH/2 + 160` (commit v1.6.1 @0x00195a58) — intentional **off-screen start** (sprite above window); animated downward each frame via `UpdateScreenElements`.
- Logo and button positions (`m_LogoFruitPos`, `m_LogoFruitTextPos`, menu button coords) all within bounds.

The old port-side `m_WindowCenter = windowHeight/2 + 160` formula has been removed; current implementation is binary-faithful.

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

### Bug 1 (FIXED) — `RotX44/Y44/Z44` pre-multiply

The binary's `_Matrix44::RotX44/Y44/Z44` use **pre-multiplication** by `Rot_std(+α)` (rows 1/2 or 0/1 transformed across columns). The port's code at `src/engine/math/_Matrix44.h:105-136` now correctly pre-multiplies (iterates columns, mixing rows), matching the binary's composition order. 

This fixes the bomb's visible rotation axis: `m_RotY` (not `m_RotX`) now drives the in-plane pinwheel via `Rz`, as the binary intends.

**Worked example:** For bomb mesh `+Z`, binary chain `M = Rz(m_RotY·°) · Ry(m_RotX·°) · Rx(−90°)` (pre-mul): long axis `+Z → Rx → +Y → swept by Rz`, pinwheel ∝ `m_RotY`. Port now matches this composition.

### Bug 2 (FIXED) — `OrthoW` `m[12]` / `m[13]` formulas

The binary's `_Matrix44::OrthoW` @ `0x0019e7a8` follows standard GL ortho: `m[12] = −(right+left)/(right−left)` (pairs with X scale, uses R/L), `m[13] = −(top+bottom)/(top−bottom)` (pairs with Y scale, uses T/B).

Port code at `src/engine/math/_Matrix44.h:45-46` is now correct. (The symmetric ortho `(160, −160, −240, 240)` masked the latent bug: both centring numerators = 0.)

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

### Coordinate-system fixes — status

1. **RotX44/Y44/Z44 pre-multiply** — ✅ Applied. Port now pre-multiplies like the binary; bomb spin axis matches (m_RotY drives pinwheel).
2. **OrthoW m[12] / m[13] pairing** — ✅ Fixed. Standard GL ortho centring formulas correct.
3. ~~**R_screen application**~~ — Dismissed; port's landscape ortho already matches binary's user-visible mapping (framebuffer / device-rotation cancellation).

**Known gap:** PowerUpShop background texture (`g_BuyBg`) is never loaded — instantiation site unresolved. This is a content-loading issue, not coordinate-system related.

## See Also

- Port: `src/game/FruitCamera.cpp` — `SetupPerspective` (centered ortho)
- Port: `src/hud/HUDControl3d.cpp` — the dead `(480, 320) * hudScale` offset
- Port: `src/screens/MainScreen.cpp` — verified faithful to binary
- Port: `src/engine/math/_Matrix44.h` — `RotX44/Y44/Z44` (pre-mul), `OrthoW` (standard GL)
