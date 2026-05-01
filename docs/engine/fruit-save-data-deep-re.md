# FruitSaveData — deep RE

RE'd from `FruitNinja.exe` (Bada ARM32 ELF, GhidraMCP). Builds on
`docs/engine/scorecontrol-combo-source.md` and the existing port skeleton
in `src/game/FruitSaveData.{h,cpp}`. Confirms / corrects the port.

`FruitSaveData` is the **single 0x238-byte container** that owns: stat
tracking maps (slice totals + session totals), per-mode highscores +
play-day cookies, achievement progress + unlocked sets, the resume
snapshot of an interrupted game (entities + waves + game-over fields),
and a save-format version stamp. It is allocated once during
`InitialiseData`, lives at `Game+0x4c`, persists to `/Home/FruitySave.xml`
on Bada, and is the *only* save artifact for fruit-ninja state — coins
go to a separate `ItemSave.xml` via `ItemManager`.

The combo-count slot (+0x78) and last-slasher slot (+0x74) flagged by
the ScoreControl handover are confirmed.

---

## 1. Struct layout (0x238 bytes)

Verified against:
- `FruitSaveData::FruitSaveData` ctor @ `0x00129e74` (default values).
- `FruitSaveData::~FruitSaveData` @ `0x0010ce90` (container destruction
  order; confirms map/list field offsets).
- `FruitNinja_SaveGame` @ `0x0012a2fc` and `ParseSaveFile` @ `0x0012b5e8`
  (XML write/read: every persisted field, exact attr key).
- `FruitSaveData::AddToTotal` @ `0x0012b21c`, `ClearTotal` @
  `0x0012a1f0`, `GetTotal` @ `0x0012a110`, `PlayedModeToday` @
  `0x0012a248`, `CheckDatesHaveChanged` @ `0x0012a29c`.
- `WaveManager::Resume` @ `0x00124b1c` and `WaveManager::SaveWaveInfo` @
  `0x001247f0` (resume / wave-snapshot fields).
- `SaveCurrentData` @ `0x0016ccc8`, `GameOver` @ `0x00169f94`,
  `GameOverScreen::Update` @ `0x00141db0`, `PauseScreen::Update` @
  `0x001543e0` (write call sites and source values).
- `InitialiseData` @ `0x0010b5b8`, `LoadGame` @ `0x0012be74` (boot path).

Defaults from ctor unless noted. "ms" = "ScopeMod" sub-struct (sizes
match the binary's container ABI). Fields not used by the port are still
listed — they each have a real binary write site or XML attr.

| Offset  | Size  | Type                                | Field                         | Default | Notes (XML attr / source) |
|--------:|------:|-------------------------------------|-------------------------------|---------|---------------------------|
| `+0x00` | `0x18`| `std::map<u32, SliceTotal>`         | `m_Totals`                    | empty   | cumulative slice/event totals (ctor calls map ctor + `clear`); SliceTotal node payload is `(name@+0x14, count@+0x58)`, node size 0x68 |
| `+0x18` | `0x18`| `std::map<u32, SliceTotal>`         | `m_SessionTotals`             | empty   | cleared by `ClearCombo` and at boot; XML attr `u="true"` flags entries from this map |
| `+0x30` | `1`   | `bool`                              | `field_0x30`                  | 0       | reserved (untouched by save/load) |
| `+0x31` | `1`   | `bool`                              | `m_bHasActiveGame`            | 0       | gates `<que>` block; set to 1 by ParseSaveFile when `<que>` parsed |
| `+0x32` | `1`   | `bool`                              | `m_bDojoBGUnlocked`           | 0       | XML attr `rated` (true/false) |
| `+0x33` | `1`   | pad                                 | —                             |         | |
| `+0x34` | `8`   | `std::list<EntityState>`            | `m_EntityStates`              | empty   | per-entity resume snapshot (Fruit/Bomb/PowerUp queued by SaveGame) |
| `+0x3c` | `1`   | `bool`                              | `field_0x3c`                  | 0       | XML attr `p2pCancelled`; **NOT** a 2nd dojo flag (binary only ever uses it as the cancellation cookie) |
| `+0x3d` | `3`   | pad                                 | —                             |         | |
| `+0x40` | `4`   | `int`                               | `m_highscore`                 | 0       | XML attr `highscore` — **all-time global** highscore across every mode |
| `+0x44` | `16`  | `int[4]`                            | `m_ModeHighScores[4]`         | {0}     | XML attrs `<MODE>highscore` (CLASSIC/CASINO/ARCADE/ZEN). `SetCurrentModeHighscore @ 0x0010a388` writes via `(mode+0x10)*4 + 4` formula = `+0x44 + mode*4`. **Only updated** when `2*newScore > currentModeHigh`, i.e. `currentModeHigh/2 < score`. |
| `+0x54` | `16`  | `int[4]`                            | `m_ModeBestCombos[4]`         | {0}     | XML attrs `<MODE>_unposted` (only emitted when value > 0). Holds **per-mode network leaderboard delta** (combo count to publish next time online) — drained by `FruitSaveData::Update` posting to leaderboard, **not** "best combo" |
| `+0x64` | `4`   | `int`                               | `m_CurrentScore`              | 0       | XML attr `count` inside `<que>` |
| `+0x68` | `4`   | `int`                               | `m_CurrentMissCount`          | 0       | XML attr `misses` |
| `+0x6c` | `4`   | `u32`                               | `m_GameMode`                  | 0       | XML attr `mode` (string -> `ParseGameMode` -> 0..3). LoadGame clamps to 0 if > 3 |
| `+0x70` | `1`   | `bool`                              | `m_bWasGameOver`              | 0       | XML attr `hasDropped` |
| `+0x71` | `3`   | pad                                 | —                             |         | |
| `+0x74` | `4`   | `int`                               | `m_LastSlasher`               | -1      | XML attr `count2`. **Confirmed**: `WaveManager::Resume @ 0x00124b54` writes `*GOT[lastSlasher] = save[+0x74]`. `SaveCurrentData @ 0x0016cd08` writes `save[+0x74] = *GOT[lastSlasher]`. (Was `field80_0x74`.) |
| `+0x78` | `4`   | `int`                               | `m_ComboCount`                | 0       | XML attr `count1`. **Confirmed**: `WaveManager::Resume @ 0x00124b68` writes `*GOT[comboCount] = save[+0x78]`. `SaveCurrentData @ 0x0016cd34` writes `save[+0x78] = *GOT[comboCount]`. (Was `field81_0x78`.) |
| `+0x7c` | `4`   | `int`                               | `m_FruitQueueCount`           | 0       | XML attr `fruitQueue` — `n,a,b,c,...` parses count + entries. `Resume @ 0x00124cf4` writes this to `WaveManager::field_0x2c8` (the wave-queue head). (Note: existing port doc lists this as `field82_0x7c` and treats `+0x80` as the `[32]` queue; layout is consistent — count then array.) |
| `+0x80` | `0x80`| `int[32]`                           | `m_FruitQueue[32]`            | all -1  | resume queue. `SaveWaveInfo @ 0x001249?? ` mirrors from `WaveManager::m_ProbabilityOverride+6`. |
| `+0x100`| `4`   | `float`                             | `m_Speed_Pre`                 | 0.0     | XML attr emitted as a comma-triple `(+0x100,+0x104,+0x108)`. Resume writes `WaveManager::field_0x4c` |
| `+0x104`| `4`   | `float`                             | `m_Speed_P0`                  | 0.0     | Resume writes `WaveManager::m_Speed_P0` and `m_Speed_P1` from this; `SaveWaveInfo` saves `m_Speed_P1` here |
| `+0x108`| `4`   | `float`                             | `m_Speed_P1_alias`            | 0.0     | Resume writes `WaveManager::field_0x60` from this. SaveGame only writes the triple when `+0x104 > 0`. |
| `+0x10c`| `4`   | `float`                             | `m_GameTimer1`                | -1.0    | XML: emitted as a SetDouble (raw timer) |
| `+0x110`| `4`   | `int`                               | `m_CriticalChance`            | 70      | XML attr `critical_chance`; default 0x46 = 70 |
| `+0x114`| `4`   | `int`                               | `m_GameOverScreenState`       | -1      | XML attr `go_state` |
| `+0x118`| `4`   | `float`                             | `m_GameOverTimer`             | -1.0    | XML SetDouble `go_time` |
| `+0x11c`| `4`   | `int`                               | `m_GameOverField1`            | -1      | XML attr `go_head` (binary keeps fields swapped from naming) |
| `+0x120`| `4`   | `int`                               | `m_GameOverField2`            | -1      | XML attr `go_body` |
| `+0x124`| `4`   | `int`                               | `m_GameOverField3`            | -1      | XML attr `go_fruit` |
| `+0x128`| `4`   | `int`                               | `m_GameOverField4`            | -1      | XML attr `go_fact` |
| `+0x12c`| `1`   | `bool`                              | `m_bResumeFlag1`              | 0       | XML attr `go_showHighScore` |
| `+0x12d`| `1`   | `bool`                              | `m_bResumeFlag2`              | 0       | XML attr `go_setScore` |
| `+0x12e`| `2`   | pad                                 | —                             |         | |
| `+0x130`| `4`   | `float`                             | `m_BombHitTimer`              | 0.0     | XML SetDouble `go_bombHitTime` (only saved when game-mode tests pass; see SaveCurrentData) |
| `+0x134`| `4`   | `float`                             | `m_NextComboBonus`            | -1.0    | XML SetDouble `nextComboBonus` |
| `+0x138`| `4`   | `float`                             | `m_ShakeIntensity`            | 0.0     | XML SetDouble `shake_time` (note SaveGame writes 0x138 twice; appears intentional) |
| `+0x13c`| `4`   | `float`                             | `m_ShakeDecay`                | 1.0     | XML SetDouble `shake_max_time` |
| `+0x140`| `4`   | `int`                               | `m_pCurrentWave_P1` (raw int) | 0       | XML attr `waveCount` inside `<wave_info>`. Stored as raw int; Resume restores it as a `WAVE_INFO*` (binary stores a pointer-as-int; on load it is a *wave-index recovered by the wave_info parser*). Both Resume and SaveWaveInfo use this slot. |
| `+0x144`| `4`   | `float`                             | `m_WaveDelay`                 | 0.0     | XML SetDouble `waveDelay` |
| `+0x148`| `4`   | `float`                             | `m_WaveWait`                  | 0.0     | XML SetDouble `waveWait` |
| `+0x14c`| `4`   | `float`                             | `m_ProbabilityOverideFlag`    | 1.0     | XML SetDouble `globalWaveDt` (probability override timer; Resume -> `WaveManager::field_0x74`) |
| `+0x150`| `8`   | `std::list<WaveState>`              | `m_WaveStates`                | empty   | per-active-wave snapshot |
| `+0x158`| `0x18`| `std::map<u32, AchievementItem>`    | `m_Achievements`              | empty   | unlocked-set; XML container `<unlocked>` with `<achievement name=…/>` children. AchievementItem: `name@+0x14` (32 chars), `progress@+0x94` (float). Node size 0x98. |
| `+0x170`| `0x18`| `std::map<u32, AchievementItem>`    | `m_AchievementProgress`       | empty   | in-progress; XML container `<achievement>` with `<achievement name= progress=/>` children. `Update @ 0x0012b3dc` migrates entries here -> m_Achievements when `progress <= 0`. |
| `+0x188`| `4`   | `int`                               | `m_blitzSpawnedThisGame`      | 0       | XML attr `blitzSpawnedThisGame` |
| `+0x18c`| `4`   | `int`                               | `m_blitzForceSpawnedCounter`  | 0       | XML attr `blitzForceSpawnedCounter` |
| `+0x190`| `4`   | `float`                             | `m_blitzSpawnTime`            | 0.0     | XML SetDouble `blitzSpawnTime` |
| `+0x194`| `0x60`| `std::map<int,int>[4]`              | `m_ModeScoreHistory[4]`       | empty   | one map per mode; key=score, value=waveIdx. XML containers `<wave_counts_<MODE>>` with `<game_count score= waveIdx=/>` children. Each std::map = 0x18 bytes; 4 of them = 0x60. LoadGame clears all four when version mismatch. |
| `+0x1f4`| `4`   | `int`                               | `m_VersionInfo`               | 0       | XML version stamp via `ParseVersionInfo`. Mismatch with `GetVersionTotal()` triggers ClearTotal + clears all four mode-history maps + `field_0x32 = 0`. |
| `+0x1f8`| `16`  | `int[4]`                            | `m_LastPlayedDay[4]`          | {0}     | XML attrs `<MODE>_dolg`. **NOT a play-count.** Holds the value of `GetDaysSince1900()` from the most recent `GameOver` for that mode (write site `0x00169fec`). Used by `PlayedModeToday` and `CheckDatesHaveChanged` to gate per-day-cap stat counters (e.g. `<MODE>_today` totals). |
| `+0x208`| `4`   | `int`                               | `m_BombQueueCount`            | 0       | XML attr `bombQueue` (csv triple, count + entries) |
| `+0x20c`| `0x2c`| `int[11]`                           | `m_BombQueue[11]`             | all -1  | resume queue (matches ctor's loop-to-`0x2c`) |

Total used: `0x208 + 0x4 + 0x2c = 0x238`. ✔ matches `operator_new(0x238)` in `InitialiseData @ 0x0010b720`.

### Embedded sub-struct layouts

**SliceTotal** (used in `m_Totals` / `m_SessionTotals` map values; 0x48
node payload + 0x20 std::map node header = 0x68 total per node):

| Offset | Size | Type    | Notes |
|-------:|-----:|---------|-------|
| `+0x00`| `0x10`| u32     | hash key (also map key) |
| `+0x14`| `0x40`| `char[64]` | name, copied via `strcpy` into the local buffer in AddToTotal |
| `+0x54`| `0x04`| pad     | |
| `+0x58`| `0x04`| `int`   | count |

`memcpy(__dest, acStack_70, 0x48)` in AddToTotal proves payload size is
0x48 bytes (sub-struct, excluding map node header).

**AchievementItem** (used in `m_Achievements` /
`m_AchievementProgress`; node payload 0x84 bytes):

| Offset | Size | Type    | Notes |
|-------:|-----:|---------|-------|
| `+0x00`| `0x10`| u32     | hash key (map key) |
| `+0x14`| `0x40`| `char[64]` | name |
| `+0x94`| `0x04`| `float` | progress (0.0..1.0); Update ticks down by dt |

`memcpy(..., 0x84)` in `Update` confirms payload size.

**EntityState** (used in `m_EntityStates`; passed to `push_back`):

The `ParseSaveFile` "actor" branch builds it on stack at `&local_84`,
filling the `<actor>` XML in this layout:

| Offset | Size | Type        | Notes |
|-------:|-----:|-------------|-------|
| `+0x00`| 12   | `Vector3<f>`| velocity (XML attr `vel`) |
| `+0x0c`| 12   | `Vector3<f>`| ? (third vector — see ParseVector calls; appears to be position pre-write) |
| `+0x18`| 12   | `Vector3<f>`| ? |
| `+0x24`| 12   | `Vector3<f>`| extra (XML attr — ParseVector at +0x10/+0x10 etc.) |
| `+0x30`| 4    | `int`       | type / fruitTypeIdx (XML attr `count`) |
| `+0x34`| 4    | `float`     | wait/chuck delay |
| `+0x?c`| 1    | `bool`      | hasMenuBombHit |

Note: the binary's exact Sprite-of-3-Vec3s field interpretation is
visible in `WaveManager::Resume`. The Resume path reads:
- `+0x14..+0x1c`: `pos_x/y/z`
- `+0x08..+0x10`: `vel_x/y/z`
- `+0x20..+0x28`: gravity (fruit) **or** rotation/player/scale (bomb)
- `+0x2c`: bomb's `m_bMenuBombHit` carry
- `+0x30`: `type` (`< 0` -> layer 4 power-up; `< g_BombThreshold` ->
  fruit; else bomb)
- `+0x34`: `wait` (drives Chuck on revive)

So the canonical EntityState layout is:

```
struct EntityState {     // size 0x38
  /*+0x00*/ ?? unused4  // ParseSaveFile fills 4 padding ints from XML "vel" parse?
  /*+0x08*/ float vel[3];
  /*+0x14*/ float pos[3];
  /*+0x20*/ float grav_or_rotPlayer[3]; // fruit: gravity; bomb: rotZ, playerIdx, timeScale
  /*+0x2c*/ char  bombMenuHit;
  /*+0x30*/ int   type;          // -1 powerup; <bombThresh fruit; else bomb
  /*+0x34*/ float wait;
};
```

Existing port's `EntityState` matches semantically; only field order
differs from the on-the-wire XML order. (Port doesn't yet serialise
entity states — see Tier-3 below.)

**WaveState** (used in `m_WaveStates`):

```
struct WaveState {       // size 0x18  (ctor + spawner list)
  /*+0x10*/ float waveT;     // SaveWaveInfo writes WAVE_INFO+0x34
  /*+0x14*/ int   waveIdx;   // index into m_WaveInfo[mode]
  /*+0x?? */ std::list<SpawnState> spawners;
};

struct SpawnState {      // size 0x10 (fits in list node)
  /*+0x08*/ int   toSpawn;
  /*+0x0c*/ float delay;
};
```

Existing port struct names match.

---

## 2. Hash-keyed stats map

The container at `+0x00` is `std::map<unsigned long, SliceTotal,
std::less<unsigned long>>` (24 bytes per std::map header on this libstdc++
ABI). Same again at `+0x18` for the session map.

### Operations

| Function | Address | Behaviour |
|---|---|---|
| `AddToTotal(name, hash, count, useSession, fireAchievement)` | `0x0012b21c` | `m = useSession ? &m_SessionTotals : &m_Totals;` find by hash; insert (name copied via `strcpy` into 0x40-byte buffer; payload memcpy'd 0x48 bytes) or `entry.count += count`. Returns the new count. If `fireAchievement`, calls `AchievementManager::UnlockSpecificFruitAchievement` with the new count. |
| `GetTotal(hash)` | `0x0012a110` | Map find; returns `entry.count` or 0. **Reads `m_Totals` only** (not session). |
| `GetTotal(name)` | `0x0012a0d4` (overload) | Hashes name, calls the u32 form. |
| `TotalExists(hash)` / `TotalExists(name)` | `0x0012a0fc` / `0x00129bb4` | Map find; returns bool. |
| `ClearTotal(hash)` | `0x0012a1f0` | Erases from **both** maps (cumulative AND session). Used at boot for the sound/music cookies. |
| `ClearTotals()` | `0x0012afb4` | Clears `m_Totals` (only); also publishes total-fruit leaderboard if online. Used at FinishedGame. |
| `ClearCombo()` | `0x00129b94` | Clears `m_SessionTotals`. |
| `SetTotal(name, count, ...)` | `0x0012b1??` (called from GameOver) | Force-sets count rather than incrementing; signature: `(this, name, count, sessTrack, fireAch)` |
| `FinishedGame()` | `0x0012a034` | Walks all four `m_ModeScoreHistory[mode]` maps; for each entry `if (val >= 0) val--`. (Decays "wave_counts" survivors by 1 each round.) |
| `Update(dt, hud)` | `0x0012b3dc` | Per-frame: ticks `m_AchievementProgress` entries' `progress -= dt`, when `progress <= 0` migrates entry to `m_Achievements` via memcpy(0x84) + erase. Also drains `m_ModeBestCombos[]` to network leaderboard when online. |

### Built-in keys observed

| Key string                      | Lookup site                               | Purpose |
|---|---|---|
| `"sound_off"` / `"music_off"`   | `InitialiseData @ 0x0010b720`             | sound/music mute cookies (zeroed on every boot) |
| `"<MODE>_today"`                | `GameOver @ 0x00169f94`, `PlayedModeToday`| per-day cap on fruit slice tracking |
| `"crits_total"`, `"%s_total"`, `"%s_point_total"`, `"strawberry_combo_total"` | `ParseSaveFile @ 0x0012b5e8` (specific-fruit list) | known per-fruit total tags that fire `UnlockSpecificFruitAchievement` |
| `"blitz_bonus"`                 | `WaveManager::ResetSpeed/AddSpeed`        | blitz banking; ClearTotal'd on speed reset |

The full key universe is keyed by `StringHash(name)` (the binary's
djb2-style 32-bit hash; existing `engine/util/StringHash.h` matches).
`AddToTotal` always copies the original string into the SliceTotal node
so the XML round-trip preserves the human-readable name even when the
hash is the only lookup key.

---

## 3. Disk persistence

### File path

`/Home/FruitySave.xml` (string at `0x001baaaa`, length 21 incl. NUL).

```c
char* GetLoadFileFullPath()  { return "/Home/FruitySave.xml"; }   // @ 0x00129b20
```

The Bada `/Home` mount is the per-app private store; equivalent to
`<data_dir>` on the port (existing port already constructs
`g->data_dir + "/FruitySave.xml"`).

### Format

Plain TinyXML (TiXmlDocument), human-readable XML. Schema:

```xml
<save_file version="<GetVersionString>"
           highscore="N"
           CLASSIChighscore="N" CLASSIC_unposted="N" CLASSIC_dolg="DAYS"
           CASINOhighscore="N"  CASINO_unposted="N"  CASINO_dolg="DAYS"
           ARCADEhighscore="N"  ARCADE_unposted="N"  ARCADE_dolg="DAYS"
           ZENhighscore="N"     ZEN_unposted="N"     ZEN_dolg="DAYS"
           critical_chance="N"
           rated="true|false"
           p2pCancelled="true|false"
           field_0x18c="N">      <!-- via Game+0x18c, undocumented -->

  <SliceTotal name="..." count="N"/>          <!-- m_Totals entry (cumulative)  -->
  <SliceTotal name="..." count="N" u="true"/> <!-- m_SessionTotals entry         -->
  ...

  <achievement>                                <!-- container for in-progress    -->
    <achievement name="..." progress="0.5"/>
    ...
  </achievement>

  <unlocked>                                   <!-- container for unlocked       -->
    <achievement name="..."/>
    ...
  </unlocked>

  <que mode="CLASSIC" hasDropped="false" count="..." misses="..."
       count1="..." count2="..." timer="..." globalWaveDt="..."
       go_state="..." go_time="..." go_bombHitTime="..."
       go_body="..." go_head="..." go_fruit="..." go_fact="..."
       go_showHighScore="..." go_setScore="..."
       nextComboBonus="..." shake_time="..." shake_max_time="..."
       fruitQueue="N,a,b,..." bombQueue="N,a,b,..."
       speedTrip="..." >
    <wave_info waveCount="N" waveDelay="F" waveWait="F"
               blitzSpawnedThisGame="N" blitzForceSpawnedCounter="N"
               blitzSpawnTime="F">
      <wave waveT="F" waveIdx="N">
        <spawner toSpawn="N" delay="F"/>
        ...
      </wave>
      ...
    </wave_info>
    <actor pos="..." vel="..." grav="..." count="N" wait="F" head="false"/>   <!-- fruit -->
    <bomb pos="..." vel="..." rot="..." count="N" head="true|false" wait="F"/>
    <powerup pos="..." wait="F" count="-1"/>
  </que>

  <wave_counts_CLASSIC>
    <game_count score="N" waveIdx="N"/>
    ...
  </wave_counts_CLASSIC>
  <wave_counts_CASINO>...</wave_counts_CASINO>
  <wave_counts_ARCADE>...</wave_counts_ARCADE>
  <wave_counts_ZEN>...</wave_counts_ZEN>

  <powerups>...</powerups>     <!-- via PowerUpManager::SaveActivePowerUps -->
</save_file>
```

### Magic / version

There is **no magic prefix** — TinyXML emits its own `<?xml ...?>`
declaration. Versioning is via the root `version` attribute (string from
`GetVersionString()`) and an integer `m_VersionInfo` (+0x1f4) parsed from
that attribute by `ParseVersionInfo @ 0x?????`. On mismatch with
`GetVersionTotal()`, LoadGame:

1. Calls `ClearTotal(hash("crits_total"))`.
2. Clears all four `m_ModeScoreHistory[*]` maps.
3. Resets `field_0x32 = 0` (loses the dojo unlock).

It does **not** wipe the high-scores or unlocked achievements. (RE'd
from `LoadGame @ 0x0012be74`.)

### IsSaving guard

`SaveCurrentData` toggles a global bool via `GetIsSavingBool()` so
re-entrant save calls (e.g. `GameTaskSaveOnExit` then `GameExit`) skip.
The bool is at the address returned by `GetIsSavingBool` (a singleton
holder). Both exit paths check it before re-saving:

```c
if (*GetIsSavingBool() == 0) { HUD::Save(); SaveCurrentData(true); }
```

---

## 4. Engine call sites

### Save sites (`SaveCurrentData @ 0x0016ccc8`)

`SaveCurrentData(bool fullSave)` is the single fan-in point — it always
calls `ItemManager::SaveItemInfo()` first (writes `ItemSave.xml`),
constructs a stack-local `FruitSaveData` snapshot from the live one,
fills in current score/miss/comboCount/lastSlasher/critical_chance/
gameMode + GameOverScreen state copy, optionally calls
`WaveManager::SaveWaveInfo(&snapshot)` when `fullSave == true`, bumps
`played_total` and `played_with_bomb` via AddToTotal, then calls
`SaveGame(&snapshot)`.

Callers of `SaveCurrentData`:

| Address                    | Function                       | When | fullSave arg |
|---|---|---|---|
| `0x00141eea`               | `GameOverScreen::Update` case 6 (terminal animation hit point) | `field92_0x110 == 10` once per game-over | `false` |
| `0x00154e1a`               | `PauseScreen::Update` case 6 (Quit-to-Menu confirmed)   | user picks Quit on pause menu | `false` |
| `0x00154e26`               | `PauseScreen::Update` case 5 (Retry confirmed)          | user picks Retry on pause menu | `false` |
| `0x0016cf64`               | `GameTaskSaveOnExit`                                    | task-level save-and-exit (Bada lifecycle hook) | `true` |
| `0x0016cf96`               | `GameExit`                                              | hard exit / app teardown                          | `true` |

`SaveCurrentData(false)` skips `WaveManager::SaveWaveInfo` and forces
`m_bHasActiveGame = 0` — used when the active game is *finished* and we
just want to persist totals. `SaveCurrentData(true)` includes the resume
snapshot — used on app-suspend / shutdown so the player can resume.

### Load site

| Address      | Function          | When |
|--------------|-------------------|------|
| `0x0010b714` | `InitialiseData`  | once at engine boot (step 6 of GameInitialise; immediately after `new FruitSaveData(0x238)` at `Game+0x4c`) |

There is **no** runtime reload — `LoadGame` is called exactly once. The
in-memory `FruitSaveData` is the source of truth thereafter; saves
serialise from a stack-local snapshot of it.

### Resume integration

| Address      | Function              | What it pulls from save data |
|--------------|-----------------------|-------------------------------|
| `0x00124b1c..0x00124e90` | `WaveManager::Resume` | `+0x40 score`, `+0x68 miss`, `+0x70 wasGameOver`, `+0x74 lastSlasher`, `+0x78 comboCount`, `+0x80 fruitQueue + count`, `+0x100..+0x108 speed triple`, `+0x114 goState`, `+0x118 goTimer`, `+0x130 bombHitTimer`, `+0x134 nextComboBonus`, `+0x138 shake_time`, `+0x13c shake_max`, `+0x140 waveCount`, `+0x144 waveDelay`, `+0x148 waveWait`, `+0x14c probOverride`, `+0x150 wave-states list`, `+0x188 blitzSpawnedThisGame`, `+0x18c blitzForceSpawned`, `+0x190 blitzSpawnTime`. Resume also drains `m_EntityStates` -> spawning Fruit/Bomb/PowerUp via `ActorManager::Add`. |
| `0x00124cf4` | (inside Resume)        | also restores `WaveManager::field_0x2c8 = save[+0x7c]` |

`Resume` is called via `SkipToPause(true)` from `WaveManager` once
the queued state is non-empty.

### Per-frame tick

`FruitSaveData::Update(dt, hud)` @ `0x0012b3dc` — driven from
`Game::Update` once per frame; ticks achievement-in-progress timers
(decay -> migrate to `m_Achievements`); drains
`m_ModeBestCombos[]` to leaderboard when online; dispatches
`DownloadTweaks()` once a second when online. **Does not auto-save** —
disk saves only happen at the explicit call sites above.

---

## 5. Per-mode highscore tracking

```c
// SetCurrentModeHighscore @ 0x0010a388
int SetCurrentModeHighscore(int newScore) {
    Game* g = Game::GetInstance();
    int mode = g->gameMode;          // byte at Game+0x4
    if (mode >= 4) return 0;
    FruitSaveData* sd = g->pSaveData;  // Game+0x4c
    if (!sd) return 0;
    int* slot = &sd->m_ModeHighScores[mode];  // (mode + 0x10)*4 + 4 inside sd = +0x44 + mode*4
    if (*slot < newScore) { *slot = newScore; return 1; }
    return 0;
}
```

**Caller**: `GameOverScreen::Update` case 6 @ `0x00142080`-ish:

```c
int currentHigh = GetCurrentModeHighscore();
if (currentHigh / 2 < currentScore) {        // i.e. score > prevHigh/2
    SetCurrentModeHighscore(currentScore);   // updates +0x44+mode*4
    // also updates Game+0x4c+offset+300 = sd's "field_0x12c" flag
}
```

So **per-mode highscore is updated when the new score beats half the
previous high**. (This is unusual — likely a tiered "improvement" check
rather than a strict-greater-than. Ghidra is correct; the literal
comparison in the binary is `iVar16 / 2 < iVar6`.)

**Global highscore** at `+0x40` is updated outside this path — only by
`SaveCurrentData` snapshot logic (which does
`if (currentScore > snapshot.m_highscore) snapshot.m_highscore = currentScore;`,
visible via the existing port's behavior). Inspecting the binary's
`SaveCurrentData` shows it does **not** mutate the global highscore. The
global high is updated indirectly: `ParseSaveFile` does
`param_2->field65_0x44 = param_2->highscore;` at load time (reset CLASSIC
mode high to global). So the global high is essentially the **CLASSIC**
mode high. This contradicts the existing port's `SaveCurrentData`
implementation that updates `m_highscore` from currentScore — that
behaviour is **not** in the binary.

⚠ **Port deviation flagged** (do NOT fix in re-analyst pass): the port's
`FruitNinja_SaveCurrentData` mutates `snapshot.m_highscore` from
`g->currentScore` directly; the binary does not. The binary leaves
+0x40 alone in SaveCurrentData and lets ParseSaveFile rebuild it as
the CLASSIC alias on next load.

---

## 6. GamesPlayed counter

There is **no single "games played" counter**. The closest equivalents:

1. **Per-mode last-played-day** at `+0x1f8 + mode*4` — written in
   `GameOver @ 0x00169fec` to `GetDaysSince1900()`. Drives
   `PlayedModeToday()` and `CheckDatesHaveChanged()`. XML attr
   `<MODE>_dolg`.

2. **Per-mode `<MODE>_today` SliceTotal** — incremented in `GameOver`
   when the day-cookie matches today, used to throttle other day-capped
   stats. Hash-keyed entry in `m_Totals`.

3. **Played-with-bomb / played-with-mod cookies** — `SaveCurrentData @
   0x0016cebc` does `if (Game+0x44 == 0) AddToTotal("played_with_bomb",1)`
   and similar for `Game+0x45`. These are the de-facto "session played
   without sound/music" counters.

4. **Global cumulative** — there is a generic `"games_played"` style
   key the `Achievement::UnlockTotalFruitAchievement` consults indirectly
   via `m_Totals`, but the binary's `GameOverScreen::Update` case 6 does
   *not* directly increment a "games played" total; the count is
   implied by the size of `m_ModeScoreHistory[mode]` (which gains one
   entry per game per mode).

So the **score-history map size** (per mode) is the binary's "games
played per mode" proxy. The XML re-emit count comes from
`map<int,int>::size()` on each `m_ModeScoreHistory[mode]`.

---

## Tier-1 — Stats tracking only (in-memory, no disk)

The port's `FruitSaveData` already has the maps and `AddToTotal` skeleton.
What's missing for Tier-1 minimal correctness:

1. **Wire the in-memory totals to the gameplay paths** that the binary
   uses but the port currently stubs:
   - `GameOver @ 0x00169f94`: bump `<MODE>_today` and write
     `m_LastPlayedDay[mode] = GetDaysSince1900()`. (No date helper in
     port yet — wrap `time(nullptr) / 86400 - DAYS_FROM_1900_TO_EPOCH`.)
   - `WaveManager::Resume`: (currently doesn't exist in port) — restore
     `m_LastSlasher` / `m_ComboCount` from save into the new globals
     `g_LastSlasher` / `g_ComboCount` (per
     `docs/engine/scorecontrol-combo-source.md`).
   - `SaveCurrentData`: write `save[+0x74] = g_LastSlasher` and
     `save[+0x78] = g_ComboCount`. The port currently has empty fields
     `m_HighScoreRef1` / `m_HighScoreRef2` for these — **rename to
     `m_LastSlasher` / `m_ComboCount`** and remove the misleading "ref"
     name.

2. **Implement `FruitSaveData::FinishedGame`** (currently empty stub):

   ```cpp
   for (int mode = 0; mode < 4; mode++) {
       for (auto& kv : m_ModeScoreHistory[mode]) {
           if (kv.second >= 0) kv.second--;   // decay survivors by 1
       }
   }
   ```

3. **Implement `FruitSaveData::UnlockTotals`** — port the binary's
   threshold check. Stubbed for now until AchievementManager is full.

4. **Plumb `m_ModeBestCombos`** — increment on combo break in MP path
   (binary writes via combo system); harmless to leave 0 for SP.

5. **Rename `field82_0x7c` -> `m_FruitQueueCount`** and ensure
   `m_FruitQueue[32]` truly starts at `+0x80` (matches binary; current
   port already has this).

6. **Rename `m_ModePlayCounts[4]` -> `m_LastPlayedDay[4]`** and revise
   doc-comment: it's a date stamp, not a play count. Drop the
   "play count" semantic from the existing `FruitSaveData.h` (the XML
   attr name `_dolg` is a Russian transliteration of "долг" = "duty/obl",
   used here as "owe a check" — i.e. "have we already counted today's
   stats?").

## Tier-2 — Disk persistence

The port already has a `FruitNinja_SaveGame` / `FruitNinja_LoadGame`
TinyXML implementation. Verify against this RE:

1. **File path**: `/Home/FruitySave.xml` original; port already maps to
   `<data_dir>/FruitySave.xml`. ✔ no change.

2. **Schema differences from this RE**:
   - Port currently reuses XML element name `<actor>` and only writes
     scalar `<que>` attrs; binary writes a richer `<que>` block with
     three actor-list types (`<actor>` / `<bomb>` / `<powerup>`).
     Tier-2 can keep the simpler scalar-only block.
   - Port emits `count1`/`count2` attrs for combo/lastSlasher? No —
     **they are not emitted at all** in the current port `SaveGame`.
     Binary at `0x0012a8?? ` does emit them via `param_1->field81_0x78`
     and `param_1->field80_0x74`. Add `<que count1=… count2=…>` write
     and `que->QueryIntAttribute("count1"/"count2", ...)` read.
   - **`<wave_counts_<MODE>>` blocks** — port already has these. ✔
   - Port writes `play_count` per mode but **the binary writes `_dolg`
     as the date stamp**. Port should write the date, not a count.

3. **Highscore semantics**: do not mutate `m_highscore` in
   `FruitNinja_SaveCurrentData` (port currently does — see flagged
   deviation above). The binary leaves it alone in SaveCurrentData; it
   acts as a CLASSIC-mode alias rebuilt at load.

4. **Save-format version**: port currently uses a fixed
   `k_SaveVersion = 1`. Match binary by emitting
   `version="<GetVersionString>"` (Bada SDK build string) and storing
   the parsed integer total in `m_VersionInfo`. Bumping the integer
   makes existing saves trigger the partial-wipe path in LoadGame.

5. **IsSaving guard**: add a global `GetIsSavingBool` to prevent re-entry
   between `GameTaskSaveOnExit` and `GameExit`.

6. **Pure SaveGame is at `0x0012a2fc`** (not 0x0012a300 etc.); port's
   binding comment matches.

## Tier-3 — Pause/resume snapshot integration

The expensive part. `SaveCurrentData(true)` builds a full resume snapshot
that lets a non-clean exit (Bada `OnSuspend` / OS kill) preserve the
in-progress game so the next `Init` skips the menus and lands back in
the level mid-wave. To match:

1. **Hook port's `Game::OnAppSuspend`** (or SDL `SDL_APP_WILLENTERBACKGROUND`
   / `SDL_APP_TERMINATING`) to call
   `FruitNinja_SaveCurrentData(true)` — same as `GameTaskSaveOnExit`.

2. **Implement `WaveManager::SaveWaveInfo(FruitSaveData* sd)`** —
   serialises:
   - `sd->m_pCurrentWave_P1 = (int)this->m_pCurrentWave_P1;`
   - `sd->m_WaveDelay = this->field_0x234;`
   - `sd->m_WaveWait = this->field_0x238;`
   - `sd->m_blitzSpawnedThisGame = this->field_0x23d;` etc.
   - `sd->m_FruitQueueCount = this->field_0x2c8;` and copy
     `&this->m_ProbabilityOverride[6 .. +0x80]` into
     `&sd->m_FruitQueue[0 .. 32]`.
   - `sd->m_Speed_P0 = this->field_0x4c;` `sd->m_Speed_P1 =
     this->m_Speed_P1;` `sd->m_Speed_P0_alias = this->field_0x60;`
   - For each `WAVE_INFO*` in the active mode's vector that contains
     `m_pCurrentWave_P1` in its range, push a `WaveState` with
     `waveT = wi->[+0x34]`, `waveIdx`, and (only for the *current* wave)
     a `SpawnState` per `SPAWNER_INFO`.

3. **Implement `WaveManager::Resume`** in the port — replay
   `m_EntityStates` via `ActorManager::Add`, restore wave queue, etc.
   This is the bulk of the work; the binary path is at `0x00124b1c`.

4. **Active-game gate**: `m_bHasActiveGame` (+0x31) must be set in
   `SaveCurrentData` when conditions hold (see binary @ `0x0016ce72`:
   `if (gameOverScreen != null) … `, plus blitz timer condition for
   ARCADE). On boot, `LoadGame` parses it via the `<que>` block presence;
   `Game::Init` then dispatches into a "skip-to-pause" branch if
   `m_bHasActiveGame == 1`.

5. **EntityState serialise/parse**: serialise live actors from
   `ActorManager::GetEntityFirst(layer=0/1/4)` into `m_EntityStates`
   right before SaveGame (binary inlines this in `SaveGame`). Existing
   port's `EntityState` struct has the right fields; just need the loop.

6. **GameOverScreen scalar copy**: SaveCurrentData currently copies the
   scalars from the live GameOverScreen into the snapshot — the binary
   does this in the lump at `0x0016ce4c..0x0016ce80`. Port already has
   this lump as commented-out / partial; finish it.

---

## Binary references — quick index

- `FruitSaveData::FruitSaveData` ctor @ `0x00129e74`
- `FruitSaveData::~FruitSaveData` @ `0x0010ce90`
- `FruitSaveData::AddToTotal` @ `0x0012b21c`
- `FruitSaveData::GetTotal(u32)` @ `0x0012a110`
- `FruitSaveData::GetTotal(char*)` @ `0x0012a0d4`
- `FruitSaveData::TotalExists(u32)` @ `0x0012a0fc`
- `FruitSaveData::TotalExists(char*)` @ `0x00129bb4`
- `FruitSaveData::ClearTotal(u32)` @ `0x0012a1f0`
- `FruitSaveData::ClearTotals` @ `0x0012afb4`
- `FruitSaveData::ClearCombo` @ `0x00129b94`
- `FruitSaveData::SaveGameState` @ `0x00129ca8`
- `FruitSaveData::FinishedGame` @ `0x0012a034`
- `FruitSaveData::PlayedModeToday` @ `0x0012a248`
- `FruitSaveData::CheckDatesHaveChanged` @ `0x0012a29c`
- `FruitSaveData::Update` @ `0x0012b3dc`
- `FruitSaveData::IsAchievementUnlocked` @ `0x?` (xref via
  `_ZN13FruitSaveData21IsAchievementUnlockedEm`)
- `SaveGame(FruitSaveData*)` @ `0x0012a2fc`
- `LoadGame(FruitSaveData*)` @ `0x0012be74`
- `ParseSaveFile(TiXmlNode*, FruitSaveData*)` @ `0x0012b5e8`
- `ParseAchievements` (ParseSaveFile child) — see xref from
  ParseSaveFile near the `_ZN13FruitSaveData…` namespace
- `SaveCurrentData(bool)` @ `0x0016ccc8`
- `GameTaskSaveOnExit` @ `0x0016cf48`
- `GameExit` @ `0x0016cf7c`
- `GameOver(int,float,int)` @ `0x00169f94`
- `WaveManager::Resume` @ `0x00124b1c`
- `WaveManager::SaveWaveInfo` @ `0x001247f0`
- `WaveManager::Reset` @ `0x00125be4`
- `SetCurrentModeHighscore(int)` @ `0x0010a388`
- `GetLoadFileFullPath` @ `0x00129b20` ("/Home/FruitySave.xml" @ `0x001baaaa`)
- `InitialiseData` @ `0x0010b5b8` (allocates SaveData @ step 5, calls
  LoadGame @ step 6)
- `GameOverScreen::Update` SaveCurrentData call @ `0x00141eea`
- `PauseScreen::Update` SaveCurrentData calls @ `0x00154e1a` (Quit) and
  `0x00154e26` (Retry)

XML schema strings:
- root `"save_file"` @ `0x?` (DAT lookup in SaveGame)
- per-mode templates: `"%shighscore"` @ `0x001baac7`, `"%s_unposted"` @
  `0x001baad3`, `"%s_dolg"` @ `0x001baadf`, `"%s_today"` (built at
  GameOver via `(GOT + DAT_0016a054)`)
- `"go_showHighScore"` @ `0x001babd7`
