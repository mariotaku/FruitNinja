# Wave Manager Struct

## WaveManager (singleton, size ≥ 0x2d8)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x000 | Math::Random | random | |
| +0x054 | float[2] | m_Speed | Current wave speed per player |
| +0x068 | float | spawnLevel | Used by Bomb chain spawn |
| +0x070 | float | m_CritChanceMult | Global critical chance multiplier |
| +0x0dc | DEFAULT_WAVE_INFO[4] | defaultWaveInfo | Each 0x40 bytes |
| +0x1dc | COIN_CHANCEINATOR[4] | coinChance | Each 0x08 bytes |
| +0x1fc | vector\<PROB_OVERRIDE\>[4] | probOverrides | |
| +0x22c | WAVE_INFO*[2] | m_pCurrentWave | Per-player current wave ptr |
| +0x2d4 | float | field_0x2d4 | |

**CriticalMode(p):** `GetCriticalChance(p) = m_pCurrentWave[p]→waveInfo[0x64] × m_CritChanceMult`. Returns true when `Game.m_ScoreThreshold / 2 < criticalChance`.

Wave data loaded from XML (TiXmlDocument). 4 game modes. WAVE_INFO = 0x78 bytes. SPAWNER_INFO: +0x2c=minAngle, +0x30=maxAngle, +0x34=typeFlags, +0x5c=zOffset.

---

## FruitSaveData (size = 0x238)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | map\<ulong,SliceTotal\> | sliceTotals1 | 24 bytes |
| +0x18 | map\<ulong,SliceTotal\> | sliceTotals2 | 24 bytes |
| +0x34 | list\<EntityState\> | entityStates | 12 bytes |
| +0x44 | int[4] | perModeField | |
| +0x54 | int[4] | perModeField2 | |
| +0x6c | int | field75_0x6c | Copied to Game+0x04 as gameMode |
| +0x74 | int | field80_0x74 | = 0xffffffff |
| +0x78 | int | highscore | Named by symbols |
| +0x80 | int[32] | perTypeData | = 0xffffffff each |
| +0x138 | float | m_blitzSpawnTime | |
| +0x150 | list\<WaveState\> | waveStates | 12 bytes |
| +0x158 | map\<ulong,AchievementItem\> | achievements1 | 24 bytes |
| +0x170 | map\<ulong,AchievementItem\> | achievements2 | 24 bytes |
| +0x194 | map\<int,int\>[4] | scoreMaps | Per game mode, each 24 bytes |
| — | byte | m_CriticalChance | = 0x46 = 70% |

---
