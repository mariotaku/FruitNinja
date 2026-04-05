# Coin : MortarEntity

Bouncing coin spawned on combo rewards. Pool-based via ActorManager (entity type unknown).

## Struct Layout (partial)

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

## Coin::_Update (0x173790, 241 lines)

State machine:
- **State 0 (waiting)**: countdown delay timer; at 0 -> switch to state 2, compute launch velocity from angle + speed
- **State 2 (flying)**: ballistic physics (`vel += gravity * dt`, `pos += vel * dt`), sparkle particles, plays SFX on collection
- Collected via `Coin::Arrived` callback

## Key Functions

| Function | Address | Purpose |
|----------|---------|---------|
| InitCoin | 0x00173454 | Set position, angle, speed, gravity, delay |
| MakeCoins | 0x00173568 | Spawn N coins (called from scoring pipeline) |
| _Update | 0x00173790 | State machine + physics |
| ClearCoins | 0x001731b8 | Remove all (called on GameExit) |
| Draw | 0x00173cc4 | Render coin model |

---

## See Also

- [Entity base](entity-base.md) -- Mortar::Entity base class
- [Scoring functions](../functions/scoring.md) -- Coin::MakeCoins
