# Other Structs & Singletons

## Other Structs

### ScoreModifier : GameModifier (size ≥ 0x3c)

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

### MAMAudioThread (size ≥ 0x154)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x10 | float | masterVolume | -1.0f |
| +0x18 | int | sampleRate | 16000 = 16 kHz |
| +0x1c | int | bufferSize | 0xc80 = 3200 |
| +0x30 | int | voiceCount | = 16 |
| +0x34 | MAMVoice[16] | voices | Each 0x10 bytes = 0x100 total |
| +0x134 | NLFQueue | cmdInput | Thread-safe audio command input |
| +0x144 | NLFQueue | cmdOutput | Thread-safe audio command output |

### ItemManager (singleton)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | ItemInfo*[4] | defaultItem | First item per type |
| +0x10 | vector\<ItemInfo*\> | allItems | 12 bytes |
| +0x1c | map\<ulong,ItemInfo*\> | itemById | 24 bytes |
| +0x34 | map\<ulong,ItemInfo*\>[4] | itemsByType | Each 24 bytes |

ItemInfo = 0x40 bytes. SlashModInfo extends ItemInfo = 0x110 bytes.

### PowerUpManager (singleton, partial)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x60 | int | m_field60 | = 0 after SetDefaults |
| +0x64 | float | m_DtMod | dt multiplier; ApplyDtMod multiplies this |
| +0x6c | float | m_field6c | = 1.0f |
| +0x70 | float | m_field70 | = 1.0f |
| +0x78 | int | m_ScoreGainMult | Used by GetScoreGainMultiplier |
| +0x7c | int | m_ScoreGainFactor | Multiplied with above |

### BonusManager (singleton, size ~0x20)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | vector\<BonusType\> | bonusTypes | 12 bytes |
| +0x0c | list\<Bonus\> | m_field12_0xc | 12 bytes |
| +0x14 | vector\<int\> | m_field_0x14 | 12 bytes |

---

## Asset Loading Order (GameInitialise)

1. MenuButton::LoadContent
2. Fruit::LoadInfo
3. SplatEntity::LoadContent
4. SlashEntity::LoadContent
5. Bomb::LoadContent
6. GameOverScreen::LoadContent
7. PowerUpShop::LoadContent
8. PreloadSounds

## Subsystem Singletons

SystemManager, MatrixManager, FileManager, DisplayManager (480×320), TextureManager, MeshManager, AnimationManager, InputManager, PSPParticleManager, PowerUpManager, LeaderboardManager, NetworkManager (P2P + OpenFeint + GameCenter), MAMAudioController → MAMAudioThread (16 voices, 16kHz), WaveManager, ItemManager, AchievementManager, BonusManager, Mortar::Touch, Mortar::SoundManager, FruitCamera
