# Save System

## Overview

Save data is stored as **XML** using TinyXML. The file path is `/Home/FruitySave.xml` (on Bada: `\Halfbrick\FruitNinja\` root). Items are saved separately to `ItemSave.xml`.

## FruitSaveData (size = 0x238 / 568 bytes)

### Struct Layout

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | map\<ulong, SliceTotal\> | m_Totals | All-time per-stat totals (24 bytes, std::map) |
| +0x18 | map\<ulong, SliceTotal\> | m_SessionTotals | Per-session stats |
| +0x30 | byte | field_0x30 | |
| +0x31 | byte | m_bHasActiveGame | Non-zero = game-in-progress save exists |
| +0x32 | byte | m_bDojoBGUnlocked | Dojo background unlock flag |
| +0x34 | list\<EntityState\> | m_EntityStates | Saved fruit/bomb positions for resume |
| +0x3c | byte | field_0x3c | Second dojo-related unlock |
| +0x44 | int[4] | m_ModeHighScores | Per-mode high scores (Classic/Arcade/Zen/+1) |
| +0x54 | int[4] | m_ModeBestCombos | Per-mode best combo count |
| +0x64 | int | m_CurrentScore | Score at time of save |
| +0x68 | int | m_CurrentMissCount | Miss count at time of save |
| +0x6c | uint | m_GameMode | 0=Classic, 1=Arcade, 2=Zen, 3=? |
| +0x70 | byte | m_bWasGameOver | Game-over flag at save time |
| +0x74 | int | m_HighScoreRef1 | Global high score reference |
| +0x78 | int | m_HighScoreRef2 | Global high score reference 2 |
| +0x7c | int | m_FruitQueueCount | Count of items in fruit queue |
| +0x80 | int[32] | m_FruitQueue | 0x80 bytes; fruit IDs for resume, -1 = empty |
| +0x100 | float | m_CameraShakeX | Camera state for resume |
| +0x104 | float | m_CameraShakeY | |
| +0x108 | float | m_CameraShakeZ | |
| +0x10c | float | m_GameTimer1 | Game timer at save |
| +0x110 | int | m_CriticalChance | Critical hit probability (default 0x46 = 70) |
| +0x114 | int | m_GameOverScreenState | -1 = not showing |
| +0x118 | float | m_GameOverTimer | |
| +0x11c | int | m_GameOverField1 | |
| +0x120 | int | m_GameOverField2 | |
| +0x124 | int | m_GameOverField3 | |
| +0x128 | int | m_GameOverField4 | |
| +0x12c | byte | m_bResumeFlag1 | |
| +0x12d | byte | m_bResumeFlag2 | |
| +0x130 | float | m_BombHitTimer | Game.bombTimer for resume |
| +0x134 | float | m_field134 | |
| +0x138 | float | m_ShakeIntensity | Camera shake intensity at save |
| +0x13c | float | m_ShakeDecay | Camera shake decay value |
| +0x140 | int | m_WaveCount | WaveManager state |
| +0x144 | float | m_WaveDelay | |
| +0x148 | float | m_WaveWait | |
| +0x14c | float | m_field14c | |
| +0x150 | list\<WaveState\> | m_WaveStates | Queued wave states for resume |
| +0x158 | map\<ulong, AchievementItem\> | m_Achievements | Unlocked achievements |
| +0x170 | map\<ulong, AchievementItem\> | m_AchievementProgress | In-progress achievements |
| +0x188 | int | m_blitzSpawnedThisGame | |
| +0x18c | int | m_blitzForceSpawnedCounter | |
| +0x190 | float | m_blitzSpawnTime | |
| +0x194 | map\<int,int\>[4] | m_ModeScoreHistory | Per-mode score history maps (4 x 0x18) |
| +0x1f4 | int | m_VersionInfo | Save version; must match GetVersionTotal() |
| +0x1f8 | int[4] | m_ModePlayCounts | Per-mode play counts |
| +0x208 | int | m_BombQueueCount | Count of bomb queue items |
| +0x20c | int[11] | m_BombQueue | Bomb IDs for resume, -1 = empty |

### Constructor Default Values (0x00129e74)

```c
FruitSaveData::FruitSaveData() {
    // Initialize all std::map and std::list containers (empty)
    m_CurrentScore = 0;
    m_CurrentMissCount = 0;
    m_blitzSpawnTime = 0.0f;  // DAT_0012a030
    m_bDojoBGUnlocked = 0;
    m_BombHitTimer = 0.0f;
    m_HighScoreRef1 = -1;
    m_ShakeIntensity = 0.0f;
    m_GameOverScreenState = -1;
    m_GameOverField2 = -1;
    m_GameOverField1 = -1;
    m_GameTimer1 = -1.0f;     // 0xBF800000
    m_GameOverTimer = -1.0f;
    m_field134 = -1.0f;
    m_GameOverField3 = -1;
    m_CriticalChance = 70;    // 0x46
    m_GameOverField4 = -1;
    m_bResumeFlag1 = 0;
    m_bResumeFlag2 = 0;
    m_highscore = 0;
    m_HighScoreRef2 = 0;
    m_GameMode = 0;
    m_blitzSpawnedThisGame = 0;
    m_blitzForceSpawnedCounter = 0;
    m_ShakeDecay = 1.0f;      // 0x3F800000
    m_FruitQueueCount = 0;
    // m_FruitQueue[0..31] = -1 each
    // m_ModeHighScores[0..3] = 0, m_ModeBestCombos[0..3] = 0, m_ModePlayCounts[0..3] = 0
    // m_ModeScoreHistory[0..3] cleared
    m_WaveWait = 0.0f;
    m_WaveDelay = 0.0f;
    m_WaveCount = 0;
    m_field14c = 1.0f;
    m_bWasGameOver = 0;
    m_VersionInfo = 0;
    // m_BombQueueCount = 0; m_BombQueue[0..10] = -1
    m_CameraShakeX = 0.0f; m_CameraShakeY = 0.0f; m_CameraShakeZ = 0.0f;
}
```

---

## Save Flow

### SaveCurrentData (0x0016ccc8)

Called on game exit, pause, or game-over:

1. Check if multiplayer P2P mode (skip save if so)
2. Set `g_isSaving = true`
3. Save item data: `ItemManager::SaveItemInfo()`
4. Copy current `FruitSaveData` from Game object (+0x4c)
5. Populate snapshot:
   - `m_CurrentScore = GetCurrentScore(0)`
   - `m_CurrentMissCount = GetCurrentMissCount(0)`
   - `m_GameMode = Game.gameMode`
   - `m_CriticalChance = Game.scoreThreshold`
   - Conditionally save `m_BombHitTimer` from Game.bombTimer
   - Save game-over screen state if active
   - Save camera shake state from FruitCamera (+0x164, +0x168)
6. Save wave info: `WaveManager::SaveWaveInfo()`
7. Increment play counters via `FruitSaveData::AddToTotal`
8. Call `SaveGame(&snapshot)`
9. Set `g_isSaving = false`

### SaveGame (0x0012a2fc)

Serializes FruitSaveData to XML:

```xml
<FruitNinjaSave version="1.2.3" highscore="12345" ...>
  <!-- Per-mode high scores -->
  highscore_Classic="1000" combo_Classic="5" plays_Classic="10"
  highscore_Arcade="2000" combo_Arcade="8" plays_Arcade="5"
  ...
  criticalChance="70"
  dojoBG="true"/"false"
  dojoBG2="true"/"false"

  <!-- Slice totals (cumulative) -->
  <SliceTotal name="apple_sliced" count="42"/>
  <SliceTotal name="bomb_sliced" count="3"/>
  <!-- Session-only totals -->
  <SliceTotal session="true" name="..." count="..."/>

  <!-- Achievements -->
  <Achievements>
    <Achievement name="first_blood" progress="1.0"/>
  </Achievements>
  <UnlockedAchievements>
    <Achievement name="master_slicer"/>
  </UnlockedAchievements>

  <!-- Active game state (for resume) -->
  <ActiveGame mode="Classic" score="500" misses="2" wasGameOver="false" ...>
    <WaveInfo waveCount="5" waveDelay="1.5" waveWait="0.8" ...>
      <Wave time="..." count="...">
        <Spawn delay="..." type="..."/>
      </Wave>
    </WaveInfo>
    <!-- Entity positions for resume -->
    <Entity pos="1.0,2.0,3.0" vel="..." accel="..." type="0" .../>
    <Entity pos="..." vel="..." accel="..." ... active="true" wait="0.5"/>  <!-- bomb -->
    <PowerUp .../>
  </ActiveGame>

  <!-- Per-mode score history -->
  <ScoreHistory_Classic>
    <Entry score="1000" combo="5"/>
  </ScoreHistory_Classic>
  ...

  <!-- Active power-ups -->
  <PowerUps>
    ...
  </PowerUps>
</FruitNinjaSave>
```

### LoadGame (0x0012be74)

1. Check if save file exists via `File::Exists()`
2. Load XML: `TiXmlDocument::LoadFile(GetLoadFileFullPath())`
3. Clear entity states list
4. Parse recursively: `ParseSaveFile(doc, saveData)`
5. Validate game mode (clamp to 0..3)
6. Version check: if `m_VersionInfo != GetVersionTotal()`, clear certain stats
7. `CheckDatesHaveChanged()` -- daily reset logic

### ParseSaveFile (0x0012b5e8, recursive)

Parses each XML element by tag name:

| Tag | Action |
|-----|--------|
| `FruitNinjaSave` | Parse version, highscore, criticalChance, per-mode stats, unlock flags |
| `SliceTotal` | `FruitSaveData::AddToTotal()` with name + count |
| `Achievement` / `UnlockedAchievement` | `ParseAchievements()` |
| `Entity` | Parse pos/vel/accel vectors, type, active flag; push to `m_EntityStates` list |
| `PowerUp` | `PowerUpManager::LoadActivePowerUps()` |
| `ActiveGame` | Parse mode, score, misses, timers, resume state fields |
| `WaveInfo` | `ParseWaveInfo()` with wave/spawn child elements |
| `ScoreHistory_*` | Per-mode `map<int,int>` score entries |

### File Paths

| Function | Address | Returns |
|----------|---------|---------|
| GetSaveFileFullPath | 0x00129b08 | Static buffer with full save path |
| GetLoadFileFullPath | 0x00129b20 | Static buffer with full load path |
| GetSaveRootDirectory | 0x0019ae64 | `\Halfbrick\FruitNinja\` base dir |

Save file: `/Home/FruitySave.xml` (string at 0x001baaaa)
Item file: `ItemSave.xml` (string at 0x001b9e40)

---

## Key Save Functions

| Function | Address | Purpose |
|----------|---------|---------|
| FruitSaveData::FruitSaveData() | 0x00129e74 | Default constructor; initializes all fields |
| FruitSaveData::FruitSaveData(const&) | 0x0016e2fc | Copy constructor |
| ~FruitSaveData | 0x0010ce90 | Destructor |
| SaveCurrentData | 0x0016ccc8 | Snapshot Game state to FruitSaveData and save |
| SaveGame | 0x0012a2fc | Serialize FruitSaveData to XML file |
| LoadGame | 0x0012be74 | Load and parse XML into FruitSaveData |
| ParseSaveFile | 0x0012b5e8 | Recursive XML parser |
| FruitSaveData::AddToTotal | 0x0012b21c | Add stat value to totals map |
| FruitSaveData::GetTotal | 0x0012a110 | Look up stat by hash |
| FruitSaveData::Update | 0x0012b3dc | Tick achievement timers |
| FruitSaveData::SaveGameState | 0x00129ca8 | Clear entity states list |
| FruitSaveData::CheckDatesHaveChanged | various | Daily reset logic |
| GameTaskSaveOnExit | 0x0016cf40 | Called on app termination |
| WaveManager::SaveWaveInfo | 0x001247f0 | Save wave queue to FruitSaveData |
| PowerUpManager::SaveActivePowerUps | various | Save active power-ups to XML element |

---

## Data Saved

1. **High scores**: Per-mode (Classic, Arcade, Zen, +1) high scores and best combos
2. **Play counts**: Per-mode play counts (m_ModePlayCounts)
3. **Slice totals**: map<ulong, SliceTotal> keyed by StringHash of stat name (e.g. "apple_sliced", "bomb_sliced")
4. **Achievements**: unlocked + in-progress with float progress values
5. **Score history**: Per-mode map<int,int> of individual game scores
6. **Active game state**: For resume -- score, misses, mode, timers, entity positions, wave queue
7. **Dojo unlocks**: Background unlock flags
8. **Critical chance**: Probability value (default 70)
9. **Camera state**: Shake intensity/decay for resume
10. **Power-ups**: Active power-ups saved via PowerUpManager
11. **Version info**: Save version for migration

## Format

- **XML** (TinyXML library)
- Human-readable text format
- All numeric values stored as attributes (int or double)
- Vectors stored as comma-separated strings ("1.0,2.0,3.0")
- Booleans stored as "true"/"false" strings

---

## See Also

- [Data structs](../structs/data.md) -- FruitSaveData layout
- [Game flow functions](../functions/game-flow.md) -- SaveCurrentData call sites
