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

## ItemManager (singleton)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | ItemInfo*[4] | defaultItem | First item per type |
| +0x10 | vector\<ItemInfo*\> | allItems | 12 bytes |
| +0x1c | map\<ulong,ItemInfo*\> | itemById | 24 bytes |
| +0x34 | map\<ulong,ItemInfo*\>[4] | itemsByType | Each 24 bytes |

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

| Singleton | Notes |
|-----------|-------|
| ActorManager | Entity pool (5 types: Fruit, Bomb, unused, unused, BombBlast). See [engine/actor-manager.md](../engine/actor-manager.md) |
| WaveManager | XML-driven wave progression. See [wave.md](wave.md) |
| PowerUpManager | Modifier tracking (dt, score gain/loss) |
| ItemManager | Shop items, blade modifiers |
| BonusManager | Post-game bonus awards |
| AchievementManager | Unlocks |
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
