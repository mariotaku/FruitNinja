# WaveManager::Init — XML Attribute Audit (binary @ 0x0012393c)

ASM-verified by reading every literal pool DAT in `WaveManager::Init`, resolving
each to the GOT-base-relative string, and matching to the actual instruction
that consumes it. Cross-checked against the four shipping XML files in
`FruitNinjaBada/Data/xml/`.

GOT base resolution: `r4 = 0x123950 + DAT_00123b70` ⇒ `r4 = 0x001ec130`.
Every `(GOT + DAT_xxx)` reference is the address of the literal **string**
itself (not a pointer-to-pointer). Strings are reused via C suffix-sharing
(e.g. the `"chance"` literal points 9 bytes into `"critical_chance"` at
0x001baae7).

Date: 2026-04-30. Auditor: asm-inspector.

---

## 1. Root cause of the watermelon-fly-up bug

The binary's `SPAWNER_INFO` constructor (`@ 0x001270ac`) initialises the
gravity Vec3 (`+0x18..+0x20`) to **`(0, -1, 0)`**.

The port's `SPAWNER_INFO()` constructor in `src/game/WaveStructs.h` initialises
it to `(0, 0, 0)`. When the wave XML has no `gravity` attribute on `<Spawn>`
(true for every wave in `originalwavelist.xml`, `combowavelist.xml`,
`zenwavelist.xml`, and most of `arcadewavelist.xml`), the port's SpawnFruit
then computes:

```cpp
f->m_Gravity = Vec3(info->m_Gravity_x * negGravY,   // 0 * negGravY = 0
                    info->m_Gravity_y * negGravY,   // 0 * negGravY = 0
                    info->m_Gravity_z * negGravY);  // 0 * negGravY = 0
```

…which gives the spawned fruit **zero gravity**, so the upward chuck velocity
is never opposed and the watermelon flies up indefinitely.

Fix: change `SPAWNER_INFO()` ctor to initialise `m_Gravity_y(-1.0f)`.

---

## 2. Attribute table — `<WaveInfo>` element

Target struct: `WAVE_INFO*` (size 0x78, 120 bytes).

| DAT addr   | XML attribute            | Type   | Binary target offset             | Port target field           | Verdict |
|------------|--------------------------|--------|----------------------------------|-----------------------------|---------|
| DAT_00123b84 | `waveNo`               | int    | local_38, conditional → +0x70 (then +0x0 mirror) | `m_WaveNumber` (+0x70) and `m_ScoreThreshold` mirror (+0x0) | MATCHES |
| DAT_00123b8c | `until`                | str    | atoi → +0x4 (clamped >= ScoreThreshold; "forever"→-2) | `m_EndScore` | MATCHES |
| DAT_00123b88 | `overideProbabiltyPool` | int   | direct → +0x70 *but only if waveNo wasn't set first*. Actually a separate slot; binary stores via `r2,#0x70` via different path. See note below. | `m_OverideProbabilityPool` (port has it as a tail field, no fixed offset) | DIVERGES — see §6 |
| DAT_001241a8 | `chance`               | int    | local_38, conditional → +0x3c       | `m_Chance` (port has as tail field) | DIVERGES — port stores at wrong offset |
| DAT_001241ac | `criticalChance`       | float  | +0x64                                | `m_CriticalChance` (+0x64) | MATCHES |
| DAT_001241b0 | `chanceRegrowth`       | float  | +0x44                                | `m_ChanceRegrowth` (port has at tail; +0x44 is `m_WaveDelay` in port) | DIVERGES — port has wrong field at +0x44 |
| DAT_001241b4 | `games`                | int    | +0x4c                                | `m_GamesMin` (port has tail field; +0x4c is `m_BombMin`) | DIVERGES — see §6 |
| DAT_001241b8 | `gamesMin`             | int    | +0x4c (overwrites previous)         | same | DIVERGES |
| DAT_001241bc | `gamesMax`             | int    | +0x50                                | `m_GamesMax` (port tail; +0x50 is `m_BombMax`) | DIVERGES |
| DAT_001241c0 | `coin_chances`         | element| if found → ParseCoinChanceinator → +0x6c | `m_pCoinChance` (+0x6c) | MATCHES |

After the int reads:
- `if (m_GamesMin < 0) m_GamesMin = m_GamesMax;`
- `if (m_GamesMax < 0) m_GamesMax = m_GamesMin;`

Note: there is **no `wavedelay` / `bombmin` / `bombmax` / `bombgravity` /
`bombspeed` / `bombspeedmax`** attribute reading in the binary. Those names
live only in the older spec doc; they are not present in the shipping XMLs.
The port's `m_BombMin` / `m_BombMax` / `m_BombGravity` / `m_BombSpeed*` fields
at +0x4c..+0x30 are misnamed legacy slots — the binary uses these for `chance`
/ `chanceRegrowth` / `gamesMin` / `gamesMax`.

---

## 3. Attribute table — `<Spawn>` child element

Target struct: `SPAWNER_INFO*` (size 0x64, 100 bytes).

| DAT addr | XML attribute | Type  | Binary target offset | Port target field      | Verdict |
|----------|---------------|-------|----------------------|------------------------|---------|
| DAT_001241c8 | `type`     | str   | SplitWords → +0x4 (vector), count → +0x10 | `m_FruitTypeNames` / `m_FruitTypeCount` | MATCHES |
| DAT_001241cc | `min`      | float | +0x38                | `m_SpawnMin` (+0x38)   | MATCHES |
| DAT_001241d0 | `max`      | float | +0x40                | `m_SpawnMax` (+0x40)   | MATCHES |
| DAT_001241d4 | `mininc`   | float | +0x44                | `m_MinInc` (port tail field; +0x44 is `m_Speed` in port) | DIVERGES — port stores at wrong offset |
| DAT_001241d8 | `maxinc`   | float | +0x44 (**overwrites**) | `m_MaxInc` (port tail) | DIVERGES — same slot in binary; port keeps two fields |
| DAT_001241dc | `delay`    | float | +0x48                | `m_ZOffset` (port has at +0x5c) | DIVERGES — wrong offset |
| DAT_001241e0 | `delayinc` | float | +0x4c                | `m_DelayInc` (port tail; +0x4c is `m_field4c`) | DIVERGES |
| DAT_001241e4 | `horizmin` | float | +0x2c                | `m_MinAngle` (+0x24 in port — WRONG offset) | DIVERGES |
| DAT_001241e8 | `horizmax` | float | +0x30                | `m_MaxAngle` (+0x28 in port) | DIVERGES |
| DAT_001241ec | `velscale` | float | +0x24, **and** copied to +0x28 | (no port handling for this combo) | DIVERGES |
| DAT_001241f0 | `velXscale`| float | +0x24                | `m_MinVel` (+0x2c in port) | DIVERGES |
| DAT_001241f4 | `velYscale`| float | +0x28                | `m_MaxVel` (+0x30 in port) | DIVERGES |
| DAT_001241f8 | `gravity`  | str→Vec3 | ParseVector → +0x18..+0x20 | `m_Gravity_x/y/z` (+0x18..+0x20) | MATCHES (offset); see §1 for ctor default |
| DAT_001241fc | `placement`| str→enum | ParsePlacement → +0x34 byte | `m_SpawnType` (+0x34) | MATCHES |
| DAT_00124200 | `mirror`   | bool  | +0x60 byte (1 if attr ≠ "false"; **0 if attr absent**) | `m_bMirror` (port has `m_bForceOnce` at +0x60 misnamed; `m_bMirror` is a separate tail field) | DIVERGES — see §6 |

After per-spawner attr reads, binary does:
`WAVE_INFO+0x74 (TotalWeight) += (int)((SpawnMin + SpawnMax) * 0.5)` —
this matches port.

The mirror attr clears to **0** when absent (`movs r3,#0; strb.w r3,[r5,#0x60]`),
unlike `waitForEntities` which defaults to 1. There is no XML use of `mirror`
in the shipping files; the binary always writes 0 to +0x60.

---

## 4. Attribute table — `<Wave_dt>` and `<NextWaveDelay>` children

### `<Wave_dt>`

| DAT addr | XML attribute | Type  | Binary target offset | Port target | Verdict |
|----------|---------------|-------|----------------------|-------------|---------|
| DAT_0012420c | `dt`       | float | +0x10                | `m_BombScale1` (+0x10) | MATCHES (offset; name misleading) |
| DAT_00124210 | `inc`      | float | +0x14                | `wave_dt_inc` (+0x14)  | MATCHES |
| DAT_00124214 | `spinc`    | float | +0x18                | `delaySpeedScale` / `m_BombGravity` overlap — port has both at +0x18 | DIVERGES — port has overlapping fields |

Note: the port also defines `m_WaveDtSpInc` as a tail field with no offset, but
the binary stores `Wave_dt/spinc` to **WAVE_INFO+0x18**. Fix: rename
`delaySpeedScale` to `m_WaveDtSpInc`, drop the duplicate tail field.

### `<NextWaveDelay>`

| DAT addr | XML attribute | Type  | Binary target offset | Port target | Verdict |
|----------|---------------|-------|----------------------|-------------|---------|
| DAT_0012421c | `wait`     | float | +0x28                | `m_BombMaxAngle` (+0x28) | DIVERGES — port name is wrong; this is `m_NextWaveWait` |
| DAT_00124220 | `waitSpinc`| float | +0x30                | `m_BombField30` (+0x30) | DIVERGES — port name wrong; this is `m_WaitSpInc` |
| DAT_00124224 | `speedLoss`| float | +0x1c                | `m_BombSpeed` (+0x1c)    | DIVERGES — port name wrong; this is `m_SpeedLoss` |
| (logic) `if (0.0 < +0x28) { +0x24 = 0; +0x20 = 0; }` — clears delay+inc if a wait is set | | | | | |
| DAT_001241dc | `delay`    | float | +0x20 (after clear)  | `m_BombSpeedMax` (+0x20) | DIVERGES — port name wrong; this is `m_NextWaveDelay` |
| DAT_00124210 | `inc`      | float | +0x24                | `m_BombMinAngle` (+0x24) | DIVERGES — port name wrong; this is `m_NextWaveDelayInc` |
| DAT_00124228 | `waitForEntities` | bool | +0x38 (1 if attr absent OR ≠ "false"; 0 if "false") | `m_bAllowBombs` (+0x38) | DIVERGES — port name wrong; this is `m_bWaitForEntities` |
| DAT_00124230 | `waitForProcessing` | bool | +0x39 (CompareWords == 0 ⇒ stored as 1; else 0) | `m_bAllowBombsFrenzy` (+0x39) | DIVERGES — port name wrong; this is `m_bWaitForProcessing` |

There is **no `allowbombs` / `allowbombsfrenzy` attribute** in the shipping XML,
nor is the binary reading any such attribute. The port struct labels at +0x38/+0x39
are wrong.

---

## 5. Attribute table — `<ChooseFrom>`, `<defaults>`, `<OverideProbability>`, `<coin_chances>`

### `<ChooseFrom>` (child of `<WaveInfo>`)

| DAT addr | XML attribute | Type  | Binary target offset | Port target | Verdict |
|----------|---------------|-------|----------------------|-------------|---------|
| DAT_00124234 | (element) | — | clear vector at WAVE_INFO+0x54; field +0x60 = 0 (always) | `m_SpecialFruits` / `m_field60` | MATCHES |
| DAT_00124238 | `types`    | str→vector | SplitWords → +0x54 vector | `m_SpecialFruits` (+0x54) | MATCHES |

The `m_ChooseFrom` field in the port is a duplicate; the binary writes to
+0x54 only and the port already has `m_SpecialFruits` at +0x54. The port also
defines a separate `m_ChooseFrom` vector — these should be merged.

### `<defaults>` (top-level, sets DEFAULT_WAVE_INFO + per-mode WaveManager fields)

Target struct: `DEFAULT_WAVE_INFO*` at WaveManager+0xdc+modeIdx*0x40.

| DAT addr | XML attribute | Type | DEFAULT_WAVE_INFO offset | Port target | Verdict |
|----------|---------------|------|--------------------------|-------------|---------|
| DAT_00124240 | `waveChance`        | int   | +0x0                | `m_DefaultCount` (+0x0) | DIVERGES — name wrong |
| DAT_00124244 | `waveChanceRegrowth`| float | +0x4                | `m_CritChance` (+0x4) | DIVERGES — name wrong; XML key is **`waveChanceRegrowth`** not `waveChanceGrowth` |
| DAT_001241ac | `criticalChance`    | float | +0x8                | `m_WaveDelay` (+0x8) | DIVERGES — name wrong |
| DAT_0012420c | `dt`                | float | +0xc                | `m_SpawnTimeScale` (+0xc) | DIVERGES — name wrong; this is the default Wave_dt `dt` |
| DAT_00124248 | `dtInc`             | float | +0x10               | `m_BombScale` (+0x10) | DIVERGES |
| DAT_0012424c | `dtSpInc`           | float | +0x14               | `m_BombGravity` (+0x14) | DIVERGES |
| DAT_00124250 | `beforeDelay`       | float | +0x18               | `m_BombSpeed` (+0x18) | DIVERGES |
| DAT_00124254 | `beforeDelayInc`    | float | +0x1c               | `m_BombSpeedMax` (+0x1c) | DIVERGES |
| DAT_00124258 | `nextDelay`         | float | +0x20               | `m_BombMin` (+0x20) | DIVERGES |
| DAT_0012425c | `nextDelayInc`      | float | +0x24               | `m_BombMax` (+0x24) | DIVERGES |
| DAT_00124260 | `nextDelaySpInc`    | float | +0x28               | `m_CritChanceMod` (+0x28) | DIVERGES |
| DAT_00124224 | `speedLoss`         | float | +0x2c               | `m_field2c` (+0x2c) | DIVERGES |
| DAT_00124264 | `overideProbabiltyPool` | int | +0x30             | `m_field30` (+0x30) | DIVERGES |

Per-mode WaveManager bytes/words:
| DAT addr | XML attribute | Type | WaveManager target | Verdict |
|----------|---------------|------|--------------------|---------|
| DAT_00124228 | `waitForEntities` | bool | +0x108 (single byte, NOT per-mode array) | DIVERGES — port writes per-mode `m_DtIncPerMode` here |
| DAT_00124230 | `waitForProcessing` | bool | +0x109 | DIVERGES |
| DAT_00124268 | `players` (str compared to "1,2") | str | if equal: +0x114=1, +0x118=2, +0x11c=-1 | MISSED — not parsed in port |
| DAT_00124270 | `globalDtInc`   | float | WaveManager+0x7c+modeIdx*4 (= `m_DtIncPerMode[mode]`) | MATCHES *but XML uses `globalDtInc` not `dtInc`* |
| DAT_00124274 | `globalDtStart` | float | WaveManager+0x8c+modeIdx*4 (= `field_0x8c[mode]`) | MATCHES |
| DAT_00124278 | `globalDtMax`   | float | WaveManager+0x9c+modeIdx*4 (= `field_0x9c[mode]`) | MATCHES |

### `<OverideProbability>` (top-level)

The binary calls `PROBABILITY_OVERIDE::Parse(el)` on each. The exact attrs
parsed by Parse aren't in this Init function. Port reads `types` /
`percentageChance` / `perWave` / `waveCount` / `disableWhenPowered` — these
match what `arcadewavelist.xml` uses. **Note port uses `percentageChance` but
the actual XML attr is `percentageChance` ✓.**

### `<coin_chances>` (top-level — separate from per-WaveInfo `<coin_chances>`)

Binary calls `ParseCoinChanceinator(WaveManager+0x1dc+modeIdx*8, el)`.
Top-level coin chances are not present in any shipping XML; safely skipped.

---

## 6. Struct field-offset audit

### SPAWNER_INFO (size 0x64) — corrected from binary

| Offset | Type   | Binary field         | Port field name (current) | Status |
|--------|--------|----------------------|---------------------------|--------|
| +0x00  | ptr    | m_pFruitTypeHashes   | m_pFruitTypeHashes        | OK |
| +0x04  | vector | m_FruitTypeNames     | m_FruitTypeNames          | OK |
| +0x10  | int    | m_FruitTypeCount     | m_FruitTypeCount          | OK |
| +0x14  | float  | m_TimeScale          | m_TimeScale               | OK (never written by Init; ctor=1.0) |
| +0x18  | float  | m_Gravity.x          | m_Gravity_x               | OK (offset); ctor default WRONG (port=0, binary=0) |
| +0x1c  | float  | m_Gravity.y          | m_Gravity_y               | OK (offset); **ctor default WRONG (port=0, binary=-1)** |
| +0x20  | float  | m_Gravity.z          | m_Gravity_z               | OK (offset); ctor default WRONG (port=0, binary=0) |
| +0x24  | float  | m_VelXScale          | m_MinVel ← misnamed       | port has at +0x2c — **WRONG OFFSET** |
| +0x28  | float  | m_VelYScale          | m_MaxVel ← misnamed       | port has at +0x30 — **WRONG OFFSET** |
| +0x2c  | float  | m_HorizMin           | m_MinAngle ← misnamed     | port has at +0x24 — **WRONG OFFSET** |
| +0x30  | float  | m_HorizMax           | m_MaxAngle ← misnamed     | port has at +0x28 — **WRONG OFFSET** |
| +0x34  | byte   | m_SpawnType          | m_SpawnType               | OK |
| +0x38  | float  | m_SpawnMin           | m_SpawnMin                | OK |
| +0x3c  | float  | (unused; ctor=0)     | m_SpawnMax_unused         | OK (binary ctor sets to 0) |
| +0x40  | float  | m_SpawnMax           | m_SpawnMax                | OK |
| +0x44  | float  | m_GrowthInc (mininc/maxinc — both write here) | m_Speed ← misnamed | **mis-mapped — should be m_MinInc/m_MaxInc shared slot** |
| +0x48  | float  | m_Delay (chuck delay base; XML "delay") | m_field48 | port has m_ZOffset at +0x5c; this should move to +0x48 |
| +0x4c  | float  | m_DelayInc (XML "delayinc") | m_field4c       | port has m_DelayInc as a tail field |
| +0x50  | int    | m_RemainingCount     | m_RemainingCount (+0x54)  | port at +0x54 — **WRONG OFFSET** |
| +0x54  | float  | (unknown — possibly m_SpawnCountF or per-spawner counter) | m_SpawnCountF (+0x58) | port at +0x58 — wrong offset |
| +0x58  | int?   | (unknown)            | (none)                    | not handled |
| +0x5c  | float  | m_SpawnTimer         | m_ZOffset (+0x5c) ← misnamed | port at +0x5c labelled wrong: this is the countdown timer, not chuck delay base |
| +0x60  | byte   | m_bMirror            | m_bForceOnce ← misnamed; m_bMirror is a tail field | port has the byte at +0x60 but uses it for forceonce |

### WAVE_INFO (size 0x78) — corrected from binary

| Offset | Type   | Binary field         | Port field name (current)     | Status |
|--------|--------|----------------------|-------------------------------|--------|
| +0x00  | int    | m_ScoreThreshold (mirror of waveNo) | m_ScoreThreshold     | OK |
| +0x04  | int    | m_EndScore           | m_EndScore                    | OK |
| +0x08  | ptr    | m_pSpawners          | m_pSpawners                   | OK |
| +0x0c  | int    | m_SpawnerCount       | m_SpawnerCount                | OK |
| +0x10  | float  | m_WaveDt (XML Wave_dt/dt)   | m_BombScale1 ← misnamed | OK (offset); rename |
| +0x14  | float  | m_WaveDtInc (XML Wave_dt/inc) | wave_dt_inc            | OK |
| +0x18  | float  | m_WaveDtSpInc (XML Wave_dt/spinc) | delaySpeedScale + m_BombGravity (overlap) | DIVERGES — collapse to single field |
| +0x1c  | float  | m_NextWaveSpeedLoss (XML NextWaveDelay/speedLoss) | m_BombSpeed | DIVERGES |
| +0x20  | float  | m_NextWaveDelay (XML NextWaveDelay/delay) | m_BombSpeedMax | DIVERGES |
| +0x24  | float  | m_NextWaveDelayInc (XML NextWaveDelay/inc) | m_BombMinAngle | DIVERGES |
| +0x28  | float  | m_NextWaveWait (XML NextWaveDelay/wait) | m_BombMaxAngle | DIVERGES |
| +0x2c  | (unknown — likely padding) | ?           | (none)                  | — |
| +0x30  | float  | m_NextWaveWaitSpInc (XML NextWaveDelay/waitSpinc) | m_BombField30 | DIVERGES |
| +0x34  | float  | m_WaveRevisitCount (incremented in GetNextWave) | field_0x34 | OK |
| +0x38  | byte   | m_bWaitForEntities (XML NextWaveDelay/waitForEntities, 1 default) | m_bAllowBombs | DIVERGES |
| +0x39  | byte   | m_bWaitForProcessing | m_bAllowBombsFrenzy           | DIVERGES |
| +0x3c  | int    | m_Chance (XML "chance")             | (none — port has tail field) | DIVERGES |
| +0x40  | (gap)  | ?                    | (gap)                         | — |
| +0x44  | float  | m_ChanceRegrowth (XML "chanceRegrowth") | m_WaveDelay         | DIVERGES |
| +0x48  | (gap)  | ?                    | (gap)                         | — |
| +0x4c  | int    | m_GamesMin (XML "games" / "gamesMin") | m_BombMin           | DIVERGES |
| +0x50  | int    | m_GamesMax (XML "gamesMax") | m_BombMax              | DIVERGES |
| +0x54  | vector | m_ChooseFrom (XML ChooseFrom/types) | m_SpecialFruits      | OK (offset; has duplicate `m_ChooseFrom` tail) |
| +0x60  | int    | (cleared to 0 always) | m_field60                    | OK |
| +0x64  | float  | m_CriticalChance     | m_CriticalChance              | OK |
| +0x68  | int    | m_WaveIndex          | m_WaveIndex                   | OK |
| +0x6c  | ptr    | m_pCoinChance        | m_pCoinChance                 | OK |
| +0x70  | int    | m_OverideProbabilityPool *and* m_WaveNumber (different Init paths write same slot) | m_WaveNumber | DIVERGES — port has `m_OverideProbabilityPool` as separate tail; binary stores both attrs at +0x70 (collision; the second-read attr wins, normally `overideProbabiltyPool`) |
| +0x74  | int    | m_TotalWeight        | m_TotalWeight                 | OK |

Note: at the WaveInfo header, the binary first reads `waveNo` (DAT_00123b84)
into a temporary `local_38`, then reads `overideProbabiltyPool`
(DAT_00123b88) directly into +0x70. The `waveNo` then goes to +0x0
(m_ScoreThreshold) via the conditional `if (-1 < local_38 || mode==2)`.
**The port has waveNo at +0x70 (`m_WaveNumber`) — that may collide with
overideProbabiltyPool also at +0x70.** Need to disambiguate the call sequence;
the simplest fix is to keep `m_WaveNumber` as a separate tail field and store
`m_OverideProbabilityPool` at +0x70.

### DEFAULT_WAVE_INFO (size 0x40) — corrected

| Offset | Binary field           | Port field          | Verdict |
|--------|------------------------|---------------------|---------|
| +0x00  | m_WaveChance (int)     | m_DefaultCount      | DIVERGES (rename) |
| +0x04  | m_WaveChanceRegrowth   | m_CritChance        | DIVERGES (XML key is `waveChanceRegrowth` — port currently parses `waveChanceGrowth` which IS NOT in any shipping XML) |
| +0x08  | m_CritChance           | m_WaveDelay         | DIVERGES |
| +0x0c  | m_DefaultDt            | m_SpawnTimeScale    | DIVERGES |
| +0x10  | m_DtInc                | m_BombScale         | DIVERGES |
| +0x14  | m_DtSpInc              | m_BombGravity       | DIVERGES |
| +0x18  | m_BeforeDelay          | m_BombSpeed         | DIVERGES |
| +0x1c  | m_BeforeDelayInc       | m_BombSpeedMax      | DIVERGES |
| +0x20  | m_NextDelay            | m_BombMin           | DIVERGES |
| +0x24  | m_NextDelayInc         | m_BombMax           | DIVERGES |
| +0x28  | m_NextDelaySpInc       | m_CritChanceMod     | DIVERGES |
| +0x2c  | m_DefSpeedLoss         | m_field2c           | DIVERGES |
| +0x30  | m_OverideProbabilityPool (int) | m_field30   | DIVERGES |

The per-mode `waitForEntities` / `waitForProcessing` / `players` parsing
writes to **WaveManager** (at +0x108 / +0x109 / +0x114..+0x11c), NOT into
DEFAULT_WAVE_INFO. The port currently does not parse `players`.

---

## 7. Cross-check: shipping XML attribute set

### `originalwavelist.xml`
Used: `waveNo`, `until`, `chance`, `chanceRegrowth`, `gamesMin`, `gamesMax`,
`type`, `min`, `max`, `mininc`, `maxinc`, `delay`, `delayinc`, `placement`,
`gravity`, `horizmin`, `horizmax`, `dt`, `inc`, `delay`, `criticalChance`
(via `<defaults>`), `waveChance`, `waveChanceGrowth` (binary parses
`waveChanceRegrowth`, but XML uses `waveChanceGrowth` — typo mismatch; binary
default of 0.33 already in port).

### `combowavelist.xml`
Adds: `<ChooseFrom types="...">`.

### `arcadewavelist.xml`
Adds: `<defaults>` with `dt`, `dtSpInc`, `waitForEntities`, `speedLoss`,
`globalDtStart`, `globalDtInc`, `globalDtMax`, `overideProbabiltyPool`;
`<NextWaveDelay wait="..." waitSpinc="..." waitForEntities="...">`;
`debugName` (NOT read by binary — purely cosmetic XML); `velYscale`;
`<OverideProbability types="..." percentageChance="..." perWave="..."
waveCount="..." disableWhenPowered="...">`.

### `zenwavelist.xml`
Same set as Classic, simpler waves.

### Attributes binary reads but no XML uses
- `games` (legacy alias for `gamesMin`) — defaultable.
- `velscale` (no XML; both `velXscale` / `velYscale` are used instead).
- `mirror` (no XML uses it — defaults to 0).
- Top-level `<coin_chances>` element — none of the four XMLs has it.
- DEFAULT_WAVE_INFO: `waveChanceRegrowth` (XML uses `waveChanceGrowth`),
  `criticalChance`, `beforeDelay`, `beforeDelayInc`, `nextDelay`, `nextDelayInc`,
  `nextDelaySpInc`, `players`.

### XML attributes binary does NOT read
- `debugName` (cosmetic, ignored at parse time — the port also ignores it).
- `<defaults waveChanceGrowth="...">` (binary key is `waveChanceRegrowth`;
  the shipping XML key is `waveChanceGrowth` — neither matches the other,
  so the default `0.33` from ctor stays; no behavioural impact).

---

## 8. Port-side actions (concrete fix list)

### Critical (root cause of upward-watermelon)
- **`src/game/WaveStructs.h:82`** — `SPAWNER_INFO()` ctor: change
  `m_Gravity_y(0.0f)` to `m_Gravity_y(-1.0f)`. Keep x and z at 0.
  Binary @ 0x001270ac initialises Vec3 to `(0, -1, 0)` via
  `_Vector3<float>::_Vector3(&local_24, 0.0, -1.0, 0.0)`.

### High priority (gameplay-affecting offset/name fixes)

- **SPAWNER_INFO horizmin / horizmax / velscale / velXscale / velYscale**
  are at the **wrong offsets**. Port currently has:
  - `m_MinAngle (+0x24)` / `m_MaxAngle (+0x28)` / `m_MinVel (+0x2c)` / `m_MaxVel (+0x30)`.
  
  Binary writes:
  - `velscale → +0x24` (and copies to +0x28); `velXscale → +0x24`; `velYscale → +0x28`.
  - `horizmin → +0x2c`; `horizmax → +0x30`.
  
  Fix: swap the field names so +0x24/+0x28 = `m_VelXScale` / `m_VelYScale`,
  and +0x2c/+0x30 = `m_HorizMin` / `m_HorizMax`. This matches Ghidra's
  inferred `m_MinVel`/`m_MaxVel` at +0x2c/+0x30 — the Ghidra struct itself
  is wrong; the **binary's actual write addresses are the source of truth**.

- **SPAWNER_INFO m_RemainingCount** is at +0x54 in port; binary uses **+0x50**.
  Fix: move to +0x50.

- **SPAWNER_INFO m_Delay** (chuck delay base, XML "delay") is at +0x48 in
  binary, NOT +0x5c. Port stores it at +0x5c (named `m_ZOffset`).
  Binary's +0x5c is `m_SpawnTimer` (countdown). UpdateWave decrements +0x5c
  by `dt * dtMod`, refills with `+0x48 + +0x4c * waveRevisit` after a spawn.
  
  Fix: rename +0x5c to `m_SpawnTimer`, add `m_Delay` (or rename existing
  `m_field48`) at +0x48. SpawnFruit's chuckDelay computation uses **+0x5c**
  (current SpawnTimer value, normally negative just after fire), not the XML
  "delay" attr — the port's `chuckDelay = (zOffset > 0) ? zOffset+0.21 : 0.21`
  is wrong; binary always returns 0.21 in normal play (since SpawnTimer is
  ~0 or slightly negative at fire moment).

- **SPAWNER_INFO mininc + maxinc** both write to **+0x44** in binary
  (single `m_GrowthInc` slot, maxinc wins). Port keeps two tail fields
  (`m_MinInc`, `m_MaxInc`). Fix: collapse to a single `m_GrowthInc` at +0x44,
  drop the duplicate.

- **SPAWNER_INFO mirror** is at +0x60 in binary. Port has `m_bForceOnce` at
  +0x60 (misnamed; binary does NOT have a `forceonce` attribute) and a
  separate `m_bMirror` tail field. Fix: rename +0x60 to `m_bMirror`, drop
  `m_bForceOnce`.

- **WAVE_INFO offsets +0x10..+0x39** are extensively misnamed in the port.
  See §6 for the corrected table. The most consequential renames:
  - +0x18 = `m_WaveDtSpInc` (NOT `delaySpeedScale` / `m_BombGravity`).
  - +0x1c = `m_NextWaveSpeedLoss` (NOT `m_BombSpeed`).
  - +0x20 = `m_NextWaveDelay` (NOT `m_BombSpeedMax`).
  - +0x24 = `m_NextWaveDelayInc` (NOT `m_BombMinAngle`).
  - +0x28 = `m_NextWaveWait` (NOT `m_BombMaxAngle`).
  - +0x30 = `m_NextWaveWaitSpInc` (NOT `m_BombField30`).
  - +0x38 = `m_bWaitForEntities` (NOT `m_bAllowBombs`).
  - +0x39 = `m_bWaitForProcessing` (NOT `m_bAllowBombsFrenzy`).

- **WAVE_INFO +0x3c, +0x44, +0x4c, +0x50** must be renamed:
  - +0x3c = `m_Chance` (NOT separate tail field).
  - +0x44 = `m_ChanceRegrowth` (NOT `m_WaveDelay`).
  - +0x4c = `m_GamesMin` (NOT `m_BombMin`).
  - +0x50 = `m_GamesMax` (NOT `m_BombMax`).

- **`WaveManager.cpp:239-242`** — `<Wave_dt>` parsing is correct in offsets
  (matches binary), but field names in the WAVE_INFO struct don't match.
  No code change in Init; just rename in WaveStructs.h.

- **`WaveManager.cpp:244-257`** — `<NextWaveDelay>` parsing currently uses
  port struct names that map to wrong offsets. After renaming the WAVE_INFO
  struct fields above, the parser code stays the same (still reads the same
  XML attrs) — only the field names change.

### Medium priority

- **`WaveManager.cpp:153-168`** — `<defaults>` parsing uses wrong field
  names but offsets in DEFAULT_WAVE_INFO are correct. Rename DEFAULT_WAVE_INFO
  fields per §6 table. The XML key for waveChanceRegrowth is shipping-XML
  `waveChanceGrowth` — neither key matches the other (binary expects
  `waveChanceRegrowth`); port currently parses `waveChanceGrowth`. Recommend
  parsing both for safety; the default `0.33` covers either way.

- **`WaveManager.cpp:159`** — port reads `globalDtStart` to `field_0x8c` and
  `globalDtMax` to `field_0x9c` — correct match with binary.
  But port reads `dtInc` from `<defaults>` to `def.m_DtInc` — binary key for
  the per-mode WaveManager `+0x7c` slot is `globalDtInc`, NOT `dtInc`. The
  `dtInc` attribute (no `global` prefix) writes to DEFAULT_WAVE_INFO+0x10
  (a different slot). Port needs to add a parse of `globalDtInc` ⇒
  `m_DtIncPerMode[mode]`.

- **Players parsing missed**: when `<defaults players="1,2">` is present,
  the binary sets WaveManager+0x114=1, +0x118=2, +0x11c=-1 (an MP-mode
  marker). Not in any shipping XML — defer.

### Low priority / informational

- The port's `m_WaveDtSpInc`, `m_WaitSpinc`, `m_SpeedLoss`,
  `m_bWaitForEntities`, `m_bWaitForProcessing` tail fields can be removed
  once the proper offsets are restored.
- The `<coin_chances>` top-level element handler is OK to leave stubbed.
- Port struct field `m_OverideProbabilityPool` should map to WAVE_INFO+0x70
  for `<WaveInfo overideProbabiltyPool="...">`. The binary collides this
  with `waveNo` at the same +0x70 — both XML attrs write to +0x70. The XML
  files only ever set ONE of the two on a given `<WaveInfo>`, so this works
  out in practice.

---

## 9. Verdict

**Verdict: DIVERGES** — extensively. Beyond the immediate watermelon-fly-up
bug (gravity ctor default), the SPAWNER_INFO and WAVE_INFO struct field
offsets/names are misaligned with the binary's actual write targets in
`WaveManager::Init`. Most attribute *parsing calls* in the port use correct
keys but write to slots that don't match the binary's slot for that XML key.
This will affect every gameplay system that later reads those slots
(SpawnFruit, SpawnBomb, UpdateWave, GetWavedt, etc).

Single immediate fix (gravity ctor) will restore basic spawning. The full
struct-rename pass is needed for fidelity but is mechanical (no algorithmic
changes — only field renames + offset moves).

---

## 10. ASM-verified comment line

For the implementer to paste above `WaveManager::Init` once the SPAWNER_INFO
ctor default is fixed AND struct offsets are aligned per §6:

```
// ASM-verified: 2026-04-30T00:00 binary @ 0x0012393c..0x00124298 (asm-inspector)
```

(Do NOT paste this comment until ALL DIVERGES items in §8 are addressed —
the verdict above is DIVERGES, not Confirmed.)
