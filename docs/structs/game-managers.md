# Game Manager Structs

Game-specific singleton managers created during GameInit/GameInitialise.

## ScoreModifier : GameModifier (size >= 0x3c)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | void* | vtable | |
| +0x1c | PowerUp* | pPowerUp | Inherited from GameModifier |
| +0x20 | int | scoreGainAdd | Flat gain bonus, init=0 |
| +0x24 | int | scoreGainMultiply | Gain multiplier, init=1 |
| +0x28 | int | scoreLossAdd | Flat loss bonus, init=0 |
| +0x2c | int | scoreLossMultiply | Loss multiplier, init=1 |
| +0x30 | int | applyCount | Increments each ApplyModifier call |
| +0x34 | bool | applied | If true, UpdateSpecific skips |

<!-- Analysed: 2026-04-25T10:30 -->

## ItemManager (singleton, 0x94 bytes)

Full struct, field types, all method pseudocode: **see `docs/structs/items.md`**.

| Offset | Size | Type                    | Name              | Notes                                              |
|--------|------|-------------------------|-------------------|----------------------------------------------------|
| +0x00  | 4    | ItemInfo*               | m_DefaultItems[0] | Equipped/default SLASH_MODIFIER item               |
| +0x04  | 4    | ItemInfo*               | m_DefaultItems[1] | Equipped/default BACKGROUND item                   |
| +0x08  | 4    | ItemInfo*               | m_DefaultItems[2] | UPSELL default (rarely set)                        |
| +0x0c  | 4    | ItemInfo*               | m_DefaultItems[3] | REMOVEADS default (always NULL — type==3 excluded) |
| +0x10  | 12   | vector\<ItemInfo*\>     | m_Items           | All items in XML order                             |
| +0x1c  | 24   | map\<uint32,ItemInfo*\> | m_ByHash          | All items by m_Hash                                |
| +0x34  | 24   | map\<uint32,ItemInfo*\> | m_ByHashType[0]   | SLASH_MODIFIER items by hash                       |
| +0x4c  | 24   | map\<uint32,ItemInfo*\> | m_ByHashType[1]   | BACKGROUND items by hash                           |
| +0x64  | 24   | map\<uint32,ItemInfo*\> | m_ByHashType[2]   | UPSELL items by hash                               |
| +0x7c  | 24   | map\<uint32,ItemInfo*\> | m_ByHashType[3]   | REMOVEADS items by hash                            |

**Total: 0x94 bytes.** Singleton at BSS ~0x1f4b3c. GetInstance @ 0x00112c34.

ItemInfo = 0x40 bytes. SlashModInfo extends ItemInfo = 0x110 bytes.

## PowerUpManager (singleton, partial)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x60 | int | m_field60 | = 0 after SetDefaults |
| +0x64 | float | m_DtMod | dt multiplier; ApplyDtMod multiplies this |
| +0x6c | float | m_field6c | = 1.0f |
| +0x70 | float | m_field70 | = 1.0f |
| +0x78 | int | m_ScoreGainMult | Used by GetScoreGainMultiplier |
| +0x7c | int | m_ScoreGainFactor | Multiplied with above |

## BonusManager (singleton, size ~0x20)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | vector\<BonusType\> | bonusTypes | 12 bytes |
| +0x0c | list\<Bonus\> | m_field12_0xc | 12 bytes |
| +0x14 | vector\<int\> | m_field_0x14 | 12 bytes |

## Game-Specific Singletons

### Analysis Coverage

| Manager | Functions | Struct Size | Named Fields | Coverage | Notes |
|---------|-----------|-------------|--------------|----------|-------|
| **WaveManager** | 10 | 728 bytes | 6 | **Good** | XML-driven wave progression |
| **PowerUpManager** | 8 | 144 bytes | 10 | **Good** | Modifier tracking (dt, score) |
| **NetworkManager** | 6 | 668 bytes | 10 | **Good** | Skipped for port |
| **BonusManager** | 6 | 32 bytes | 1 | **Partial** | Post-game bonus awards |
| **ItemManager** | 13 | 148 bytes | 11 | **Full** | Shop items, blade modifiers — see items.md |
| **AchievementManager** | 5 | 1 byte stub | 0 | **Stub** | Unlocks |
| **LeaderboardManager** | 5 | 64 bytes | 0 | **Minimal** | Skipped for port |

All managers have `__thiscall` properly applied.

### Singleton Details

| Singleton | Notes |
|-----------|-------|
| ActorManager | Entity pool (5 types: Fruit, Bomb, unused, unused, BombBlast). See [engine/actor-manager.md](../engine/actor-manager.md) |
| WaveManager | XML-driven wave progression. See [wave.md](wave.md) |
| PowerUpManager | Modifier tracking (dt, score gain/loss) |
| ItemManager | Shop items, blade modifiers. 148-byte struct. Full RE in docs/structs/items.md |
| BonusManager | Post-game bonus awards |
| AchievementManager | Unlocks. 1-byte stub struct |
| LeaderboardManager | Online services (skipped in port) |
| NetworkManager | P2P + OpenFeint + GameCenter (skipped in port) |
| GameSound | 32-slot SFX pool over SoundManager. See [engine/sound-system.md](../engine/sound-system.md) |
| FruitCamera | Ortho camera. See [engine/camera.md](../engine/camera.md) |

## Asset Loading Order (GameInitialise at 0x10bdfc, 305 lines)

Full 25-step bootstrap documented in [functions/game-loop.md](../functions/game-loop.md#gameinitialise-0x0010bdfc-305-lines--one-time-engine-bootstrap).

Final asset loading steps (after engine singletons + fonts):
1. LoadLocalisedTexture -> Game+0x17c (fruit atlas)
2. MenuButton::LoadContent
3. Fruit::LoadInfo (FRUIT_INFO from XML)
4. SplatEntity::LoadContent
5. SlashEntity::LoadContent
6. Bomb::LoadContent
7. GameOverScreen::LoadContent
8. PowerUpShop::LoadContent
9. PreloadSounds
