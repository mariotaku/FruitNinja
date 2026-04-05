# Rendering Pipeline

## Coordinate System

The game renders in landscape on a portrait Bada device (480×800 physical). The ortho projection (from `FruitCamera::SetupPerspective` mode 0, verified via `read_memory`):

```
SetupOrtho(left=160.0, right=-160.0, bottom=-240.0, top=240.0, near=2000.0, far=-6000.0)
```

| Axis | Ortho Range | Maps To | Direction |
|------|-------------|---------|-----------|
| **X** | +160 (left) to -160 (right) | Screen vertical (320 units) | **Flipped** — positive X = screen top |
| **Y** | -240 (bottom) to +240 (top) | Screen horizontal (480 units) | Normal — positive Y = screen right |

In landscape orientation (phone held sideways):
- **Game X** = vertical axis (top=+160, bottom=-160), **flipped**
- **Game Y** = horizontal axis (left=-240, right=+240), normal

Touch transform (`GlesForm::TransformTouchPos`, 0x18327c) converts portrait device coords to this space:
```c
game.x = (int)(phys.y * 480.0 / 800.0);       // phys Y → game X
game.y = 319 - (int)(phys.x * 320.0 / 480.0); // phys X → game Y (flipped)
```

Note: Touch coords are in 0-479 (X) / 0-319 (Y) range, NOT in ortho units (±160/±240). UI code uses a mix of both coordinate spaces.

## Rendering Pipeline (GameDraw — 0x16b888, 211 lines)

### Frame Structure

```
GameDraw(dt, active):
  if (!active) → skip main draw, only handle input + final HUD layer
  if (!DisplayManager::IsRenderingAllowed()) → return

  ┌─ SETUP ──────────────────────────────────────────────
  │  Save HUD position
  │  Disable depth buffer (write + test)
  │  Set 3 ambient colours: dark(64,64,64), medium, light
  │  SetGlobalAmbience(dark)
  │  SetLightDirection(Game.worldPos.xy, 0)
  │  FruitCamera::SetupPerspective(cam, 0, false)
  │
  ├─ BACKGROUND ─────────────────────────────────────────
  │  Texture::Set(background_tex)
  │  ResetMatrixStack
  │  if (no camera shake):
  │    Scale44 + Translate → draw background quad (normal)
  │  else:
  │    Scale44 + Translate → draw background quad (offset for shake)
  │  Texture::UnSet
  │  if (!LoadingJob::CanBoot()) → DrawStartFade; return
  │
  ├─ 3D ENTITIES ────────────────────────────────────────
  │  Enable depth buffer (write + test)
  │  SetGlobalAmbience(medium)
  │  ActorManager::Draw()          ← draws all Fruit/Bomb 3D meshes
  │  SetGlobalAmbience(dark)
  │
  ├─ 2D OVERLAYS (layered by HUD bit flags) ─────────────
  │  HUD::BeginDraw(dt)
  │  HUD::Draw(0x40)               ← combo text, score popups
  │  SplatEntity::DrawActiveSplats  ← juice splats on background
  │  Fruit::DrawShadows             ← shadow circles under fruit
  │  SlashEntity::PreDraw           ← blade trail setup
  │  BombBlast::DrawActiveBlasts    ← bomb explosions
  │  BombFlash::DrawActiveFlashes   ← bomb flash overlays
  │  HUD::Draw(0x80)
  │  PSPParticleManager::Draw(-1)   ← all particles (background layer)
  │  Disable depth
  │  SlashEntity::Draw × 16         ← blade trail geometry (loop over all slash entities)
  │  PSPParticleManager::Draw(0)    ← particles (mid layer)
  │  DrawSlices(dt)                 ← slice line effects (animated split lines)
  │  HUD::Draw(0x01)               ← foreground UI
  │  PSPParticleManager::Draw(1)    ← particles (foreground)
  │  WaveManager::Draw(0)           ← wave indicator
  │  HUD::Draw(0x08)
  │
  ├─ POST-PROCESSING ────────────────────────────────────
  │  MainScreen::DrawPostEffects    ← if main screen active
  │  DrawCritHit()                  ← critical hit flash (fast hardware only)
  │  HUD::Draw(0x100)
  │  DrawBombHit()                  ← bomb hit screen flash
  │  HUD::Draw(0x200)
  │  NetworkManager::DrawNews       ← if news overlay active
  │  Restore HUD position
  │  DrawStartFade()                ← loading transition fade
  │
  └─ FINAL UI ───────────────────────────────────────────
     Handle pause toggle input
     HUD::Draw(0x400)              ← topmost: pause menu, dialogs, game over
```

### HUD Layer Flags

| Flag | Layer | Content |
|------|-------|---------|
| 0x01 | Foreground UI | Score display, lives |
| 0x08 | Wave indicator | Wave progress |
| 0x40 | Combo text | MissControl combo popups |
| 0x80 | Mid-layer | General HUD elements |
| 0x100 | Post-crit | After critical hit flash |
| 0x200 | Post-bomb | After bomb hit flash |
| 0x400 | Topmost | Pause menu, game over screen, dialogs |

### Fruit::Draw (0x1791f4, 161 lines)

**Unsliced fruit** (`m_bSliced == 0`):
```
model = fruitModelArray[fruit_type * 0x24 + 0x18]  // SmartPtr<Model>
matrix = Scale44(entity+0x28) × Quaternion::Matrix44Unit(m_Rot1)
pos = entity.pos + offset * scale
matrix = GlobalTranslate44(matrix, pos)
Model::Draw(model, matrix)
```

**Sliced fruit** (`m_bSliced != 0`, 2 halves):
```
for i in [0, 1]:
    halfModel = fruitModelArray[fruit_type * 0x24 + (i+4)*4]
    matrix = Scale44(entity+0x28) × Quaternion::Matrix44Unit(m_Rot[i])
    pos = (i==0) ? entity.pos : field_0xb8  // half A vs half B
    pos.z += field_0x98  // Z depth offset
    matrix = GlobalTranslate44(matrix, pos)
    Model::Draw(halfModel, matrix)
```

**Fruit model array**: 0x24 (36) bytes per fruit type. Each entry contains:
- `+0x08`: SmartPtr — multiplayer colour variant A
- `+0x0c`: SmartPtr — multiplayer colour variant B  
- `+0x10`: SmartPtr — half A model
- `+0x14`: SmartPtr — half B model
- `+0x18`: SmartPtr — whole fruit model
- `+0x1c`: SmartPtr — multiplayer whole variant

### DrawSlices (0x169ac8, 61 lines)

Iterates `List<SliceEffect>`. Each SliceEffect has:
- `+0x00`: float timer (advances at `DAT * speed`)
- `+0x08`: float angle (radians)
- `+0x0c`: Vec3 position
- `+0x18`: int slice_type (0 or 1; selects model variant)

Rendering: Scale by interpolated keyframe → RotZ by angle → Translate to position → `Model::Draw`

### DrawCritHit (0x16b5b4, 72 lines)

Loads a localised texture on first call. Fades based on `Game.crit_timer` with alpha ramp. Draws a full-screen flash quad using `ScaleMatrix → TranslateMatrix → UploadMatrices → DrawQuad`.

### DrawBombHit (0x16b73c)

Similar full-screen flash for bomb hits, with red tint and camera shake position offset.

### Fruit::DrawShadows (0x00178f28, 33 lines)

Static function. Iterates all type-0 entities (fruit) via `ActorManager::GetEntityFirst/Next`. For each live fruit (scale > 0), calls `AddShadow` to append shadow quad vertices into a stack-allocated `QUADCUSTOMVERTEX[18432]` buffer. After iterating, resets matrix stack, sets shadow texture, draws all accumulated quads as a single triangle strip via `Mesh::DrawTriStrip`, then unsets texture.

```
DrawShadows():
  for each fruit in ActorManager(type=0):
    if fruit.scale > 0 → AddShadow(fruit, &vertexBuf, &count)
  MatrixStack::Reset → Upload
  Texture::Set(shadowTex)
  Mesh::DrawTriStrip(vertexBuf, count*6 - 1)
  Texture::UnSet(shadowTex)
```

### DrawStartFade (0x0016ab10, ~45 lines)

Loading/transition fade overlay. Uses the localised texture at task+0xf4. Renders a full-screen quad with animated alpha and brightness based on fade timer (task+0x1c):

- Timer 0.0–0.5: fade in (alpha ramps up, brightness = 0)
- Timer 0.5–1.0: fade out (alpha = full, brightness ramps, scale grows via `fVar3² + 1.0`)
- Colour = `Colour(brightness, brightness, brightness, alpha)` clamped to 0–255
- Uses `FruitCamera::SetupPerspective(cam, 3, true)` for overlay projection

### SlashEntity::InitPoints (0x0017c340, ~40 lines)

Initialises blade trail vertex buffers for a slash entity.

```c
void SlashEntity::InitPoints(long splitPoint) {
    this->m_PointCount = 0;
    this->m_SplitPoint = splitPoint;     // screen divide for multiplayer
    this->m_BladeDir = globalBladeDir;   // default blade direction vector
    
    // Init 3 tail positions to (0,0,0)
    for (int i = 0; i < 3; i++)
        this->m_TailPos[i] = Vec3(0, 0, 0);
    
    // Allocate 2 vertex buffers (left + right edges of blade trail)
    Colour bladeColour = globalColour.PlatformColour();
    for (int side = 0; side < 2; side++) {
        QUADCUSTOMVERTEX* buf = new QUADCUSTOMVERTEX[(splitPoint + 2) * 9];
        this->m_pBuffer[side] = buf;
        for (int j = 0; j < splitPoint; j++) {
            buf[j] = { pos=(0,0,0,0,0), uv=(1.0,0), colour=bladeColour, extra=0 };
        }
    }
}
```

Each buffer has `(splitPoint + 2)` vertices × 0x24 bytes (36 bytes = QUADCUSTOMVERTEX stride). Two buffers = left and right edges of the blade trail polygon.

### MainScreen::DrawPostEffects (0x0014ac94)

**No-op stub** — returns `this` immediately (`bx lr`). Post-effects were not implemented or stripped for this Bada build. Safe to skip in port.

### MainScreen::UpdateScreenElements (0x0014ad3c, 55 lines)

Logo bounce animation for the main menu. Updates the "FRUIT NINJA" logo position with physics-based bounce:
- Bounce velocity (field_0x104) accumulates with gravity (`+= param1 * const`)
- Position rebounds at floor threshold (pos_y + 3.0) with energy loss (`velocity *= -0.25`)
- Settles when `|velocity| < 3.0` and time threshold passed
- Alpha (field_0xe8) lerps toward target at 25% per frame
- Final logo offset = `(0, -17, 0) * 2.0` from base position

### Key Rendering Functions

| Function | Address | Lines | Purpose |
|----------|---------|-------|---------|
| GameDraw | 0x0016b888 | 211 | Main render orchestrator |
| Fruit::Draw | 0x001791f4 | 161 | 3D fruit mesh rendering |
| Fruit::DrawShadows | 0x00178f28 | 33 | Batched shadow tri-strip under all fruit |
| SlashEntity::InitPoints | 0x0017c340 | ~40 | Blade trail vertex buffer allocation |
| SlashEntity::PreDraw | 0x0017e504 | — | Blade trail vertex update |
| SlashEntity::DrawSlice | 0x0017e424 | — | Blade trail geometry render |
| SplatEntity::DrawActiveSplats | 0x00180344 | — | Background juice splats |
| DrawSlices | 0x00169ac8 | 61 | Animated slice line effects |
| DrawCritHit | 0x0016b5b4 | 72 | Critical hit screen flash |
| DrawBombHit | 0x0016b73c | — | Bomb hit screen flash |
| DrawStartFade | 0x0016ab10 | ~45 | Loading/transition fade overlay |
| MainScreen::DrawPostEffects | 0x0014ac94 | 1 | **No-op stub** (skip for port) |
| MainScreen::UpdateScreenElements | 0x0014ad3c | 55 | Logo bounce physics |
| HUD::Draw | 0x00144a90 | — | Layered HUD rendering (bit-flag filtered) |
| HUD::BeginDraw | 0x00144b28 | — | HUD frame begin |

---

## See Also

- [Rendering functions](../engine/rendering-functions.md) -- GameDraw, HUD::Draw
- [Game loop functions](../functions/game-loop.md) -- GameDraw call site
- [Fruit entity](../entities/fruit.md) -- Fruit::Draw
- [Bomb entity](../entities/bomb.md) -- Bomb::Draw
