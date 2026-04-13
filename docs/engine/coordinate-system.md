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

The Bada physical device is 480×800 portrait. The game runs in landscape (rotated 90°). To keep the ortho math clean, the binary multiplies the projection by a static rotation matrix set **once** per run by `BeginFrame` at `DisplayManager + 0x54`:

```
R = [  0  1  0  0 ]      col0 = (0, -1, 0, 0)   // output X basis
    [ -1  0  0  0 ]      col1 = (1,  0, 0, 0)   // output Y basis
    [  0  0  1  0 ]
    [  0  0  0  1 ]
```

This is a 90° CW rotation. A vertex `(vx, vy)` is transformed to `(vy, -vx)`.

`MatrixManager::_UploadCurrentMatrices` at `0x0019e2b4` multiplies `m_projection × R` before `glLoadMatrixf`, so the rotation is baked into the projection matrix the GPU sees.

### What the rotation does to the ortho

After composition, the effective rendered extents map:

| Stored game axis | After rotation | Ortho range | Maps to NDC | Orientation on landscape device |
|---|---|---|---|---|
| `Y` (±240) | becomes screen X | `[-240, 240]` | `[-1, +1]` | horizontal (long edge) |
| `X` (±160) | becomes screen -Y | `[-160, 160]` | `[-1, +1]` (inverted) | vertical   (short edge) |

So **game X is the short axis, game Y is the long axis**. Play button at `(16, -66)` means X=16 (slightly off vertical centre), Y=-66 (left of horizontal centre). This is consistent with the binary comments like `"Play button at (16, -66)"` in port sources.

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

## See Also

- [`camera.md`](camera.md) — `MortarCamera` / `FruitCamera` struct layouts and method addresses
- [`rendering-functions.md`](rendering-functions.md) — `HUDControl3d::Draw` full pseudocode
- [`display-manager.md`](display-manager.md) — `BeginFrame`, `m_ScreenRotationMatrix`
- Port: `src/game/FruitCamera.cpp` — `SetupPerspective`
- Port: `src/hud/HUDControl3d.cpp` — the dead `(480, 320) * hudScale` offset
- Port: `src/screens/MainScreen.cpp` — `SetupQuadMatrix`, `UpdateScreenElements` (values need audit)
