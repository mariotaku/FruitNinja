# Entity Base

## CreateEntity Factory (0x0017421c)

Maps entity type ID to class. Sizes verified from `operator_new` in the decompilation:

| Type | Class | Alloc Size | Notes |
|------|-------|------------|-------|
| 0 | Fruit | 0x118 (280 bytes) | |
| 1 | Bomb | 0xB0 (176 bytes) | |
| 2 | Coin | 0x94 (148 bytes) | |
| 3 | SlashEntity | 0x184 (388 bytes) | |
| 4 | BombBlast | 0x70 (112 bytes) | |

Registered with `ActorManager::m_FactoryDelegate` during GameInit. GameInit also pre-allocates 30 each of Fruit, Bomb, and BombBlast (flags |= 0x11 = deactivated).

---

## Mortar::Entity (base class, size = 0x3c)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | EntityFns* | vtable | |
| +0x04 | int | field_0x04 | |
| +0x08 | ushort | m_TrackerID | Entity network tracker ID; used in FruitSlicedPacket |
| +0x0a | | (padding) | 2 bytes |
| +0x0c | byte | flags | bit 1 = has collision shape, bit 4 = skip entity |
| +0x10 | float | pos_x | Position X |
| +0x14 | float | pos_y | Position Y |
| +0x18 | float | pos_z | Position Z |
| +0x1c | float | vel_x | Velocity X |
| +0x20 | float | vel_y | Velocity Y |
| +0x24 | float | vel_z | Velocity Z |
| +0x28 | float | m_Scale_x | Visual scale X; set in SetFruitType/SetBombScale |
| +0x2c | float | m_Scale_y | Visual scale Y |
| +0x30 | float | m_Scale_z | Visual scale Z |
| +0x34 | | (padding) | 2 bytes |
| +0x36 | ushort | angle | Blade/rot angle; used with CosIdx/SinIdx |
| +0x38 | Col* | m_Col | Collision shape pointer |

---

## See Also

- [Slash Entity](slash-entity.md) -- blade trail entity
- [Coin](coin.md) -- bouncing coin entity
- [Splat Entity](splat-entity.md) -- juice splat entity
- [Fruit entity](fruit.md) -- Fruit struct and functions
- [Bomb entity](bomb.md) -- Bomb struct and functions
- [Physics system](../systems/physics.md) -- gravity, collision shapes
