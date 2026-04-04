# Gameplay-Adjacent Structs

## Coin : MortarEntity

Bouncing coin spawned on combo rewards. Pool-based via ActorManager (entity type unknown).

### Struct Layout (partial)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | EntityFns* | vtable | |
| +0x0c | byte | flags | |
| +0x10 | float | pos_x/y/z | Entity position |
| +0x1c | float | vel_x/y/z | Entity velocity |
| +0x36 | ushort | angle | Launch angle (from InitCoin param) |
| +0x3c | void* | field1_0x3c | Player index or target |
| +0x40 | int | m_State | 0=waiting, 2=flying |
| +0x44 | float | m_DelayTimer | Negative of param_8; counts up to 0 |
| +0x48 | byte | m_field48 | Flag |
| +0x4c | float | m_Speed | Calculated from random + constants |
| +0x5c | float | m_Gravity_x | Gravity vector |
| +0x60 | float | m_Gravity_y | |
| +0x64 | float | m_Gravity_z | |
| +0x6c | PSPParticleEmitter* | m_pEmitter | Coin sparkle trail |

### Coin::_Update (0x173790, 241 lines)

State machine:
- **State 0 (waiting)**: countdown delay timer; at 0 → switch to state 2, compute launch velocity from angle + speed
- **State 2 (flying)**: ballistic physics (`vel += gravity * dt`, `pos += vel * dt`), sparkle particles, plays SFX on collection
- Collected via `Coin::Arrived` callback

### Key Functions

| Function | Address | Purpose |
|----------|---------|---------|
| InitCoin | 0x00173454 | Set position, angle, speed, gravity, delay |
| MakeCoins | 0x00173568 | Spawn N coins (called from scoring pipeline) |
| _Update | 0x00173790 | State machine + physics |
| ClearCoins | 0x001731b8 | Remove all (called on GameExit) |
| Draw | 0x00173cc4 | Render coin model |

---

## SlashEntityGhost

Fading echo of the blade trail. Created by `SlashEntity::CreateGhost`.

### Struct Layout

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | float | m_FadeTimer | Starts at 1.0, decreases by `dt * 0.5` |
| +0x04 | void* | m_pLeftBuffer | Copied vertex strip (left side) |
| +0x08 | void* | m_pRightBuffer | Copied vertex strip (right side) |
| +0x0c | int | m_PointCount | Number of vertex pairs |

### SlashEntityGhost::Update (0x17eb60, 47 lines)

```
if fadeTimer > 0:
    fadeTimer -= dt * 0.5
    for each vertex pair (i = 0..pointCount):
        alpha = (i/2 / pointCount) * fadeTimer * 255
        clamp alpha to [0, 255]
        set vertex colour = (255, 255, 255, alpha)  // white with fade
        write to both left and right buffers
```

Each vertex is 0x24 (36) bytes with colour at +0x18.

### SlashEntityGhost::StartEffect (0x17ec24, 50 lines)

Copies vertex data from SlashEntity's buffers into ghost's own buffers. Sets `fadeTimer = 1.0`.

---

## MenuBackground

Simple background image drawn behind menu screens.

### Struct Layout (8 bytes)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | SmartPtr\<Texture\> | m_Texture | Background texture |
| +0x04 | int | m_field04 | |

### MenuBackground::Init (0x16f0a4)

Loads a localised texture via `TextureManager::LoadLocalisedTexture`.

Used by SplashInit and FrontendInit. Created with `operator_new(8)`.

---

## MenuButton : HUDControl3d : HUDControl (size = 0x15C, leaf class)

Interactive button used in all menus. Renders a 2D texture quad with optional sparkle ring, "new" indicator, and text labels. Each button optionally has a **real 3D Fruit/Bomb entity** spinning on top, drawn by the normal ActorManager render pipeline.

### Architecture

MenuButton has 3 rendering layers (drawn by `MenuButton::Draw`) plus one entity (drawn by `ActorManager::Draw`):

```
Layer 0 (3D): Spinning fruit entity (NOT drawn by MenuButton)
  └─ Real Fruit/Bomb entity at +0x80, created via ActorManager::Add
     Position = button.pos, rotation speed = random 8-12 deg/frame
     Drawn by ActorManager::Draw() in GameDraw (depth-sorted 3D mesh)

Layer 1 (2D): Button texture quad (+0x74)
  └─ Scale(size) → RotZ(angle) → Translate(globalOffset + pos)
     Shake offset if field_0x158 > 0 (random ±3.0)
     TintColour with button colour + alpha → DrawQuadUnCached

Layer 2 (2D): "New item" star indicator (+0xFC >= 0)
  └─ Oscillating bounce via SinIdx, dimmed or highlighted
     Uses shared star texture from LoadContent

Layer 3 (2D): Sparkle ring (+0xF8 >= 0)
  └─ 8 segments × 6 verts = 48 QUADCUSTOMVERTEX tri-list
     Pre-baked ring geometry on first call (SinIdx/CosIdx at 45° intervals)
     Colour cycles through brightness per frame (segment × 32, clamped 64-255)

Text: BakedString labels drawn at button.pos with Y offsets
```

### Fruit Entity Creation (in Init, 0x0014ee40)

```c
// fruitType >= 0 creates a real entity; -1 (toggles) skips this
if (fruitType >= 0) {
    int entityType = (fruitType >= bombThreshold) ? 1 : 0;  // 0=Fruit, 1=Bomb
    Entity* entity = ActorManager::Add(entityType, true);
    entity->pos = button.pos;
    entity->vel = globalScale;     // written to +0x1c (velocity fields, NOT scale!)
    entity->Init(0, fruitType, NULL);  // scale param = NULL → 1.0
    // Init calls SetFruitType which computes visual scale from globals:
    //   entity.scale = globalVec × configFloat × 0.01  (NOT 25.0!)
    this->m_pEntity = entity;        // +0x80

    // POST-INIT: shrink fruit for menu display
    entity->scale *= 0.2;  // DAT_0014f194 = 0.2

    this->field_0x34 = 0x40;  // menu draw layer
    this->field_0xf4 = RandFloat(4.0) + 8.0;  // rotation speed 8-12
    if (Rand32(2) == 0) field_0xf4 = -field_0xf4;  // random direction

    // Clamp rotation magnitude
    entity->rotX = max(0.75, abs(entity->rotX)) * sign(entity->rotX);
    entity->rotY = max(0.50, abs(entity->rotY)) * sign(entity->rotY);
}
```

### Struct Layout (0x15C bytes)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00..+0x73 | HUDControl | super | Base class |
| +0x74 | SmartPtr\<Texture\> | m_ButtonTex | Main button texture |
| +0x80 | Entity* | m_pEntity | 3D fruit/bomb spinning on button (NULL for toggles) |
| +0x84 | int | m_FruitType | -1 = no fruit, 0+ = fruit index, ≥bombThreshold = bomb |
| +0x88 | Delegate0\<void\> | m_ClickCallback | Fired on touch release |
| +0xAC | Delegate0\<void\> | m_DeletedCallback | Fired when button removed |
| +0xD0 | int | m_FadeCounter | Drives alpha fade (× 1000 / 255) |
| +0xE8 | float | m_RandomOffset | Random visual offset (-20 to +20) |
| +0xF0 | bool | m_bFlipped | Random horizontal flip |
| +0xF4 | float | m_RotationSpeed | 8-12 deg/frame, random sign |
| +0xF8 | float | m_SparkleTimer | ≥0 = sparkle ring active |
| +0xFC | float | m_NewIndicatorTimer | ≥0 = "new" star active |
| +0x100..+0x108 | Vec3 | m_HitBoundsScale | From constructor param_5 |
| +0x114 | BakedString* | m_pLabel1 | Text label (upper) |
| +0x118 | BakedString* | m_pLabel2 | Text label (lower) |
| +0x11C | int | m_PlayerIndex | For multiplayer colour tint |
| +0x120 | byte | m_bScoreSubmitted | |
| +0x121 | byte | m_bVisible | = 1 |
| +0x122 | byte | m_bInteractive | = 1 |
| +0x123 | byte | m_bEnabled | = 1 |
| +0x124..+0x12C | Vec3 | m_TargetSize | Hit-test bounds target |
| +0x130 | bool | m_bHasHitArea | true if hitBounds > 0 |
| +0x131 | byte | m_bHighlighted | Affects tint (0.5 vs 1.0 alpha) |
| +0x134 | Fruit* | m_pFruitPiece | Direct fruit reference (for scale/rotate access) |
| +0x138 | byte | m_bRemovalPending | |
| +0x13C | float | m_AnimScale | = 1.0 |
| +0x140..+0x148 | Vec3 | m_BounceParams | For "new" indicator bounce |
| +0x14C | float | m_AnimSpeed2 | = 5.0 |
| +0x150 | float | m_AnimSpeed | = 5.0 |
| +0x154 | float | m_field154 | |
| +0x158 | float | m_ShakeTimer | > 0 = shaking (random ±3.0 offset) |

### Key Functions

| Function | Address | Lines | Purpose |
|----------|---------|-------|---------|
| Init | 0x0014ee40 | 222 | Create entity, set callbacks, random rotation |
| Update | 0x0014e614 | — | Tick entity rotation, animations, shake |
| Draw | 0x0014f9cc | 359 | Render 3 layers: button quad + star + sparkle ring |
| SetText | 0x0014ebc0 | — | Set BakedString labels |
| AddPeice | 0x00150240 | — | Add sub-element (text, icon) |
| Clicked | 0x001507d8 | — | Fire click delegate (empty virtual stub) |
| LoadContent | 0x0014f674 | 28 | Load 3 shared textures (star, sparkle, etc.) |
| Remove | 0x0014ed18 | — | Animate removal |

### Constructor Variants

| Address | Signature | Notes |
|---------|-----------|-------|
| 0x0014f24c | `MenuButton(pos, clickCb, fruitType, hitBounds, deletedCb, ...)` | Full constructor |
| 0x0014f348 | Similar | Variant |
| 0x0014f444 | Similar | Variant — called via thunk 0x000f36cc |
| 0x0014f55c | Similar | Variant |
| 0x000f36cc | Thunk | Dispatches to 0x0014f444 |
| 0x000f747c | Thunk | Most-used entry point (38 callers) |

All constructors internally call `MenuButton::Init` (0x001073d0 → 0x0014ee40).

### Usage Across Screens (38 call sites via constructor thunk 0x000f747c)

MenuButton is the primary interactive widget used in virtually every screen:

| Caller Function | Address Range | Buttons Created |
|----------------|---------------|-----------------|
| **MainScreen_Update** | 0x0014b342–0x0014bca0 | Play, Dojo, Arcade, Zen, Multiplayer, Sensei (6 buttons) |
| **AboutScreen::Update** | 0x0012f0cc–0x0012f242 | About screen navigation |
| **UpdateButtons** | 0x00130b58 | Dynamic button creation |
| **GameOverScreen::CreateControls** | 0x0013e7f8–0x0013ebea | Retry, menu, share buttons |
| **UpdateOnlineMultiplayerButton** | 0x0013edd8–0x0013eeaa | Online multiplayer toggle |
| **GameOverScreen::Update** | 0x001384fa–0x00138802 | 3 dynamic buttons |
| **UpdateLeaderboard** | 0x0013b136 | Leaderboard entry buttons |
| **CreateRetryButton** | 0x00141208 | Pause/GameOver retry |
| **CreateQuitButton** | 0x0014136e, 0x00148c06 | Quit buttons (2 screens) |
| **Leaderboard nav** | 0x00148d50–0x00149508 | Friends, Global, Local, Weekly, PageUp, PageDown |
| **PauseScreen::Update** | 0x00145d18 | Pause menu buttons |
| **PowerUpShop::Update** | 0x001547dc–0x00154bb0 | 4 shop item buttons |
| **TimeControl::Update** | 0x0015e352–0x0015e7ca | 3 buttons |
| **UpsellScreen::CreateBuyNowRing** | 0x00164da6 | Buy now button |
| **UpsellScreen::Update** | 0x001651c8 | Upsell buttons |
| **SpeedControl::Update** | 0x00156684 | Speed control button |

### Helper Functions (static, not methods)

| Function | Address | Purpose |
|----------|---------|---------|
| DrawQuad_MenuButton | 0x00149f34 | Static wrapper for Mesh::DrawQuadUnCached |
| MakeColourFromGlobal_MenuButton | 0x00149ef4 | Static: construct Colour from global pointer |
| DeleteStackDelegate_MenuButton | 0x0014a170 | Static: StackAllocatedPointer::Delete |
| MainScreen_DeleteMenuButtons | 0x0014aee8 | Remove all MenuButtons from MainScreen |

---

## EffectImage

Screen overlay image used by power-up screen effects (freeze ice, frenzy sides).

### Key Functions

| Function | Address | Purpose |
|----------|---------|---------|
| Parse | 0x0011dda4 | Load from XML `<image>` element |
| LoadTextures | 0x0011d1e4 | Load referenced textures |
| EffectImage ctor | 0x0011ba7c | 2-param constructor |

Parsed from `poweruplist.xml` `<effect><image>` elements with attributes: texture, pos, timeStart, timeEnd, transitionMoveIn/Out, transitionTime, transition, drawOrder, scaleToScreen, anchor, pulseSpeed, pulseScale.

---

## QUADCUSTOMVERTEX (vertex format)

Used by SlashEntity blade trail, SlashEntityGhost, and SplatEntity.

### Layout (0x24 = 36 bytes per vertex)

| Offset | Type | Name |
|--------|------|------|
| +0x00 | float | x |
| +0x04 | float | y |
| +0x08 | float | z? |
| +0x0c | float | u? |
| +0x10 | float | v? |
| +0x14 | float | ? |
| +0x18 | uint | colour (packed BGRA) |
| +0x1c | float | alpha/weight |
| +0x20 | float | ? |

Confirmed 0x24 stride from SlashEntityGhost::Update loop (`iVar4 += 0x24`).

---

## See Also

- [Screens & effects functions](../functions/screens-effects.md) -- MenuButton callbacks
- [Scoring functions](../functions/scoring.md) -- Coin::MakeCoins
