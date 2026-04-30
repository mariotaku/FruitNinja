# WaveManager Deep RE Pass

Comprehensive ASM-verified audit of every stub, partial body, and `field_0xNN`
placeholder in the port's `WaveManager`. Produced 2026-04-30 by `re-analyst`
against `FruitNinja.exe` (ARM32 LE, image base 0x00010000).

This builds on but supersedes the per-section data in:
- `docs/engine/wavemanager-init-asm-audit.md` (Init only)
- `docs/engine/wavemanager-asm-verify.md` (SpawnFruit/SpawnBomb focus)
- `docs/engine/wave-transition-asm-audit.md` (UpdateWave/GetNextWave timer flow)
- `docs/engine/splat-pool-and-wave-resume.md §A7` (Resume)

Where this doc disagrees with the older audits, **this one wins** — newer reads
of the binary, more constants resolved, more methods covered.

GOT base is `0x001ec130` for any `(GOT + DAT_xxx)` resolutions.

---

## 1. WaveManager struct — full field-by-field table (size ~ 0x300 bytes)

Every offset reachable from the binary's read/write set is listed. "Refs"
column shows the binary functions that touch the slot.

| Offset | Type    | Binary semantic                                | Refs                                     | Suggested port name |
|--------|---------|------------------------------------------------|------------------------------------------|---------------------|
| +0x000 | Random  | RNG instance (24 bytes)                        | Init, GetNextWave, SpawnBomb, SpawnFruit | `m_Random`          |
| +0x024 | ptr     | `WaveQue*` (survival/combo only; null in Classic/Arcade/Zen) | Reset, Destroy, GetNextWave queue path, SetupWaveQue | `m_pWaveQue` |
| +0x020 | ptr     | `WaveQueItem*` (queue head item)               | Reset, Destroy, SetupWaveQue             | `m_pWaveQueItem`    |
| +0x028 | int     | misc reset slot (cleared in Reset)             | Reset                                    | `m_field28`         |
| +0x02c | int     | misc reset slot                                | Reset                                    | `m_field2c`         |
| +0x035 | uint8   | wave-active flag (1 default; cleared while running) | Reset writes 1; UpdateWave's wave-end gate reads | `m_bWaveActive` (port `field_0x35`) |
| +0x036 | uint8   | reset flag (always 0 after Reset)              | Reset                                    | `m_field36`         |
| +0x037 | uint8   | "wave was reset" tail flag                     | Reset, Update post-pump gate             | `m_field37`         |
| +0x038 | int     | last-selected wave index sentinel (-1 = none)  | Reset, Update post-pump compare          | `m_LastWaveIdx`     |
| +0x040 | float   | running play-time accumulator (game[+0x170]==paused-gate increments) | UpdateWave when game[+0x170]!=0 | `m_PlayTimeA` |
| +0x044 | float   | running play-time accumulator B (parallel)     | UpdateWave (same gate)                   | `m_PlayTimeB`       |
| +0x048 | int     | misc int slot (cleared in Reset)               | Reset                                    | `m_field48`         |
| +0x04c | float   | combo-speed-loss timer P0 (decremented in UpdateComboSpeed) | Reset, AddSpeed, ResetSpeed, AddToSpeedLossTime, UpdateComboSpeed | `m_SpeedLossTime[0]` |
| +0x050 | float   | combo-speed-loss timer P1                       | Same                                     | `m_SpeedLossTime[1]` |
| +0x054 | float   | base wave-speed P0 (=`m_Speed[0]`)              | GetSpeed, ResetSpeed, AddSpeed (writes via `&m_Speed_P0[playerIdx]`) | `m_Speed[0]` |
| +0x058 | float   | base wave-speed P1 (=`m_Speed[1]`)              | Same                                     | `m_Speed[1]`        |
| +0x05c | int     | combo blitz-bonus point counter P0              | AddSpeed, ResetSpeed, GetComboBonusProgression | `m_BlitzBonus[0]` |
| +0x060 | float   | combo timer slot P0 (`field_0x60`); pre-blitz countdown 3.0 | UpdateComboSpeed, AddSpeed, ResetSpeed, GetComboBonusProgression | `m_ComboTimer[0]` |
| +0x064 | float   | bomb scale multiplier (BombScale power-up)      | BombScale, Update reset, SpawnBomb path  | `m_BombScale`       |
| +0x068 | float   | bomb spawn-level multiplier (BombMultiplyer power-up). **Reads as `0.0 < this` gate in UpdateWave bomb-branch** | BombMultiplyer, Update reset, UpdateWave | `spawnLevel` (binary name) |
| +0x06c | float   | fruit-multiplier (FruitMultiplyer power-up)     | FruitMultiplyer, Update reset, UpdateWave fruit-branch gate | `m_FruitMult` |
| +0x070 | float   | critical chance multiplier                       | CriticalChanceMod, Update reset, GetCriticalChance | `m_CritChanceMult` |
| +0x074 | float   | global wave-speed accumulator (`globalDt`).  `GetWavedt` multiplies output by this. Saved/restored to `FruitSaveData+0x14c`. | Reset, Update (clamped to [+0x8c..+0x9c]), Resume, SaveWaveInfo, GetWavedt, ResetGlobalDt | `m_GlobalDt` |
| +0x078 | float   | dtMod scratch (PowerUpManager::m_DtMod)          | Update copies from PowerUpManager, GetWavedt multiplies | `m_DtMod` |
| +0x07c | float[4]| **per-mode `globalDtInc`** (XML `<defaults>`)    | Init parses, Update accumulates `+0x74 += dt * +0x7c[mode]` | `m_GlobalDtInc[4]` (port `m_DtIncPerMode`) |
| +0x08c | float[4]| **per-mode `globalDtStart`** (lower bound)      | Init parses, Update clamp lo               | `m_GlobalDtMin[4]` (port `field_0x8c`) |
| +0x09c | float[4]| **per-mode `globalDtMax`** (upper bound)        | Init parses, Update clamp hi               | `m_GlobalDtMax[4]` (port `field_0x9c`) |
| +0x0ac | vector* | `std::vector<WAVE_INFO*>[4]` per game mode (stride 0xc) | Init push_back, Reset/GetNextWave size/begin/end | `waveInfos[4]` |
| +0x0dc | DEFAULT_WAVE_INFO[4] | per-mode <defaults> snapshot (stride 0x40) | Init parses, WAVE_INFO ctor copies | `defaultWaveInfo[4]` |
| +0x108 | uint8   | **global** waitForEntities (from <defaults>; one byte, NOT per-mode) | Init parses; UpdateWave never reads (per-wave overrides at WAVE_INFO+0x38) | `m_DefWaitForEntities` |
| +0x109 | uint8   | **global** waitForProcessing                                          | Same                                     | `m_DefWaitForProcessing` |
| +0x114 | int     | "players==1,2" mode marker (1 if attr present)                        | Init parses; influences vector indexing in GetNextWave/Init `pWVar16->field_0x114 * 0x30` | `m_bSplitPlayerWaves` |
| +0x118 | int     | second player count (=2 if "players=1,2")                             | Init only                                | `m_PlayerCount`     |
| +0x11c | int     | tertiary marker (=-1 if "1,2")                                        | Init only                                | `m_field11c`        |
| +0x1dc | COIN_CHANCEINATOR[4] | per-mode coin chance (stride 0x08)                       | Init parses, RequestCoins                | `coinChance[4]`     |
| +0x1fc | vector* | `std::vector<PROBABILITY_OVERIDE>[4][2]` — per-mode and per-player; stride 0xc per (mode,player), outer stride 0x30 per player | Init parses (`+modeIdx*0xc + +0x114-mark*0x30`), UpdateWave consumes, Reset/Resume re-rolls, GetNextWave decrements counters | `probOverrides[4][2]` |
| +0x22c | WAVE_INFO* | `m_pCurrentWave[0]` (current wave for player 0)                     | Many                                      | `m_pCurrentWave[0]` |
| +0x230 | WAVE_INFO* / int | `m_pCurrentWave[1]` (player 1) — also reused as `int m_WaveCount` (binary writes int wave count to this slot in single-player) | Reset writes -1 (cast as ptr), GetNextWave increments as int via `(&m_pCurrentWave_P1)[0]` then dereferences | `m_pCurrentWave[1]` aliased with `m_WaveCount` |
| +0x234 | float[2]| pre-spawn delay timer per player (XML `NextWaveDelay/delay` after revisit math; clamped >= 0.05) | UpdateWave decrements (Fix 1, fix 4), GetNextWave writes via `+0x234+p*4`, SetCurrentWave adds delay, Resume restores | `m_WaveDelay[2]` |
| +0x238 | float[2]| wave-end wait timer per player (XML `NextWaveDelay/wait` after `waitSpinc * m_Speed`) | UpdateWave wave-end branch reads `+0x238+p*4`, GetNextWave writes, Resume restores | `m_WaveWait[2]` |
| +0x23c | uint8   | "wave-was-spawned" flag P0 (gate for IsWaveProcessing)                | UpdateWave sets to 1 after each spawn, IsWaveProcessing reads | `m_bWaveSpawnedP0` |
| +0x23d | uint8   | blitz `m_blitzSpawnedThisGame` (count of blitz fruits forced this game) | UpdateWave PROBABILITY_OVERIDE branch, Resume/SaveWaveInfo | `m_BlitzSpawnedThisGame` |
| +0x23e | uint8   | blitz `m_blitzForceSpawnedCounter` (per-wave force-spawn state)        | UpdateWave PROBABILITY_OVERIDE branch  | `m_BlitzForceSpawnedCounter` |
| +0x23f | uint8   | (padding)                                                              | —                                       | `_pad23f`           |
| +0x240 | float   | random next-blitz spawn time (`RandF(10) + 10`)                        | Reset/Init writes initial, Resume restores, UpdateWave reads (compares against `TimeControl::GetCountDown - timeDelta`) | `m_BlitzSpawnTime` |
| +0x244 | int[2][32] | `m_FruitQueue[2][32]` (ChooseFrom queue; fruit-type per slot)       | GetNextWave writes, UpdateWave consumes, Reset/Resume init | `m_FruitQueue[2][32]` |
| +0x2c4 | int     | `m_ScoreThreshold[0]` — last waveNo that built the FruitQueue (gate) | GetNextWave writes after building queue, Reset/Resume init  | `m_ScoreThreshold[0]` |
| +0x2c8 | int     | `m_FruitQueueSize[0]` (alias for `m_ScoreThreshold[1]` in port)       | Resume sets to 1 (default), GetNextWave writes, SaveWaveInfo serialises | `m_FruitQueueSize[0]` |
| +0x2cc | int     | misc reset counter (cleared in Reset)                                 | Reset                                   | `m_field2cc`        |
| +0x2d0 | int     | misc reset counter (cleared in Reset)                                 | Reset                                   | `m_field2d0`        |
| +0x2d4 | float   | fixed-timestep accumulator (carries leftover dt < 1/60 between frames) | Update                                  | `m_StepAccum`       |
| +0x000 (HUD slot, **also stored at offset 0**) | ptr | `SpeedControl*` HUDControl cached pointer for blitz visualiser. **WARNING: this is at +0x00 — overlaps with `m_Random.seed`!** Binary uses `*(undefined4*)this` for both the RNG seed and the SpeedControl pointer in different code paths; this is impossible unless the SpeedControl is stored at a different offset. Re-read of UpdateComboSpeed shows `iVar2 = *(int*)this` and `*(SpeedControl**)this = this_00` — must be at +0x00. The Random class is constructed at +0x00 but the seed is in fields, so `*(int*)this` accesses the vtable/first member which the Random ctor sets but UpdateComboSpeed re-purposes. | UpdateComboSpeed, DeleteSpeedControl | `m_pSpeedControl` (stored at WaveManager+0x00) — **needs re-RE; suspect Random does not actually live at +0x00 in binary** |

**Field-naming critical note:** the binary's `WaveManager` does NOT have
`Random` at +0x00. The `*(Random**)(GOT + DAT_xxx)` pattern in many functions
is a separate global Random pointer, not a member. The port placed
`Random m_Random` at +0x00 in error. The binary slot at +0x00 is the
**`SpeedControl* m_pSpeedControl`** cached HUD pointer (used by
UpdateComboSpeed and DeleteSpeedControl). This explains why
`DeleteSpeedControl(c)` does `if (*(HUDControl**)this == c) *(int*)this = 0;`.

The `Random` referenced by SpawnFruit/GetNextWave is fetched via
`*(Random**)(GOT + DAT_xxx)` — a global game-wide Random instance, separate
from any WaveManager member.

This is a port-side struct layout bug that should be reviewed: the port's
`m_Random` member at +0x00 displaces every following field by 24 bytes.
The audit doc `wavemanager-init-asm-audit.md` did not surface this. **Flag:
RE-needed for full WaveManager layout — confirm whether port currently has
all subsequent offsets shifted by +0x18 or whether the +0x00 Random slot was
moved/renamed already.**

---

## 2. WAVE_INFO / SPAWNER_INFO / DEFAULT_WAVE_INFO — full field tables

### 2.1 WAVE_INFO (size 0x78)

Per binary ctor `@ 0x00126748` (single-arg) and `WaveManager::Init`-time
2-arg ctor (`@ 0x0012393c` calls `WAVE_INFO::WAVE_INFO(this, DEFAULT_WAVE_INFO*)`
which copies defaults from the parent <defaults> snapshot).

| Offset | Type    | Binary semantic                              | Default (no-arg ctor) | Port name | Notes |
|--------|---------|----------------------------------------------|-----------------------|-----------|-------|
| +0x00  | int     | `m_ScoreThreshold` (XML `waveNo`)            | 0                     | `m_ScoreThreshold` | OK |
| +0x04  | int     | `m_EndScore` (XML `until`; "forever"→-2)     | **-1** (port has -2)  | `m_EndScore` | Default mismatch |
| +0x08  | ptr     | `m_pSpawners`                                | nullptr               | `m_pSpawners` | OK |
| +0x0c  | int     | `m_SpawnerCount`                             | 0                     | `m_SpawnerCount` | OK |
| +0x10  | float   | `m_WaveDt` (XML `Wave_dt/dt`)                | **1.0** (port has 0.9) | `m_WaveDt` | Default mismatch |
| +0x14  | float   | `m_WaveDtInc` (XML `Wave_dt/inc`)            | 0.0                   | `m_WaveDtInc` | OK |
| +0x18  | float   | `m_WaveDtSpInc` (XML `Wave_dt/spinc`)        | 0.0                   | `m_WaveDtSpInc` | OK |
| +0x1c  | float   | `m_NextWaveSpeedLoss` (XML `NextWaveDelay/speedLoss`) | 0.0           | `m_NextWaveSpeedLoss` | OK |
| +0x20  | float   | `m_NextWaveDelay` (XML `NextWaveDelay/delay`; cleared to 0 if `wait>0`) | **2.0** (port has 0.0) | `m_NextWaveDelay` | Default mismatch |
| +0x24  | float   | `m_NextWaveDelayInc` (XML `NextWaveDelay/inc`) | 0.0                | `m_NextWaveDelayInc` | OK |
| +0x28  | float   | `m_NextWaveWait` (XML `NextWaveDelay/wait`)  | 0.0                   | `m_NextWaveWait` | OK |
| +0x2c  | float   | unused gap                                    | 0.0                   | `m_field2c`  | OK |
| +0x30  | float   | `m_NextWaveWaitSpInc` (XML `NextWaveDelay/waitSpinc`) | 0.0           | `m_NextWaveWaitSpInc` | OK |
| +0x34  | float   | `m_WaveRevisitCount` — incremented in GetNextWave when wave revisited | **1.0** (binary ctor; ResetWaveChances also resets to 1.0) | `field_0x34` | Default mismatch — ResetWaveChances writes 1.0 not 0.0 |
| +0x38  | uint8   | `m_bWaitForEntities`                          | 1                     | `m_bWaitForEntities` | OK |
| +0x39  | uint8   | `m_bWaitForProcessing`                        | 1                     | `m_bWaitForProcessing` | OK (per recent fix) |
| +0x3c  | int     | `m_Chance` (XML `chance`)                     | **10** (port has 90)  | `m_Chance` | Default mismatch |
| +0x40  | int     | `m_CurrentChance` — running copy of `m_Chance`; ResetWaveChances writes `+0x40 = +0x3c`; GetNextWave reads as weight; depleted via `wi[+0x40]--` is NOT done — only `*(int*)(iVar5 + 0x48) = DAT_00124b0c` (=0) and `*(int*)(iVar5 + 0x40) = 0` after low-game-count special case. Actually the depletion path is in ResetWaveChances under the "0 < +0x4c" branch only. | (set in ResetWaveChances) | (port `m_CurrentMax` is the same field — but is a tail field) | **TYPE WARNING: port has int; semantically same** |
| +0x44  | float   | `m_ChanceRegrowth` (XML `chanceRegrowth`)     | **0.25** (port has 0.33) | `m_ChanceRegrowth` | Default mismatch |
| +0x48  | float   | `m_CurrentRegrowth` — running copy of `m_ChanceRegrowth`; ResetWaveChances writes `+0x48 = +0x44`. **TYPE: float** (binary ctor sets to 0x3e800000 = 0.25). | (set in ResetWaveChances) | (port has gap; needs new field) | **NEW FIELD needed in port** |
| +0x4c  | int     | `m_GamesMin` (XML `games`/`gamesMin`; binary names this `m_BombMin` in Ghidra) | **-1** (port has 0) | `m_GamesMin` | Default mismatch |
| +0x50  | int     | `m_GamesMax`                                  | -1                    | `m_GamesMax` | Default mismatch |
| +0x54  | vector  | `m_SpecialFruits` / `m_ChooseFrom` (XML `<ChooseFrom types=>`) | empty | `m_SpecialFruits` | OK |
| +0x60  | int     | always cleared to 0 during Init               | 0                     | `m_field60` | OK |
| +0x64  | float   | `m_CriticalChance`                            | 1.0                   | `m_CriticalChance` | OK |
| +0x68  | int     | `m_WaveIndex` (sequential)                    | 0                     | `m_WaveIndex` | OK |
| +0x6c  | ptr     | `m_pCoinChance`                               | nullptr               | `m_pCoinChance` | OK |
| +0x70  | int     | `m_OverideProbabilityPool` (XML `overideProbabiltyPool` AND `waveNo` write here in different paths) | 100 | `m_OverideProbabilityPool` | Default mismatch (port has 0) |
| +0x74  | int     | `m_TotalWeight` (sum of `(min+max)/2` across spawners) | 0           | `m_TotalWeight` | OK |

**Per-binary `m_CurrentMax`/`m_CurrentRegrowth` mapping:** port stores
`m_CurrentMax` as a tail-end int field. Binary uses **`+0x40` (int) and
`+0x48` (float)**. The two fields ARE inside the struct — port should move
them to those slots and drop the tail variants.

### 2.2 SPAWNER_INFO (size 0x64)

Per binary ctor `@ 0x001270ac`.

| Offset | Type    | Binary semantic                                       | Default | Port name | Notes |
|--------|---------|-------------------------------------------------------|---------|-----------|-------|
| +0x00  | int*    | `m_pFruitTypeHashes`                                  | nullptr | `m_pFruitTypeHashes` | OK |
| +0x04  | vector  | `m_FruitTypeNames` (12 bytes)                         | empty   | `m_FruitTypeNames` | OK |
| +0x10  | int     | `m_FruitTypeCount`                                    | 0       | `m_FruitTypeCount` | OK |
| +0x14  | float   | `m_TimeScale` (never written by Init; runtime only)    | **1.0** | `m_TimeScale` | OK |
| +0x18  | float   | `m_Gravity.x`                                          | 0.0     | `m_Gravity_x` | OK |
| +0x1c  | float   | `m_Gravity.y`                                          | **-1.0** | `m_Gravity_y` | OK |
| +0x20  | float   | `m_Gravity.z`                                          | 0.0     | `m_Gravity_z` | OK |
| +0x24  | float   | `m_VelXScale` (XML `velscale` writes here AND +0x28; `velXscale` overrides) | 1.0 | `m_VelXScale` | OK |
| +0x28  | float   | `m_VelYScale`                                          | 1.0     | `m_VelYScale` | OK |
| +0x2c  | float   | `m_HorizMin` (XML `horizmin`)                          | -1.0    | `m_HorizMin` | OK |
| +0x30  | float   | `m_HorizMax` (XML `horizmax`)                          | 1.0     | `m_HorizMax` | OK |
| +0x34  | uint8   | `m_SpawnType` (enum)                                   | 0 (BOTTOM) | `m_SpawnType` | OK |
| +0x38  | float   | `m_SpawnMin` (XML `min`)                               | **0.0** (port has 1.0) | `m_SpawnMin` | Default mismatch |
| +0x3c  | float   | unused (binary ctor=0)                                 | 0.0     | `m_SpawnMax_unused` | OK |
| +0x40  | float   | `m_SpawnMax` (XML `max`)                               | **0.0** (port has 1.0) | `m_SpawnMax` | Default mismatch |
| +0x44  | float   | `m_GrowthInc` (XML `mininc`/`maxinc` BOTH write here; second wins) | 0.0 | `m_GrowthInc` | OK |
| +0x48  | float   | `m_Delay` (XML `delay`)                                | 0.0     | `m_Delay` | OK |
| +0x4c  | float   | `m_DelayInc` (XML `delayinc`)                          | 0.0     | `m_DelayInc` | OK |
| +0x50  | int     | `m_RemainingCount` (per-tick countdown)                | 0       | `m_RemainingCount` | OK |
| +0x54  | float   | `m_SpawnCountF` (running fractional accum; SaveWaveInfo serialises as `to_spawn`) | 0.0 | `m_SpawnCountF` | OK |
| +0x58  | int     | unknown (always 0; SaveWaveInfo writes 0 to this slot at SpawnState clear) | 0 | `m_field58` | OK |
| +0x5c  | float   | `m_SpawnTimer` (countdown timer; SaveWaveInfo serialises) | 0.0 | `m_SpawnTimer` | OK |
| +0x60  | uint8   | `m_bMirror` (XML `mirror`; default 0)                  | 0       | `m_bMirror` | OK |
| +0x61  | uint8   | unknown padding (set to 0 by ctor explicitly)          | 0       | `m_field61` | Could rename `_pad61` |

### 2.3 DEFAULT_WAVE_INFO (size 0x40)

Per `WaveManager::Init` <defaults> parsing. Reset @ method `DEFAULT_WAVE_INFO::Reset`.

| Offset | Type   | XML attribute              | Port name              |
|--------|--------|----------------------------|------------------------|
| +0x00  | int    | `waveChance`               | `m_WaveChance`         |
| +0x04  | float  | `waveChanceRegrowth`       | `m_WaveChanceRegrowth` |
| +0x08  | float  | `criticalChance`           | `m_CritChance`         |
| +0x0c  | float  | `dt`                       | `m_DefaultDt`          |
| +0x10  | float  | `dtInc`                    | `m_DtInc`              |
| +0x14  | float  | `dtSpInc`                  | `m_DtSpInc`            |
| +0x18  | float  | `beforeDelay`              | `m_BeforeDelay`        |
| +0x1c  | float  | `beforeDelayInc`           | `m_BeforeDelayInc`     |
| +0x20  | float  | `nextDelay`                | `m_NextDelay`          |
| +0x24  | float  | `nextDelayInc`             | `m_NextDelayInc`       |
| +0x28  | float  | `nextDelaySpInc`           | `m_NextDelaySpInc`     |
| +0x2c  | float  | `speedLoss`                | `m_DefSpeedLoss`       |
| +0x30  | int    | `overideProbabiltyPool`    | `m_OverideProbabilityPool` |

Port struct already matches.

### 2.4 PROBABILITY_OVERIDE (size 0x78)

Per ctor `@ 0x00126870` and copy-ctor `@ 0x0012737c`.

| Offset | Type   | Binary semantic                                              | Default | Port name |
|--------|--------|--------------------------------------------------------------|---------|-----------|
| +0x00  | int    | `m_PercentChance` (binary stores as int despite XML being float — divides by something at use site) | 0 | `m_PercentChance` (port float — TYPE warn) |
| +0x04  | int    | `m_PerWave` (XML `perWave`)                                   | 0       | `m_PerWave` |
| +0x08  | int    | `m_Counter` (running per-wave counter; UpdateWave bumps)      | 0       | `m_Counter` |
| +0x0c  | vector | `m_Types` (XML `types`)                                        | empty   | `m_Types` |
| +0x18..+0x68 | int[20] | per-type spawn-tracking queue (initialised to -1 each) | -1      | `m_TypeQueue[20]` (NEW FIELD; port doesn't have) |
| +0x68  | int    | `m_field68` (init 0)                                          | 0       | `m_field68` |
| +0x6c  | float  | `m_DisableWhenPowered` (init from `DAT_001268b0`; default 0.0; XML `disableWhenPowered`) | 0.0 | `m_DisableWhenPowered` |
| +0x70  | int    | `m_PerWaveCount` (init from `DAT_001268ac` = 0; XML `waveCount`)  | 0       | `m_WaveCount` (port name) |
| +0x74  | int    | `m_SelectedType` (-1 = unset; SelectType picks)              | -1      | `m_SelectedType` |

**Port mismatch:** the +0x18..+0x68 int[20] queue is missing in port struct.
Used by UpdateWave's blitz-fruit-selection inner loop to track which types
were already spawned in the current wave.

---

## 3. Per-method body audit

For each: status, full pseudocode (binary semantics), and port-side action.

### 3.1 `UpdateComboSpeed(float dt)` @ 0x00122f50 — STUB in port

**Status: stubbed. Full body needed.**

**Binary pseudocode:**

```c
void WaveManager::UpdateComboSpeed(float dt) {
    // Gate: only run when game->dt == 0 AND game->mode == 2 (Arcade).
    // (DAT field at game+0x0c == 0.0 means "not paused via menu";
    //  game+0x04 byte == 2 is gameMode==Arcade)
    if (game->somePauseFloat == 0.0f && game->gameMode == 2) {
        float curSpeed   = m_Speed[0];
        float targetP1   = m_Speed[1];
        if (targetP1 < 2.9f) targetP1 = 0.0f;   // DAT_001230dc/e0
        // Lerp m_Speed[0] toward m_Speed[1] (clamped by ±5*dt).
        float delta;
        if (curSpeed == targetP1)            delta = 0.0f;
        else if (targetP1 < curSpeed)        delta = max(targetP1 - curSpeed, dt * -5.0f);
        else                                  delta = min(targetP1 - curSpeed, dt *  5.0f);
        m_Speed[0] = curSpeed + delta;

        // Lazily allocate the SpeedControl HUD widget on first call.
        if (m_pSpeedControl == nullptr) {
            m_pSpeedControl = new SpeedControl();   // 0xac bytes
            // Wire DeleteSpeedControl callback.
            Mortar::Delegate1<void,HUDControl*>::QCallee<WaveManager>(...);
            m_pSpeedControl->m_Callback = ...;
            HUD::AddControl(game->display->hud, m_pSpeedControl, false);
        }
        // Push current speed into the widget.
        m_pSpeedControl->m_DisplayedSpeed = m_Speed[0];   // +0x80
        m_pSpeedControl->m_RawSpeed       = m_SpeedLossTime[0];  // +0x94 from field_0x4c

        // Decay the speed-loss timer (field_0x4c) using GetWavedt/m_NextWaveSpeedLoss.
        if (m_SpeedLossTime[0] > 0.0f && m_pCurrentWave[0] != nullptr
            && m_pCurrentWave[0]->m_NextWaveSpeedLoss > 0.0f)
        {
            float wd = GetWavedt(0);
            if (wd > 1.0f) wd = 1.0f;
            m_SpeedLossTime[0] -= (wd * dt) / m_pCurrentWave[0]->m_NextWaveSpeedLoss;
            if (m_SpeedLossTime[0] <= 0.0f)
                ResetSpeed(0);
        }
    }
}
```

**Port-side action:** `WaveManager.cpp:776` — replace stub with the above body.
Requires:
- `game->gameMode == 2` gate (already accessible via `Game::GetInstance()`).
- `game->dt`/pause float — port doesn't have a direct equivalent yet; `0.0`
  always passes gate so port can drop the first half of the gate (only mode==2).
- `m_pSpeedControl` member at WaveManager+0x00 — the port's current member is
  `Random m_Random` at +0x00, which is wrong. Either rename / re-place to
  match binary, or keep port's stub-allocated `SpeedControl*` member somewhere
  else and document the divergence.
- `SpeedControl` HUD widget — not ported yet. Stub the `if (m_pSpeedControl
  == nullptr)` branch to no-op (still do the speed lerp + decay).
- `GetWavedt` / `ResetSpeed` already implemented per port.

### 3.2 `ResetSpeed(int playerIdx)` @ 0x00122e94 — STUB

**Binary pseudocode:**

```c
void WaveManager::ResetSpeed(int playerIdx) {
    m_Speed[1 + playerIdx]      = 0.0f;       // +0x58 + p*4 (overlap with m_Speed[1] for p=0)
    m_Speed[playerIdx]          = 0.0f;       // +0x54 + p*4
    m_SpeedLossTime[playerIdx]  = 0.0f;       // +0x4c + p*4

    // One-shot init of "blitz_bonus" string hash (lazy guard).
    static uint32_t s_blitzBonusHash = 0;
    if (s_blitzBonusHash == 0)
        s_blitzBonusHash = StringHash("blitz_bonus");
    game->pSaveData->ClearTotal(s_blitzBonusHash);

    m_ComboTimer[playerIdx] = 0.0f;            // +0x60 + p*4
    m_BlitzBonus[playerIdx] = 0;               // +0x5c + p*4

    // If a SpeedControl HUD widget is alive, reset its displayed/raw values.
    if (m_pSpeedControl != nullptr) {
        m_pSpeedControl->m_RawSpeed       = 0.0f;
        m_pSpeedControl->m_DisplayedSpeed = 0.0f;
    }
}
```

**Port-side action:** `WaveManager.cpp:1249` — replace stub. Notes:
- `1 + playerIdx` indexing: writing to +0x58 (m_Speed[1]) for p=0 looks like
  a binary bug, but it's intentional (combo-speed slot is overlapping). Port
  should preserve.
- `StringHash("blitz_bonus")` — implement lazily once, cache in static.
- `pSaveData->ClearTotal(uint32_t hash)` — already on FruitSaveData.
- HUD widget reset can be guarded by `m_pSpeedControl` being non-null
  (currently always null in port).

### 3.3 `AddSpeed(float amount, int playerIdx)` @ 0x00123510 — STUB

**Binary pseudocode (highly truncated; full SFX/score side-effects):**

```c
void WaveManager::AddSpeed(float amount, int playerIdx) {
    float v = m_Speed[1 + playerIdx] + amount;
    if (v <= 0.0f) v = 0.0f;
    if (v >= 14.0f) v = 14.0f;
    m_Speed[1 + playerIdx] = v;

    if (amount <= 0.0f) return;

    // Lazy-init "blitz_bonus" hash.
    static uint32_t s_blitzBonusHash = 0;
    if (!s_blitzBonusHash) s_blitzBonusHash = StringHash("blitz_bonus");

    m_SpeedLossTime[playerIdx] = 1.0f;          // +0x4c reset to 1.0 (full timer)

    if (m_ComboTimer[playerIdx] <= 0.0f) {
        // Cold start path.
        if (m_Speed[1 + playerIdx] > 2.9f) {           // DAT_00123828
            FruitSaveData* sd = game->pSaveData;
            m_ComboTimer[playerIdx] = 2.5f;            // 0x40200000

            sd->ClearTotal(s_blitzBonusHash);
            int newCount = sd->AddToTotal("blitz_bonus", s_blitzBonusHash, 1, false, false);
            m_BlitzBonus[playerIdx] = newCount;

            AddToCurrentScore(5, playerIdx, false, false);

            // Lazy-init "blitz_count" hash for activate effect.
            static uint32_t s_blitzCountHash = 0;
            if (!s_blitzCountHash) s_blitzCountHash = StringHash("blitz_count");
            PowerUpManager::GetInstance()->ActivateScreenEffect(s_blitzCountHash);

            // SFX: "blitz" sample (no level suffix).
            game->soundMgr->SFXPlay("blitz", 0.0f, 1.0f, sfxDelegate);
        }
    } else {
        // Combo continuation path.
        m_ComboTimer[playerIdx] -= amount;
        if (m_ComboTimer[playerIdx] <= 0.0f) {
            FruitSaveData* sd = game->pSaveData;
            int newCount = sd->AddToTotal("blitz_bonus", s_blitzBonusHash, 1, false, false);

            int level = (newCount < 6) ? newCount : 6;   // clamp 1..6
            m_BlitzBonus[playerIdx] = newCount;

            char buf[16];
            snprintf(buf, 16, "blitz_%d_count", level);   // DAT_0012383c format string
            uint32_t hash = StringHash(buf);
            PowerUpManager::GetInstance()->ActivateScreenEffect(hash);

            // SFX: blitz_<level> sample (1..6).
            int sfxIdx = (level > 1) ? level - 1 : 0;
            const char* sfxName = sfxNameTable[sfxIdx];   // DAT_00123840 + i*4
            game->soundMgr->SFXPlay(sfxName, 0.0f, 1.0f, sfxDelegate);

            // Score award: 5 * level.
            int clamped = (m_BlitzBonus[playerIdx] > 5) ? 6 : m_BlitzBonus[playerIdx];
            AddToCurrentScore(clamped * 5, playerIdx, false, false);
            m_ComboTimer[playerIdx] = 2.5f;            // reset combo timer
        }
    }

    // Update "blitz_max" stat. Lazy-init hash.
    static uint32_t s_blitzMaxHash = 0;
    if (!s_blitzMaxHash) s_blitzMaxHash = StringHash("blitz_max");
    int existing = game->pSaveData->GetTotal(s_blitzMaxHash);
    int delta    = m_BlitzBonus[playerIdx] - existing;
    if (delta > 0)
        game->pSaveData->AddToTotal("blitz_max", s_blitzMaxHash, delta, false, false);
}
```

**Port-side action:** `WaveManager.cpp:1253` — replace simplified stub with
full body. Dependencies:
- `game->pSaveData->AddToTotal/GetTotal/ClearTotal` — already in port.
- `AddToCurrentScore(score, player, false, false)` — exists per `Score.cpp`.
- `PowerUpManager::ActivateScreenEffect(hash)` — TODO upstream.
- `GameSound::SFXPlay(name, volume, pitch, delegate)` — exists per port.
- `StringHash` — present.
- The sound name table at `DAT_00123840` is 6 entries: presumably
  `"blitz_2", "blitz_3", "blitz_4", "blitz_5", "blitz_6"` plus base `"blitz"`.
  Need to read the GOT pointers individually; defer SFX until dispatcher
  available.

The current port body covers the speed lerp portion only — the SFX/score
side effects are absent.

### 3.4 `AddToSpeedLossTime(float amount, int playerIdx)` @ 0x001218ac — STUB

**Binary pseudocode:**

```c
void WaveManager::AddToSpeedLossTime(float amount, int playerIdx) {
    if (m_SpeedLossTime[playerIdx] > 0.0f) {           // +0x4c + p*4
        float v = m_SpeedLossTime[playerIdx] + amount;
        if (v < 1.0f) v = 1.0f;                        // clamp UP to 1.0 if would dip
        m_SpeedLossTime[playerIdx] = v;
    }
}
```

Note: the clamp is `< 1.0 → 1.0`, NOT `>` — so if `amount` is negative and
result drops below 1.0, it pins at 1.0. If timer is already <= 0, no-op.

**Port-side action:** `WaveManager.cpp:1245` — implement.

### 3.5 `ResetGlobalDt(float dt)` @ 0x00121ed8 — STUB

**Binary pseudocode:**

```c
void WaveManager::ResetGlobalDt(float dt) {
    // Walk the per-mode (game->gameMode) PROBABILITY_OVERIDE vector.
    // Erase entries whose (+0x74 = m_PerWaveCount) is >= 0; advance
    // iterator on those whose +0x74 < 0.
    auto& vec = probOverrides[game->gameMode];
    for (auto it = vec.begin(); it != vec.end(); ) {
        if (it->m_PerWaveCount < 0) {
            ++it;
        } else {
            it = vec.erase(it);
        }
    }
    m_GlobalDt    = dt;            // +0x74
    m_StepAccum   = 0.0f;          // +0x2d4 (DAT_00121f68 = 0)
}
```

**Port-side action:** `WaveManager.cpp:617` — implement. Note: the binary
selects the per-mode vector via `*(byte*)(game+4) * 0xc + +0x1fc`, where
`+0x1fc` is the per-mode probOverrides base. The port uses
`probOverrides[gameMode]` directly.

### 3.6 `CriticalMode(int playerIdx)` @ 0x001219e4 — STUB returning false

**Binary pseudocode:**

```c
bool WaveManager::CriticalMode(int playerIdx) {
    float chance = GetCriticalChance(playerIdx);
    // *(int*)(GOT+DAT_00121a18) = global Random*; ** = first int read from random state
    int   randVal = **((int**)global_Random);   // a single int from the RNG state
    return (float)(randVal / 2) < chance;
}
```

**WARNING: the `**(int**)(...)` pattern** — Ghidra is dereferencing a global
Random pointer and reading the first 4 bytes. This is reading the seed/state
directly, NOT calling `Rand32`. This is unusual; it might be reading some
RNG-derived counter (like a "frames_since_critical" int).

The `(longlong)` cast then `/ 2` then convert to float says: `(float)((int)X / 2) < chance`.

**Port-side action:** `WaveManager.cpp:1226` — investigate global Random
state slot before implementing. Stubbing to `false` is fine for now; document
as `TODO: needs global RNG state slot`. The function is called only by
`Fruit::Slice` (critical multiplier path) — so stub-returning false means no
critical hits ever, but does not crash.

### 3.7 `GetCurrentOverideList(int playerIdx)` @ 0x0012180c — STUB returning nullptr

**Binary pseudocode:**

```c
PROBABILITY_OVERIDE* WaveManager::GetCurrentOverideList(int playerIdx) {
    // Returns a pointer/iterator into the per-(gameMode, playerIdx) override vector.
    return (PROBABILITY_OVERIDE*)(
        (uint8_t*)this + 0x1fc
        + game->gameMode * 0xc
        + playerIdx * 0x30
    );
}
```

This returns a **pointer to the vector's header (begin/size/cap)**, not a
PROBABILITY_OVERIDE element. Callers cast to `vector<PROBABILITY_OVERIDE>*`
and iterate. The doubled `0x30` per-player + `0xc` per-mode is the layout
explained in §1: probOverrides is a flat 8-vector array, indexed by
`mode * 0xc + player * 0x30`.

**Port-side action:** `WaveManager.cpp:1236` — return
`&probOverrides[gameMode][playerIdx]` (or treat as flat 8-vector). Match the
indexing scheme.

### 3.8 `GetComboBonusProgression(int playerIdx)` @ 0x00121840 — STUB returning 0

**Binary pseudocode:**

```c
float WaveManager::GetComboBonusProgression(int playerIdx) {
    float progress = m_ComboTimer[playerIdx] / -2.5f + 1.0f;   // 0..1 timer-based
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    // Fold blitz-bonus count into progression.
    float result = ((float)(int)m_BlitzBonus[playerIdx] + progress) / 6.0f;
    if (result > 1.0f) result = 1.0f;
    return result;
}
```

`m_ComboTimer` is the per-player timer at `+0x60 + p*4`. Combo timer ranges
from 0..2.5 (set by AddSpeed); after 2.5 seconds, `progress` is 0.0; near
0.0, `progress` is 1.0. Then blitz bonus count is added to that fractional
progress, divided by 6, capped at 1.

**Port-side action:** `WaveManager.cpp:1232` — implement.

### 3.9 `SetCurrentWave(int waveNo, float delay, int playerIdx)` @ 0x00125340 — PARTIAL

**Binary pseudocode:**

```c
void WaveManager::SetCurrentWave(int waveNo, float delay, int playerIdx) {
    ClearUnspawned();
    *(int*)(&m_pCurrentWave[1] + playerIdx) = waveNo - 1;   // writes to int storage at +0x230+p*4
    GetNextWave(0);
    float v = m_WaveDelay[playerIdx] + delay;
    if (v < 0.0f) v = 0.0f;
    m_WaveDelay[playerIdx] = v;
}
```

Port matches at `WaveManager.cpp:910`. Note the binary writes `waveNo - 1` to
`(&m_pCurrentWave_P1)[playerIdx]` which is the wave-count slot (binary
overloads m_pCurrentWave[1] and m_WaveCount). Port's `m_WaveCount[playerIdx]`
is the same storage. The port also passes `0` to GetNextWave (not
`playerIdx`), matching the binary.

**Port-side action:** `WaveManager.cpp:910` — body matches the binary.
Verified MATCHES.

### 3.10 `SetupWaveQue()` @ 0x00124564 — STUB (not used in shipped game modes)

**Binary pseudocode (abbreviated; ~140 lines):**

```c
void WaveManager::SetupWaveQue() {
    // Free existing queue + queue item.
    if (m_pWaveQue) { delete m_pWaveQue; m_pWaveQue = nullptr; }
    m_pWaveQue = new WaveQue();   // 0xc bytes
    m_pWaveQue->m_field0 = 0;
    m_pWaveQue->m_field4 = 0;
    m_pWaveQue->m_field8 = 0;     // initial scalar (DAT_001247e0)
    WaveQue::WaveQue(m_pWaveQue);

    if (m_pWaveQueItem) { delete m_pWaveQueItem; m_pWaveQueItem = nullptr; }
    m_pWaveQueItem = new WaveQueItem();   // 0x1c bytes
    memset(m_pWaveQueItem, 0, 0x1c);
    WaveQueItem::WaveQueItem(m_pWaveQueItem);

    // Sum all wave Chance values.
    int sumChance = 0;
    for (WAVE_INFO* wi : waveInfos[gameMode])
        sumChance += wi->m_Chance;

    // Loop while remaining time > 1.0 (starts at 27.0).
    float remaining = 27.0f;
    while (remaining > 1.0f) {
        // Pick a wave by weight (Rand32(sumChance), iterate, deduct).
        uint32_t roll = Rand32(globalRng, sumChance);
        WAVE_INFO* picked = waveInfos[gameMode][0];
        for (WAVE_INFO* wi : waveInfos[gameMode]) {
            if ((int)roll <= 0) break;
            picked = wi;
            roll -= wi->m_Chance;
        }
        // If picked wave's m_NextWaveWait is too large for remaining, re-roll.
        while (remaining + 1.0f < picked->m_NextWaveWait) {
            roll = Rand32(globalRng, sumChance);
            // ... iterate again
            for (WAVE_INFO* wi : waveInfos[gameMode]) {
                if ((int)roll <= 0) break;
                picked = wi;
                roll -= wi->m_Chance;
            }
        }
        remaining -= picked->m_NextWaveWait;
        m_pWaveQue->AddWave(picked, false);
    }

    // Finalise: set queue scalar to 27.0 (0x41d80000).
    m_pWaveQue->m_field8 = 27.0f;
    m_pWaveQue->RandomiseOrder(true);

    // Append the second-last wave from waveInfos[gameMode] FOUR times.
    for (int i = 0; i < 4; ++i) {
        WAVE_INFO* second_last = waveInfos[gameMode][waveInfos[gameMode].size() - 2];
        m_pWaveQue->AddWave(second_last, true);
    }
    m_pWaveQue->AddSpecials();
    // Then append the last wave once.
    WAVE_INFO* last = waveInfos[gameMode][waveInfos[gameMode].size() - 1];
    m_pWaveQue->AddWave(last, true);
}
```

**Port-side action:** `WaveManager.cpp:921` — leave stub for now since
SetupWaveQue is only used by Combo / Survival modes (not in primary
gameplay). Document `WaveQue` / `WaveQueItem` layouts when those modes
are ported.

### 3.11 `Resume()` @ 0x00124b1c — RECENTLY IMPLEMENTED, partially incomplete

**Binary pseudocode (~210 lines; current port body matches the high-level
shape but with several TODOs):**

The port currently captures:
- Score/MissCount restore (TODO)
- `field_0x4c <- sd->m_Speed_P0`, `field_0x60 <- sd->m_Speed_P1`
- `m_FruitQueueSize` reset
- Re-roll all PROBABILITY_OVERIDE (TODO; SelectType not ported)
- Entity respawn (TODO; deps missing)
- Branch on game-over vs wave-state restore
- Wave-state restore loop (TODO; SPAWNER_INFO state restore stub)

**Gaps in port (per binary):**

1. `SetScore(sd->m_CurrentScore, 0)` and `SetMissCount(sd->m_CurrentMissCount, 0)` —
   not yet ported (Score API gap).
2. `game->m_bWasGameOver = sd->m_bWasGameOver` — game struct missing field.
3. PROBABILITY_OVERIDE::SelectType() — not ported (depends on Fruit::FruitType
   random pick).
4. Entity respawn loop — depends on `ActorManager::Add` proper, Fruit/Bomb
   `Init` vtable, `Bomb::SetForPlayer/SetHit/Chuck`, `Fruit::Chuck`, all
   present per port but the type-detection (`base+0x35`) is fragile.
5. `ActorManager::Update(0)` — settle frame to apply gravity once.
6. `PowerUpManager::LoadTextures()` (mode==2 only) — TODO upstream.
7. SkipToGameOver/SkipToPause — both stubbed in port; one or the other gets
   called depending on save-state branch.
8. Wave-state SpawnState restore loop — port stops short of restoring per-
   spawner state (`+0x5c m_SpawnTimer`, `+0x50 m_RemainingCount`, +0x54 etc.).
9. `field_0x40` (PausedGame state byte from save fade-screen) restore — TODO.
10. `m_EntityStates.clear()` — port does this. ✓

**Port-side action:** `WaveManager.cpp:501` — plus the gaps above. Most are
upstream API blockers; flag these in the port comments next to each TODO.

### 3.12 `SaveWaveInfo(FruitSaveData* sd)` @ 0x001247f0 — STUB returning 0

**Binary pseudocode:**

```c
int WaveManager::SaveWaveInfo(FruitSaveData* sd) {
    // Default-zero the speed snapshots.
    sd->m_Speed_P0       = 0.0f;     // +0x100
    sd->m_Speed_P0_alias = 0.0f;     // +0x104
    sd->m_Speed_P1       = 0.0f;     // +0x108

    sd->m_blitzSpawnedThisGame     = (uint8_t)m_BlitzSpawnedThisGame;       // +0x188
    sd->m_blitzSpawnTime           = m_BlitzSpawnTime;                       // +0x190
    sd->m_blitzForceSpawnedCounter = (uint8_t)m_BlitzForceSpawnedCounter;   // +0x18c

    sd->m_WaveStates.clear();    // list at +0x150

    // Sentinel: only save if we're not a multi-player wave-2-player split,
    // OR m_pCurrentWave[1] (waveCount) is < 0, AND there are waves loaded.
    if ((!game->m_bSplitPlayerWaves || (int)m_pCurrentWave[1] < 0)
        && waveInfos[gameMode].size() != 0)
    {
        sd->m_ProbabilityOverideFlag = m_GlobalDt;   // +0x14c

        // Collect candidate WAVE_INFO* whose [m_ScoreThreshold, m_EndScore]
        // straddles the current wave count.
        WAVE_INFO* candidates[20];
        int candidateIdx[20];
        int numCandidates = 0;
        int waveIdx = 0;
        for (WAVE_INFO* wi : waveInfos[gameMode]) {
            if (wi->m_ScoreThreshold <= m_WaveCount[0]
                && (m_WaveCount[0] <= wi->m_EndScore || wi->m_EndScore == -2))
            {
                candidates[numCandidates] = wi;
                candidateIdx[numCandidates] = waveIdx;
                ++numCandidates;
            }
            ++waveIdx;
        }

        // Build a WaveState for each candidate; only the "current" one
        // (m_pCurrentWave[0]) gets its SpawnState list populated.
        for (int i = 0; i < numCandidates; ++i) {
            WaveState state;
            state.index = candidateIdx[i];
            state.waveIdx = (int)candidates[i]->m_WaveRevisitCount;
            state.spawners.clear();
            if (candidates[i] == m_pCurrentWave[0]) {
                for (int s = 0; s < candidates[i]->m_SpawnerCount; ++s) {
                    SpawnState ss;
                    SPAWNER_INFO& sp = candidates[i]->m_pSpawners[s];
                    ss.delay   = sp.m_SpawnTimer;       // +0x5c
                    ss.toSpawn = sp.m_RemainingCount;   // +0x50
                    state.spawners.push_back(ss);
                }
            }
            sd->m_WaveStates.push_back(state);
        }

        sd->m_pCurrentWave_P1 = (int)m_pCurrentWave[1];   // raw int (waveCount)
        sd->m_FruitQueueCount = m_FruitQueueSize[0];
        sd->m_WaveDelay       = m_WaveDelay[0];
        sd->m_WaveWait        = m_WaveWait[0];
        sd->m_Speed_P0        = m_SpeedLossTime[0];      // +0x4c
        sd->m_Speed_P1        = m_ComboTimer[0];         // +0x60
        sd->m_Speed_P0_alias  = m_Speed[1];
        memcpy(&sd->m_FruitQueue[0], &m_FruitQueue[0][0], 0x80);
        return 1;
    }
    return 0;
}
```

**Port-side action:** `WaveManager.cpp:600` — replace stub. Note the
sentinel `game->m_bSplitPlayerWaves` is the new `+0x114` field per Init §1.

### 3.13 `Draw(int playerIdx)` @ 0x00122ae8 — STUB

**Binary pseudocode:**

```c
WaveManager* WaveManager::Draw(int playerIdx) {
    if (playerIdx == 0) {
        PowerUpManager::GetInstance();
        PowerUpManager::Draw();
    }
    return this;
}
```

That's it — the entire body. WaveManager doesn't draw the wave banner; it
just delegates to PowerUpManager::Draw() once per frame for player 0.

**Port-side action:** `WaveManager.cpp:1189` — replace stub with single call
to `PowerUpManager::Draw()` once that's ported. Currently safe to leave as
no-op since PowerUpManager isn't drawn yet.

### 3.14 `DeleteSpeedControl(HUDControl* c)` @ 0x001217d4 — STUB

**Binary pseudocode:**

```c
void WaveManager::DeleteSpeedControl(HUDControl* c) {
    if (m_pSpeedControl == c)   // pointer at WaveManager+0x00
        m_pSpeedControl = nullptr;
}
```

**Port-side action:** `WaveManager.cpp:1193` — implement, but only after
`m_pSpeedControl` member is in place (see §1 layout note).

### 3.15 `GetSpeed(int playerIdx)` @ 0x00121834 — MATCHES

Already implemented identically to binary (`return m_Speed[playerIdx];`).

### 3.16 `GetWavedt(int playerIdx)` @ 0x001218dc — MATCHES

Port at `WaveManager.cpp:1205` matches binary semantics. Both:
- Compute `waveDt = m_WaveDt + m_WaveDtInc * revisit + m_WaveDtSpInc * m_Speed[p]`
- Multiply by `m_GlobalDt * m_DtMod` for player 0; else 1.0
- Clamp result to [0.0, 100.0] (DAT_00121974/8).

### 3.17 `GetCriticalChance(int playerIdx)` @ 0x001219c4 — MATCHES

Port matches binary: `wave ? wave->m_CriticalChance : 1.0f` times
`m_CritChanceMult`.

### 3.18 BombScale / BombMultiplyer / FruitMultiplyer / CriticalChanceMod — MATCHES

All four single-line stubs match binary exactly. Each does `field *= mult`
for one of the four power-up modifier slots. Port matches.

---

## 4. Wave-state-machine flow diagram

```
                                 +---------------------+
                                 |    Reset(full)      |
                                 |---------------------|
                                 | m_bWaveActive=1     |
                                 | m_bWaveSpawnedP0=1  |
                                 | m_WaveCount[0/1]=-1 |
                                 | m_StepAccum=0       |
                                 | All timers=0        |
                                 | First GetNextWave(0)|
                                 +----------+----------+
                                            |
                                            v
                                +-------------------------+
                                |  GetNextWave(playerIdx) |  <----------+
                                |-------------------------|             |
                                | ++m_WaveCount[p]        |             |
                                | revisit++ if reentered  |             |
                                | Pick by weighted RNG    |             |
                                | over candidates whose   |             |
                                | scoreThreshold..EndScore|             |
                                | brackets m_WaveCount[p] |             |
                                | Build FruitQueue if     |             |
                                | m_SpecialFruits         |             |
                                | Reset spawners          |             |
                                | m_WaveDelay[p] := delay |             |
                                | m_WaveWait[p]  := wait  |             |
                                | (See §5 PROBABILITY_OVE.|             |
                                | counters decrement)     |             |
                                +-----------+-------------+             |
                                            |                           |
                                            v                           |
                                  +---------------------+               |
                                  |   Update(dt)        |               |
                                  |---------------------|               |
                                  | Reset m_CritMult=1, |               |
                                  | m_DtMod=1, m_FruitMt|               |
                                  | =1, etc.            |               |
                                  | PowerUpManager::Upd |               |
                                  | ate(dt) (if powers  |               |
                                  | enabled & game not  |               |
                                  | paused) -> m_DtMod  |               |
                                  | m_GlobalDt accumul: |               |
                                  | clamp[+0x8c..+0x9c] |               |
                                  | Step accum: 1/60 dt |               |
                                  | UpdateWave(1/60,0,0)|               |
                                  +-----------+---------+               |
                                              |                         |
                                              v                         |
                  +-----------------------------------------+            |
                  |   UpdateWave(1/60, p=0, unk=0)          |            |
                  |-----------------------------------------|            |
                  | UpdateComboSpeed(dt) (Arcade only)      |            |
                  | game[+0x170](paused) gate -> playtime++ |            |
                  | UpdateNetworking() -> 0                  |            |
                  | If wave != null:                         |            |
                  |   if m_WaveDelay[p] > 0 (pre-spawn):    |            |
                  |     m_WaveDelay[p] -= dt; mark "still   |            |
                  |     waiting" (block bottom GetNextWave) |            |
                  |   else:                                  |            |
                  |     for each spawner s:                  |            |
                  |       s.m_SpawnTimer -= dt * dtMod      |            |
                  |       while s.m_RemainingCount>0 &      |            |
                  |             s.m_SpawnTimer<=0:           |            |
                  |         pick fruit type                  |            |
                  |         (PROBABILITY_OVERIDE may         |            |
                  |          override -- §5)                  |            |
                  |         if -2 -> SpawnBomb                |            |
                  |         elif -1 -> Random fruit          |            |
                  |         else -> SpawnFruit(type)         |            |
                  |         m_bWaveSpawnedP0 = 1              |            |
                  |         s.m_RemainingCount--            |            |
                  |         s.m_SpawnTimer +=                |            |
                  |           max(0, s.m_Delay -             |            |
                  |               s.m_DelayInc * revisit)    |            |
                  +-----------------+-----------------------+            |
                                    |                                    |
                                    v                                    |
                  +-----------------------------------------+            |
                  |   IsWaveProcessing(p) check             |            |
                  |-----------------------------------------|            |
                  |  If m_bWaveSpawnedP0 == 0 -> false      |            |
                  |  If wave != null and                     |            |
                  |     wave->m_bWaitForProcessing:          |            |
                  |       short-circuit on Fruit/Bomb count  |            |
                  |  else fall through to                    |            |
                  |       ActorManager::GetNumEntities       |            |
                  |  If 0 entities AND no actor activity:   |            |
                  |       m_bWaveSpawnedP0 = 0; return false|            |
                  +-----------------+-----------------------+            |
                                    | (not processing)                   |
                                    v                                    |
                  +-----------------------------------------+            |
                  |   m_WaveWait[p] gate                    |            |
                  |-----------------------------------------|            |
                  | if m_WaveWait[p] > 0:                   |            |
                  |   m_WaveWait[p] -= dt                    |            |
                  |   if still > 0: keep waiting             |            |
                  |  else: GetNextWave(p) ----------------- + (loop)
                  +-----------------------------------------+
```

Key timer slot assignments per Fix-1/Fix-4:
- `+0x234[p]` = "delay slot" = pre-spawn delay (decrements while >0; gate
  for spawn loop).
- `+0x238[p]` = "wait slot" = wave-end wait (decrements after IsWaveProcessing
  returns false; gate for GetNextWave).

**Critical: the binary falls through from delay-decrement to spawn loop
with no early-return** — the port had to remove a spurious `return` to match.

---

## 5. PROBABILITY_OVERIDE — full power-up fruit selection logic

The "random fruit" branch in UpdateWave (binary @ 0x001254f2 → 0x001256f2)
is a complex blitz/power-up bomb selection state machine. Pseudocode:

```c
// Inside UpdateWave's spawner loop, when fruitType == -1 (random):
//   spawner is the SPAWNER_INFO* at wave->m_pSpawners[i]
//   this_01 is the per-(mode,player) PROBABILITY_OVERIDE vector

// Check Arcade-mode countdown timer gate.
bool blitzGate = false;
if (game->gameMode == 2) {
    TimeControl* tc = game->pSomeFlow + 0x180;
    float threshold = tc->m_field7c;
    if (tc->GetCountDown() - m_BlitzSpawnTime >= threshold) {
        // Time threshold met -- enter blitz force-spawn flow.
        if (m_BlitzForceSpawnedCounter == 0) {
            m_BlitzForceSpawnedCounter = 1;
            blitzGate = (m_BlitzSpawnedThisGame > 1) ? 0 : (1 - m_BlitzSpawnedThisGame);
            // Random new threshold for next spawn 10..20 seconds.
            m_BlitzSpawnTime = RandF(globalRng, 10.0f) + 10.0f;
        } else if (m_BlitzForceSpawnedCounter == 1
                   && m_BlitzSpawnedThisGame == 1) {
            m_BlitzForceSpawnedCounter = 2;
            blitzGate = 1;
        } else {
            blitzGate = 0;
        }
    } else {
        blitzGate = 0;
    }
} else {
    // Non-Arcade: skip the blitz time-gate logic.
    blitzGate = 0;
}

// Pick a random PROBABILITY_OVERIDE entry weighted by m_PercentChance.
int    cumulative = 0;
uint32_t roll = Rand32(globalRng, wave->m_OverideProbabilityPool);   // wave +0x70
for (int u = 0; u < probOverrides.size(); ++u) {
    PROBABILITY_OVERIDE& po = probOverrides[u];
    bool eligible = (po.m_Counter < po.m_PerWave) || (po.m_PerWave < 0);
    if (eligible) {
        // Check disable-when-powered.
        float dwp = po.m_DisableWhenPowered;
        float spawnT = (spawner->m_SpawnTimer < 0.0f) ? 0.0f : spawner->m_SpawnTimer;
        float prog = PowerUpManager::GetActiveProgression(spawnT);
        if (dwp >= prog) eligible = false;
        // Check m_WaveCount[0] vs po.m_PerWaveCount.
        if (m_WaveCount[0] >= 0 && m_WaveCount[0] < po.m_PerWaveCount)
            eligible = false;
    }
    if (eligible) {
        // Eligible: try to spawn one, weighted.
        // Already-counted check (else clause inside loop):
        if (po.m_Counter > 0 && blitzGate != 0)
            ; // fall through to selection
    } else {
        if (po.m_Counter > 0 && blitzGate != 0)
            ; // fall through anyway because of blitzGate
    }
    int chance = po.m_PercentChance;
    if (m_BlitzSpawnedThisGame > 5) chance >>= 1;   // half if >5 spawned
    cumulative += chance;
    if ((int)roll < cumulative
        || (po.m_PercentChance > 0 && blitzGate != 0))
    {
        long type = po.GetType();
        if (type >= 0 && type < FRUIT_INFO_COUNT) {
            FRUIT_INFO* fi = Fruit::FruitInfo(type);
            if (fi->m_pPowers != nullptr) {
                if (fi->m_pPowers->AnyActivePowers()) break;  // already active
                // Lazy-init: set up 3 fixed bomb-spawner templates (bottom/left/right).
                static SPAWNER_INFO s_bombTemplates[3];
                static bool s_bombTemplatesInit = false;
                if (!s_bombTemplatesInit) {
                    SPAWNER_INFO::SPAWNER_INFO(&s_bombTemplates[0]);
                    SPAWNER_INFO::SPAWNER_INFO(&s_bombTemplates[1]);
                    SPAWNER_INFO::SPAWNER_INFO(&s_bombTemplates[2]);

                    // Override ctor defaults for each template.
                    s_bombTemplates[0].m_SpawnType = 1;   // BOTTOM_SLOW
                    s_bombTemplates[0].m_Gravity = (DAT_001259b0, -1.1f, 0); // (0,-1.1,0)
                    s_bombTemplates[0].m_VelXScale = 0.66666f;  // DAT_001259b8
                    // ... etc (16 fields per template)
                    // (Templates are 100 bytes apart at iVar11+0x4, +0x68, +0xcc.)

                    s_bombTemplatesInit = true;
                }
                // Pick one of the 3 templates randomly.
                uint32_t bombChoice = Rand32(globalRng, 3);
                spawner = (SPAWNER_INFO*)&s_bombTemplates[bombChoice];
            }
            po.m_Counter += 1;
            // Check fruit-multiplier gate.
            if (m_FruitMult <= 0.0f
                || somePowerCount > 0
                || (game->fruitProgressTimer < 8.0f && fi->m_pPowers->FirstHash != hash("blitz_bonus")))
                break;
            // ----> spawn this type via the bomb template
            if (!fi->m_pPowers->AnyActivePowers())
                ++m_BlitzSpawnedThisGame;
            break;
        }
    }
}
// Fall through to normal SpawnFruit/SpawnBomb branch with the chosen
// type and (possibly overridden) spawner.
```

**Port-side status:** the port currently calls
`int rft = Fruit::RandomFruit(false); SpawnFruit(1, rft, &spawner, ...)` for
the `fruitType == -1` case. This bypasses ALL of the above logic. Result:
no power-up fruits ever spawn; blitz mode never triggers in Arcade.

**Port-side action:** `WaveManager.cpp:743` — implement the full PROBABILITY_OVERIDE
selection. This requires:
- `PROBABILITY_OVERIDE::GetType()` — port stub
- `PowerUpManager::GetActiveProgression(t)` — TODO upstream
- `FRUIT_INFO::m_pPowers->AnyActivePowers()` — partly ported
- `TimeControl::GetCountDown()` — required for Arcade time gate
- The 3 lazy-initialised `SPAWNER_INFO` bomb-template statics (bottom-slow,
  left, right) with hand-coded gravity/velocity offsets.

**Defer:** until PowerUpManager and TimeControl are ported, leave the
`rft = Fruit::RandomFruit(false)` fallback. Add a comment block flagging the
gap with reference to this section.

---

## 6. Port-side action list

Concrete edits the implementer should apply. Each line is `file:line — change`.

### Critical (gameplay-breaking)

1. `src/game/WaveStructs.h:88..91` — change `SPAWNER_INFO` ctor:
   - `m_SpawnMin(0.0f)` (was 1.0f) — matches binary
   - `m_SpawnMax(0.0f)` (was 1.0f) — matches binary

2. `src/game/WaveStructs.h:191..205` — change `WAVE_INFO` ctor defaults:
   - `m_EndScore(-1)` (was -2) — binary ctor sets -1; Init then re-clamps
   - `m_WaveDt(1.0f)` (was 0.9f)
   - `m_NextWaveDelay(2.0f)` (was 0.0f) — major change
   - `m_Chance(10)` (was 90)
   - `m_ChanceRegrowth(0.25f)` (was 0.33f)
   - `m_GamesMin(-1), m_GamesMax(-1)` (were 0)
   - `m_OverideProbabilityPool(100)` (was 0)
   - `field_0x34(1.0f)` (was 0.0f) — wave revisit counter starts at 1, not 0
   - Note: Init then overrides most via `<defaults>` parsing, but bare ctor
     defaults still affect ResetWaveChances and tests.

3. `src/game/WaveStructs.h:155..167` — add new fields at the correct offsets:
   - `+0x40 m_CurrentChance` (int, mirror of `m_Chance`)
   - `+0x48 m_CurrentRegrowth` (float, mirror of `m_ChanceRegrowth`)
   - Drop the tail `m_CurrentMax` int (replaced by `m_CurrentChance`).

4. `src/game/WaveStructs.h:271..289` — extend `PROBABILITY_OVERIDE`:
   - Add `int m_TypeQueue[20]` at +0x18..+0x68 (init -1)
   - Type the +0x6c slot as `float m_DisableWhenPowered` (currently OK)
   - Type the +0x70 slot as `int m_PerWaveCount` (currently `m_WaveCount`)
   - Confirm the `m_PercentChance` is read as int OR float — binary stores
     as int and treats as int in the `cumulative += chance` weight loop.
     Suggest **change `m_PercentChance` from `float` → `int`** to match binary.

### Method bodies to fill in

5. `src/game/WaveManager.cpp:617` — `ResetGlobalDt`: walk `probOverrides[gameMode]`,
   erase entries with `m_PerWaveCount >= 0`, advance otherwise; set `m_GlobalDt = dt`,
   `m_StepAccum = 0`.

6. `src/game/WaveManager.cpp:776` — `UpdateComboSpeed`: full body per §3.1.

7. `src/game/WaveManager.cpp:1245` — `AddToSpeedLossTime`: 6-line clamp logic
   per §3.4.

8. `src/game/WaveManager.cpp:1249` — `ResetSpeed`: full body per §3.2.

9. `src/game/WaveManager.cpp:1253` — `AddSpeed`: replace simplified 6-line body
   with full SFX/score side effects per §3.3. Most calls (PowerUpManager,
   GameSound) are upstream-blocked; gate behind `if (deps_ready)` for now.

10. `src/game/WaveManager.cpp:1226` — `CriticalMode`: leave returning false
    until global Random state can be exposed (see §3.6 warning).

11. `src/game/WaveManager.cpp:1232` — `GetComboBonusProgression`: full body
    per §3.8.

12. `src/game/WaveManager.cpp:1236` — `GetCurrentOverideList`: return
    `&probOverrides[gameMode][playerIdx]` per §3.7.

13. `src/game/WaveManager.cpp:600` — `SaveWaveInfo`: full body per §3.12.

14. `src/game/WaveManager.cpp:1189` — `Draw`: replace stub with PowerUpManager
    delegation per §3.13.

15. `src/game/WaveManager.cpp:1193` — `DeleteSpeedControl`: 1-line clear per
    §3.14 (gated on `m_pSpeedControl` member existing).

### Lower-priority gaps

16. `src/game/WaveManager.cpp:743` — UpdateWave random branch: full
    PROBABILITY_OVERIDE selection per §5. Defer until PowerUpManager and
    TimeControl ported; current fallback is acceptable but blitz-fruit power-
    ups never spawn.

17. `src/game/WaveManager.cpp:921` — `SetupWaveQue`: leave stubbed; only used
    in unimplemented Combo/Survival modes.

18. `src/game/WaveManager.cpp:609..619` — `GameOver`/`NewGame`: per binary
    these are 3-line wrappers around `ResetGlobalDt(1.0)` and
    `PowerUpManager::Reset(true/false)`. Implement once `ResetGlobalDt` body
    is filled.

### Struct/layout review (RE-needed before edit)

19. `src/game/WaveManager.h:32` — `Random m_Random` at +0x00 may be incorrect
    (see §1 final note). Binary's +0x00 slot holds `SpeedControl*
    m_pSpeedControl`. The decompile's `*(Random**)(GOT + DAT_xxx)` references
    a global Random pointer, not a member. Confirm by:
    - Reading the disassembly of `WaveManager::WaveManager()` ctor (find by
      vtable or by xrefs to the singleton init).
    - If ctor doesn't construct a Random at +0x00, the port has the wrong
      layout.
    - **DO NOT** apply a fix without that verification — the port currently
      compiles and runs because every use of `m_Random` goes through the
      port's own Random instance and never dereferences via the binary's
      offset arithmetic.

### Sentinels / known gaps

- `Game.h` is missing `field_0x170` (paused-gate byte), `field_0x199` (MP-
  ready flag), `field_0x1a8`/`field_0x1ac` (device-orientation timer), and
  `field_0x470` (UpdateComboSpeed reset flag). All four are referenced by
  WaveManager but currently invisible in the port. None are critical for
  Classic-mode play.

- `FruitSaveData::m_field134` is the binary's "nextComboBonus" XML attr.
  Currently in port struct but unused.

- `IsOnlineMultiplayer`, `IsSameScreenMultiplayer`, `IsMultiplayer`,
  `PowersEnabled` — all utility predicates from `WaveManager` referenced by
  several methods. Stubbed to false in port; matches single-player assumption.
