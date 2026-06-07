#ifndef FN_FRUIT_SAVE_DATA_H
#define FN_FRUIT_SAVE_DATA_H

// FruitSaveData -- the persistent save-data subsystem.
//
// Full struct is 0x238 (568) bytes at Game+0x4c; layout from
// re-analyst dump of FruitNinja_SaveGame @ 0x0012a2fc /
// FruitNinja_LoadGame @ 0x0012be74. Binary serialises via TinyXML to
// /Home/FruitySave.xml; port writes to <data_dir>/FruitySave.xml.
//
// Coin balance is NOT a member of FruitSaveData. In the binary the coin
// fields (m_CoinsBalance/m_CoinsTotalEarned/m_CoinsAtGameStart) live at
// game_work +0x20..+0x28 (GameWork struct), not in FruitSaveData. They
// are serialised to ItemSave.xml via ItemManager::SaveItemInfo /
// LoadItemData, not to FruitySave.xml. Access via game_work directly.
//
// Analysed: 2026-04-23T02:00, REVISED 2026-05-30T00:00

#include <cstdint>
#include <map>
#include <list>
#include <string>

class HUD;

// SliceTotal -- per-key counter struct stored inside m_Totals /
// m_SessionTotals maps. Binary layout has the name at +0x14 and the
// count at +0x58 (~0x68 bytes); port collapses to a plain pair.
struct SliceTotal {
    std::string name;   // -- key in plain text (binary stores at +0x14)
    int         count;  // -- count value (binary stores at +0x58)

    SliceTotal() : count(0) {}
    SliceTotal(const std::string& n, int c) : name(n), count(c) {}
};

// AchievementItem -- pending / unlocked achievement entry stored inside
// FruitSaveData::m_PendingUnlocks (+0x158) and m_UnlockedAchievements (+0x170).
// Binary layout @ FruitSaveData::AddToQue (0x0012b38c):
//   +0x00  char  name[128]  -- strcpy'd from AchievementInfo::m_Name
//   +0x80  float timer      -- 3.0f or 0.0f; counts down each Update tick
// sizeof = 0x84 (132 bytes).
struct AchievementItem {
    char  m_Name[128];  // +0x00
    float m_Timer;      // +0x80

    AchievementItem() : m_Timer(0.0f) { m_Name[0] = '\0'; }
};
static_assert(sizeof(AchievementItem) == 0x84, "AchievementItem must be 0x84 bytes");

// EntityState -- queued resume snapshot for a fruit / bomb / power-up.
// Binary serialises one per active actor when m_bHasActiveGame is set.
//
// ASM-confirmed layout from copy-ctor @ 0x0012c594 and Resume @ 0x00124b1c.
// sizeof = 0x30 (48 bytes), no trailing padding beyond the last field.
//
// Field layout:
//   +0x00  Vec3f m_Velocity      -- restore -> actor->vel
//   +0x0C  Vec3f m_Position      -- restore -> actor->pos
//   +0x18  Vec3f m_Overlay       -- per-kind overlay (see below); +0x25..+0x27 = pad
//   +0x24  uint8 m_BombHitFlag   -- bomb: 0 = Chuck, != 0 = SetHit; upper 3 bytes pad
//   +0x28  int32 m_KindIndex     -- kind discriminator AND Init type-index
//   +0x2C  float m_ChuckMag      -- Chuck/Hit magnitude; applied only if > 0.0f
//
// m_Overlay interpretation by actor kind (derived from m_KindIndex):
//   Fruit  (kind == 0): m_Overlay = gravity Vec3 -> actor->m_Gravity
//   Bomb   (kind == 1): x = rotAxis_z, y = playerIdx (raw int in float slot),
//                       z = timeScale -- TODO: 0x00124b1c bomb-overlay fields
//   PowerUp(kind == 4): overlay not consumed
//
// Kind selection from m_KindIndex (g_FruitCount = **DAT_00124f04):
//   idx >= g_FruitCount  -> kind = 1  (Bomb)
//   idx < 0              -> kind = 4  (PowerUp)
//   else                 -> kind = 0  (Fruit)
struct EntityState {
    float    m_Velocity[3];    // +0x00: actor->vel
    float    m_Position[3];    // +0x0C: actor->pos
    float    m_Overlay[3];     // +0x18: per-kind overlay (see above)
    uint8_t  m_BombHitFlag;    // +0x24: 0=Chuck, !=0=SetHit (bomb only)
    // +0x25..+0x27: 3 bytes implicit padding
    int32_t  m_KindIndex;      // +0x28: Init type-index; also selects Fruit/Bomb/PowerUp
    float    m_ChuckMag;       // +0x2C: Chuck/SetHit magnitude; gate: > 0.0f

    EntityState() : m_BombHitFlag(0), m_KindIndex(0), m_ChuckMag(0.0f) {
        m_Velocity[0] = m_Velocity[1] = m_Velocity[2] = 0.0f;
        m_Position[0] = m_Position[1] = m_Position[2] = 0.0f;
        m_Overlay[0]  = m_Overlay[1]  = m_Overlay[2]  = 0.0f;
    }
};
static_assert(sizeof(EntityState) == 0x30, "EntityState size mismatch (binary: 0x30=48)");

// SpawnState -- queued spawner info (one per <spawner> child of <wave>).
// Binary layout (8 bytes): count@+0x00 (float, -> SPAWNER_INFO.m_SpawnCountF/m_RemainingCount),
// timer@+0x04 (float, -> SPAWNER_INFO.m_SpawnTimer). Both are floats in the binary.
struct SpawnState {
    float count;  // +0x00: spawn count (maps to m_SpawnCountF / m_RemainingCount)
    float timer;  // +0x04: spawn timer (maps to m_SpawnTimer)

    SpawnState() : count(0.0f), timer(0.0f) {}
};

// WaveState -- queued wave info (one per <wave> child of <wave_info>).
// Binary layout (16 bytes): spawners@+0x00 (std::list<SpawnState>, 8-byte sentinel),
// waveIdx@+0x08 (int32), index@+0x0c (uint32).
struct WaveState {
    std::list<SpawnState> spawners;  // +0x00 (8 bytes, Sourcery 2010q1 sentinel-only list)
    int      waveIdx;                // +0x08
    uint32_t index;                  // +0x0c

    WaveState() : waveIdx(0), index(0) {}
};
#ifdef __bada__
static_assert(sizeof(WaveState) == 16, "WaveState size mismatch");
#endif

class FruitSaveData {
public:
    // ------------------------------------------------------------------
    // Field layout. Offsets match the binary's 0x238-byte struct so
    // any future memcpy / Game+0x4c accesses stay binary-faithful.
    // ------------------------------------------------------------------

    // +0x00: cumulative slice/event totals across all sessions.
    // Map key is StringHash(name); value is a name+count pair.
    std::map<uint32_t, SliceTotal> m_Totals;

    // +0x18: per-session totals (cleared at session start).
    std::map<uint32_t, SliceTotal> m_SessionTotals;

    // +0x30: undocumented byte (reserved).
    uint8_t  field_0x30;

    // +0x31: non-zero when a game-in-progress save exists. Gates the
    // <que> ActiveGame block in SaveGame.
    uint8_t  m_bHasActiveGame;

    // +0x32: dojo background unlock flag (XML attr "rated").
    uint8_t  m_bDojoBGUnlocked;

    // +0x34: list of saved entity states for resume.
    std::list<EntityState> m_EntityStates;

    // +0x3c: second dojo unlock flag (XML attr "p2pCancelled").
    uint8_t  field_0x3c;

    // +0x40: global all-time high score (XML attr "highscore").
    int      m_highscore;

    // +0x44..+0x53: per-mode high scores (XML attr "%shighscore").
    int      m_ModeHighScores[4];

    // +0x54..+0x63: per-mode best combos (XML attr "%s_unposted",
    // only written when value > 0).
    int      m_ModeBestCombos[4];

    // +0x64..+0x6c: snapshot of current run.
    int      m_CurrentScore;       // +0x64
    int      m_CurrentMissCount;   // +0x68
    uint32_t m_GameMode;           // +0x6c

    // +0x70: was-game-over flag at save time.
    uint8_t  m_bWasGameOver;

    // +0x74: last-slasher player index snapshot (default -1, sentinel = no slash yet).
    // XML attr "count2". Confirmed: WaveManager::Resume @ 0x00124b54 writes
    // g_LastSlasher = save[+0x74]; SaveCurrentData @ 0x0016cd08 writes save[+0x74] = g_LastSlasher.
    int      m_LastSlasher;        // +0x74

    // +0x78: combo count snapshot (default 0).
    // XML attr "count1". Confirmed: WaveManager::Resume @ 0x00124b68 writes
    // g_ComboCount = save[+0x78]; SaveCurrentData @ 0x0016cd34 writes save[+0x78] = g_ComboCount.
    int      m_ComboCount;         // +0x78

    // +0x7c: fruit queue entry count for resume snapshot.
    // XML attr "fruitQueue" (N,a,b,...). Resume @ 0x00124cf4 writes this to
    // WaveManager::field_0x2c8.
    int      m_FruitQueueCount;    // +0x7c

    // +0x80..+0xfc: fruit queue for resume.
    int      m_FruitQueue[32];     // +0x80 (all -1 by default)

    // +0x100..+0x108: per-player base speed snapshot.
    // Resume: m_Speed_P0 -> WaveManager::m_ComboTimer[0]; m_Speed_P0_alias -> m_Speed[0]/[1];
    // m_Speed_P1 (stored as float) -> WaveManager::m_BlitzBonus[1] (int).
    // SaveWaveInfo (inverse): m_Speed[0] -> m_Speed_P0, m_BlitzBonus[1] -> m_Speed_P1.
    float    m_Speed_P0;           // +0x100  (WaveManager::m_ComboTimer[0])
    float    m_Speed_P0_alias;     // +0x104  (WaveManager::m_Speed[0]/[1])
    float    m_Speed_P1;           // +0x108  (WaveManager::m_BlitzBonus[1], stored as float)

    // +0x10C: persists TimeControl::m_TimeRemaining for resume.
    //         -1.0f sentinel = "non-timed mode, no saved time".
    //         Confirmed via FruitSaveData copy-ctor @ 0x0016e2fc.
    float    m_TimeRemainingSave;

    // +0x110: critical hit chance (default 70 / 0x46).
    int      m_CriticalChance;

    // +0x114..+0x12c: GameOverScreen state copy.
    int      m_GameOverScreenState;  // +0x114 (default -1)
    float    m_GameOverTimer;        // +0x118 (default -1.0)
    int      m_GameOverField1;       // +0x11c (default -1)
    int      m_GameOverField2;       // +0x120 (default -1)
    int      m_GameOverField3;       // +0x124 (default -1)
    int      m_GameOverField4;       // +0x128 (default -1)
    uint8_t  newBestThisGame;        // +0x12c: set when score beat previous high this game
    uint8_t  secondaryFlag;          // +0x12d: cleared on commit; full semantics TBD
    // TODO: determine exact semantics of secondaryFlag (+0x12d)

    // +0x130..+0x13c: in-progress timers + camera shake.
    float    m_BombHitTimer;       // +0x130
    float    m_field134;           // +0x134 (default -1.0; XML "nextComboBonus")
    float    m_ShakeIntensity;     // +0x138
    float    m_ShakeDecay;         // +0x13c (default 1.0)

    // +0x140..+0x14c: WaveManager state.
    // CORRECTED: +0x140 was int m_WaveCount. Verified by Resume
    // (this->m_pCurrentWave_P1 = sd->+0x140) and SaveWaveInfo
    // (sd->+0x140 = (int)this->m_pCurrentWave_P1). Stored as raw word (int).
    int      m_pCurrentWave_P1;    // +0x140  raw WAVE_INFO* from binary save
    float    m_WaveDelay;          // +0x144
    float    m_WaveWait;           // +0x148
    // CORRECTED: +0x14c was m_field14c ("globalWaveDt"). Verified by Resume
    // (this->field_0x74 = sd->+0x14c) and SaveWaveInfo (inverse). The XML
    // attr name is "globalWaveDt" but in-code semantic is PROBABILITY_OVERIDE
    // flag word (WaveManager::field_0x74). Resume and SaveWaveInfo both confirm.
    float    m_ProbabilityOverideFlag; // +0x14c  WaveManager::field_0x74

    // +0x150: queued wave states for resume.
    std::list<WaveState> m_WaveStates;

    // +0x158: queued pending unlocks; populated by AddToQue, ticked by Update.
    // ASM-verified: 2026-05-18 binary @ 0x0012b38c (re-analyst)
    std::map<uint32_t, AchievementItem> m_PendingUnlocks;

    // +0x170: fully unlocked achievements; persisted in <achievements> XML block.
    // ASM-verified: 2026-05-18 binary @ 0x0012b3dc (re-analyst)
    std::map<uint32_t, AchievementItem> m_UnlockedAchievements;

    // +0x188..+0x190: blitz mode state.
    int      m_blitzSpawnedThisGame;       // +0x188
    int      m_blitzForceSpawnedCounter;   // +0x18c
    float    m_blitzSpawnTime;             // +0x190

    // +0x194: per-mode score history maps.
    std::map<int, int> m_ModeScoreHistory[4];

    // +0x1f4: save format version (must match GetVersionTotal()).
    int      m_VersionInfo;

    // +0x1f8: date stamp of most-recent GameOver per mode (XML attr "%s_dolg").
    // Value is GetDaysSince1900() at the time of GameOver. NOT a play count.
    // Used by PlayedModeToday / CheckDatesHaveChanged to gate per-day-cap
    // stat counters (e.g. <MODE>_today totals). The XML attr "_dolg" is a
    // Russian transliteration; semantically "last day this mode was played".
    int      m_LastPlayedDay[4];    // +0x1f8

    // +0x208: best combo length (fruit count) ever achieved across all sessions.
    // +0x20c..+0x234: fruit-type sequence for that best combo (11 slots, -1 = unused).
    // Updated by SlashEntity combo-resolve block @ 0x0017df88 when a new high-combo
    // is achieved; read by FruitFactControl to display the "best combo" fact card.
    int      m_BestComboLength;    // +0x208
    int      m_BestComboFruits[11]; // +0x20c

    // ------------------------------------------------------------------
    // Construction
    // ------------------------------------------------------------------

    // 0x00129e74 -- default ctor; zeros fields + sets documented defaults.
    FruitSaveData();

    // 0x0010ce90.
    ~FruitSaveData();

    // ------------------------------------------------------------------
    // Stat tracking
    // ------------------------------------------------------------------

    // 0x0012b21c. Increments an entry in m_Totals (and m_SessionTotals
    // when trackFruit is set). Returns the new total. Hashes name.
    int AddToTotal(const char* name, uint32_t hash, int count,
                   bool trackSession = false, bool sendNetPacket = false);
    int AddToTotal(const char* name, int count);

    // 0x0012a110. Map lookup by hash; 0 if missing.
    int GetTotal(uint32_t hash);

    // Binary @ 0x00153ebc (via PauseScreen::QuitGameCallback) -- wipes entire m_Totals map.
    // Called from PauseScreen::QuitGameCallback, PauseScreen::RetryGameCallback, and
    // GameOverScreen state-0 exit.
    void ClearTotals();

    // Clears a single entry in m_Totals by hash (binary addr TBD; called by ResetSpeed/AddSpeed).
    void ClearTotal(uint32_t hash);

    // 0x00129b94. Clears m_SessionTotals.
    void ClearCombo();

    // 0x0012a034. Decrements modifier counters at end of round.
    void FinishedGame();

    // Copy g_LastSlasher / g_ComboCount globals into m_LastSlasher / m_ComboCount.
    // Called by SaveCurrentData before writing to disk.
    void SnapshotComboState();

    // Copy m_LastSlasher / m_ComboCount back into g_LastSlasher / g_ComboCount.
    // Called by WaveManager::Resume when restoring an interrupted game.
    void RestoreComboState();

    // SetCurrentModeHighscore @ 0x0010a388. Updates m_ModeHighScores[currentMode]
    // when the new score beats the stored value (strict greater-than at write site,
    // but caller in GameOverScreen::Update gates on currentHigh/2 < currentScore).
    // Returns true iff the stored highscore was actually replaced; binary callers
    // (GameOverScreen::Update case 6) capture this as the "newBest" byte.
    bool SetCurrentModeHighscore(int score);

    // ------------------------------------------------------------------
    // Achievements
    // ------------------------------------------------------------------

    // 0x00129c50. Checks whether hash is pending (returns 2) or already
    // unlocked (returns 1), or absent (returns 0).
    // Binary: non-static; checks m_PendingUnlocks then m_UnlockedAchievements.
    int IsAchievementUnlocked(uint32_t hash);

    // 0x00124f10. Unlocks "total-X" achievements when thresholds hit.
    void UnlockTotals();

    // 0x0012b38c. Queues an achievement unlock by name+hash. Skips if
    // IsAchievementUnlocked returns non-zero. Returns 1 on success, 0 if skipped.
    int AddToQue(const char* name, uint32_t hash);

    // ------------------------------------------------------------------
    // Save / Load
    // ------------------------------------------------------------------

    // 0x00129ca8. Resets the active-game snapshot (entity list, resume).
    void SaveGameState();

    // Daily-reset logic. Stub.
    void CheckDatesHaveChanged();

    // 0x0012a248. Returns true iff gameMode was played today (m_LastPlayedDay[gameMode]
    // matches GetDaysSince1900()) AND the per-mode "<MODE>_today" total is > 0.
    bool PlayedModeToday(int gameMode);

    // Network tweak download (defunct online service). No-op.
    static void DownloadTweaks();

    // ------------------------------------------------------------------
    // Per-frame tick
    // ------------------------------------------------------------------

    // 0x0012b3dc. Achievement in-progress timer ticks.
    void Update(float dt, HUD* hud);

    // Defunct: online tweaks -- no-op stub; binary @ 0x0012a080
    void DownloadedTweakValue(char const*, int);
    // Defunct: online achievements -- no-op stub; binary @ 0x0012a194
    void PublishUnlockedAchievements();
    // TODO: 0x0012b2b0 -- SetTotal: hash name, compute delta vs GetTotal, AddToTotal(delta); return old total
    void SetTotal(char const*, int, bool, bool);
    // TODO: 0x0012a0fc -- TotalExists(name): hash name, delegate to TotalExists(hash)
    void TotalExists(char const*);
    // TODO: 0x00129bb4 -- TotalExists(hash): true if hash present in m_Totals or m_SessionTotals
    void TotalExists(unsigned int);
};

#ifdef __bada__
static_assert(sizeof(FruitSaveData) == 568, "FruitSaveData size mismatch (binary: 0x238)");
#endif

// ----------------------------------------------------------------------
// Save/Load free functions (binary calls them as file-scope fns)
// ----------------------------------------------------------------------

// 0x0016ccc8. Snapshot live Game state into pSaveData and write to disk.
// fullSave=true triggers WaveManager::SaveWaveInfo.
void FruitNinja_SaveCurrentData(bool fullSave = true);

// 0x0012a2fc. Serialise a FruitSaveData to FruitySave.xml.
void FruitNinja_SaveGame(FruitSaveData* save);

// 0x0012be74. Load FruitySave.xml into the given FruitSaveData.
// Returns true on success.
bool FruitNinja_LoadGame(FruitSaveData* save);

// 0x0016cf40. Called on app termination.
void FruitNinja_SaveOnExit();

#endif  // FN_FRUIT_SAVE_DATA_H
