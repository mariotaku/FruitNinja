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

## MenuButton : HUDControl

Interactive button used in all menus. Complex widget with sub-pieces (text, icon, background).

### Key Features

- Constructor takes **13 parameters** (position, size, textures, text, callbacks)
- Has "pieces" system: `AddPeice` adds sub-elements (text labels, icons)
- `Clicked` callback via Delegate
- Supports loading symbols, new-item indicators, shake animation
- `TouchReleased` triggers click callback
- `UpdatePeices` animates sub-elements

### Struct Size: Large (estimated ~0x120+ bytes)

Key fields include position, multiple textures, text buffers, callback delegates, and a list of sub-pieces.

### Key Functions

| Function | Address | Params | Purpose |
|----------|---------|--------|---------|
| Init | 0x0014ee40 | 12 | Full initialization |
| Update | 0x0014e614 | 2 | Tick animations |
| Draw | 0x0014f9cc | 2 | Render button + pieces |
| SetText | 0x0014ebc0 | 5 | Set label text |
| AddPeice | 0x00150240 | 13 | Add sub-element |
| Clicked | 0x001507d8 | 0 | Fire click callback |
| LoadContent | 0x0014f674 | 0 | Load textures |
| Remove | 0x0014ed18 | 0 | Animate removal |

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
