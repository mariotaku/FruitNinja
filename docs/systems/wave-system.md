# Wave System

## Wave System (WaveManager::Init — 0x12393c, 470 lines)

### XML Loading

One XML file loaded per game mode (4 total), via `TiXmlDocument::LoadFile`. Outer loop: `mode = 0..3`.

### XML Structure

```xml
<wavedata>
  <!-- Global defaults for this mode -->
  <defaults count="int" critchance="float" wavedelay="float"
            spawntimescale="float" bombscale="float" bombgravity="float"
            bombspeed="float" bombspeedmax="float" bombmin="float"
            bombmax="float" critchancemod="float"
            allowbombs="true/false" allowbombsfrenzy="true/false"
            quesize="str" />

  <!-- Coin spawn probability -->
  <coinchance ... />

  <!-- Probability overrides -->
  <override ... />

  <!-- Wave definitions (one per difficulty level) -->
  <wave score="int" endscore="int|-2" minscore="int"
        critchance="float" wavedelay="float"
        bombcount="int" bombmin="int" bombmax="int"
        fruit="int">

    <!-- Spawn rules (1+ per wave) -->
    <spawner types="apple,orange,..."
             min="float" max="float"
             minangle="float" maxangle="float"
             speed="float" speedmax="float"
             gravity="float" timescale="float"
             offset="x,y,z" side="bottom|left|right|random"
             minvel="float" maxvel="float"
             forceonce="true/false" />

    <!-- Bomb parameters -->
    <bombs scale="float" scaleto="float" gravity="float"
           speed="float" speedmax="float"
           allowbombs="true/false" allowbombsfrenzy="true/false" />

    <!-- Special fruit list -->
    <special types="starfruit,dragonroll,..." />
  </wave>
</wavedata>
```

### WAVE_INFO (size = 0x78 = 120 bytes)

| Offset | Type | Name | XML Source |
|--------|------|------|------------|
| +0x00 | int | m_ScoreThreshold | `score` attr |
| +0x04 | int | m_EndScore | `endscore` attr; -2 = "none" |
| +0x08 | SPAWNER_INFO* | m_pSpawners | Allocated array from `<spawner>` children |
| +0x0c | int | m_SpawnerCount | Count of `<spawner>` elements |
| +0x10 | float | m_BombScale1 | `<bombs>` `scale` attr |
| +0x14 | float | m_BombScale2 | `<bombs>` `scaleto` attr |
| +0x18 | float | m_BombGravity | `<bombs>` `gravity` attr |
| +0x1c | float | m_BombSpeed | `<bombs>` `speed` attr |
| +0x20 | float | m_BombSpeedMax | `<bombs>` `speedmax` attr |
| +0x24 | float | m_BombMinAngle | `<bombs>` `minangle` attr |
| +0x28 | float | m_BombMaxAngle | `<bombs>` attr |
| +0x30 | float | m_BombField30 | `<bombs>` attr |
| +0x38 | byte | m_bAllowBombs | `allowbombs` attr (true/false) |
| +0x39 | byte | m_bAllowBombsFrenzy | `allowbombsfrenzy` attr |
| +0x3c | int | m_MinScore | `minscore` attr |
| +0x44 | float | m_WaveDelay | `wavedelay` attr |
| +0x4c | int | m_BombMin | `bombmin` / `bombcount` attr |
| +0x50 | int | m_BombMax | `bombmax` attr |
| +0x54 | vector\<string\> | m_SpecialFruits | `<special>` `types` attr (word list) |
| +0x60 | int | m_field60 | |
| +0x64 | float | m_CriticalChance | `critchance` attr (e.g. 100.0 = 100%) |
| +0x68 | int | m_WaveIndex | Sequential index within mode |
| +0x6c | COIN_CHANCEINATOR* | m_pCoinChance | `<coinchance>` child element |
| +0x70 | int | m_WaveNumber | `fruit` attr |
| +0x74 | int | m_TotalWeight | Sum of (spawner.min + spawner.max) / 2 |

Constructed from `DEFAULT_WAVE_INFO` base, then overridden per `<wave>` element.

### SPAWNER_INFO (size = 0x64 = 100 bytes) — Updated

| Offset | Type | Name | XML Source |
|--------|------|------|------------|
| +0x00 | int* | m_pFruitTypeHashes | Allocated array of StringHash per fruit type |
| +0x04 | vector\<string\> | m_FruitTypeNames | From `types` attr (SplitWords) |
| +0x10 | int | m_FruitTypeCount | Number of fruit types in `types` |
| +0x14 | float | m_TimeScale | `timescale` attr |
| +0x18 | float | m_Offset_x | `offset` attr (parsed Vec3) |
| +0x1c | float | m_Offset_y | |
| +0x20 | float | m_Offset_z | |
| +0x24 | float | m_MinAngle | `minangle` attr |
| +0x28 | float | m_MaxAngle | `maxangle` attr |
| +0x2c | float | m_MinVel | `minvel` attr |
| +0x30 | float | m_MaxVel | `maxvel` attr |
| +0x34 | byte | m_SpawnType | `side` attr via ParsePlacement (0=bot,1=bot-slow,2=left,3=right,4=rand) |
| +0x38 | float | m_SpawnMin | `min` attr (min fruit count per spawn) |
| +0x3c | float | m_SpawnMax_unused | (unused?) |
| +0x40 | float | m_SpawnMax | `max` attr (max fruit count per spawn) |
| +0x44 | float | m_Speed | `speed` / `speedmax` attr |
| +0x48 | float | m_Gravity | `gravity` attr |
| +0x4c | float | m_field4c | `timescale` attr (secondary) |
| +0x5c | float | m_ZOffset | (from earlier analysis) |
| +0x60 | byte | m_bForceOnce | `forceonce` attr (true/false) |

### DEFAULT_WAVE_INFO (size = 0x40 = 64 bytes)

Stored at WaveManager + 0xdc + mode × 0x40. Parsed from `<defaults>` element. Contains default values that each WAVE_INFO inherits.

| Offset | Type | Name | XML Source |
|--------|------|------|------------|
| +0x00 | int | m_DefaultCount | `count` attr |
| +0x04 | float | m_CritChance | `critchance` attr |
| +0x08 | float | m_WaveDelay | `wavedelay` |
| +0x0c | float | m_SpawnTimeScale | `spawntimescale` |
| +0x10 | float | m_BombScale | `bombscale` |
| +0x14 | float | m_BombGravity | `bombgravity` |
| +0x18 | float | m_BombSpeed | `bombspeed` |
| +0x1c | float | m_BombSpeedMax | `bombspeedmax` |
| +0x20 | float | m_BombMin | `bombmin` |
| +0x24 | float | m_BombMax | `bombmax` |
| +0x28 | float | m_CritChanceMod | `critchancemod` |
| +0x2c | float | m_field2c | |
| +0x30 | int | m_field30 | |
| +0x34 | bool | m_bAllowBombs | `allowbombs` |
| +0x35 | bool | m_bAllowBombsFrenzy | `allowbombsfrenzy` |

### Wave Processing Flow

```
WaveManager::Init():
  For each game mode (0..3):
    1. Load XML file (one per mode)
    2. Parse <defaults> → DEFAULT_WAVE_INFO[mode]
    3. Parse <coinchance> → COIN_CHANCEINATOR[mode]
    4. Count <wave> elements → allocate WAVE_INFO* array
    5. For each <wave>:
       a. Create WAVE_INFO from DEFAULT_WAVE_INFO base
       b. Override fields from XML attributes
       c. Count <spawner> children → allocate SPAWNER_INFO array
       d. For each <spawner>:
          - SplitWords(types) → fruit type name list
          - Allocate hash array, parse angles/speed/gravity/offset
          - ParsePlacement(side) → spawn type enum
       e. Parse <bombs> parameters
       f. Parse <special> fruit list
       g. Push WAVE_INFO* to vector per mode
    6. Fix up wave end-scores (fill -1 gaps with next wave's score - 1)

WaveManager::Update(dt):
  → GetNextWave() → SetCurrentWave() → UpdateWave()
  → SpawnFruit() / SpawnBomb() per spawner rules
```

---
