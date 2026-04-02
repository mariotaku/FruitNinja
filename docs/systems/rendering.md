# Rendering Pipeline

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

### Key Rendering Functions

| Function | Address | Purpose |
|----------|---------|---------|
| GameDraw | 0x0016b888 | Main render orchestrator (211 lines) |
| Fruit::Draw | 0x001791f4 | 3D fruit mesh rendering (161 lines) |
| Fruit::DrawShadows | — | Shadow circles under fruit |
| SlashEntity::PreDraw | 0x0017e504 | Blade trail vertex setup |
| SlashEntity::DrawSlice | 0x0017e424 | Blade trail geometry render |
| SplatEntity::DrawActiveSplats | 0x00180344 | Background juice splats |
| DrawSlices | 0x00169ac8 | Animated slice line effects |
| DrawCritHit | 0x0016b5b4 | Critical hit screen flash |
| DrawBombHit | 0x0016b73c | Bomb hit screen flash |
| DrawStartFade | 0x0016ab10 | Loading/transition fade |
| HUD::Draw | 0x00144a90 | Layered HUD rendering (bit-flag filtered) |
| HUD::BeginDraw | 0x00144b28 | HUD frame begin |

---

## See Also

- [Rendering functions](../functions/rendering.md) -- GameDraw, HUD::Draw
- [Game loop functions](../functions/game-loop.md) -- GameDraw call site
- [Entity structs](../structs/entities.md) -- Fruit::Draw, Bomb::Draw
