#ifndef FN_FRUIT_SAVE_DATA_H
#define FN_FRUIT_SAVE_DATA_H

// FruitSaveData -- the persistent save-data subsystem.
//
// Full struct is 0x238 (568) bytes at Game+0x4c; layout from
// docs/systems/save-system.md and re-analyst dump of FruitNinja_SaveGame
// @ 0x0012a2fc / FruitNinja_LoadGame @ 0x0012be74. Binary serialises
// via TinyXML to /Home/FruitySave.xml; port writes to <data_dir>/FruitySave.xml.
//
// Coin balance (m_Coins/m_CoinsTotal/m_LevelStartCoins) lives at
// +0x20..+0x28 but is NOT serialised in FruitySave.xml -- those go to
// ItemSave.xml via ItemManager (see ItemManager::SaveItemInfo /
// LoadItemData).
//
// Analysed: 2026-04-23T02:00, REVISED 2026-04-27T22:00

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
struct EntityState {
    float pos[3];   // -- world position (Vec3)
    float vel[3];   // -- world velocity
    float grav[3];  // -- gravity vector
    int   type;     // -- fruit type / bomb variant / -1 for power-up
    float wait;     // -- chuck-delay / spawner wait
    int   layer;    // -- 0=fruit, 1=bomb, 4=powerup (binary: ent.layer)
    bool  hit;      // -- bombs only: m_bMenuBombHit equivalent

    EntityState() : type(0), wait(0.0f), layer(0), hit(false) {
        pos[0] = pos[1] = pos[2] = 0.0f;
        vel[0] = vel[1] = vel[2] = 0.0f;
        grav[0] = grav[1] = grav[2] = 0.0f;
    }
};

// SpawnState -- queued spawner info (one per <spawner> child of <wave>).
struct SpawnState {
    float delay;
    int   toSpawn;

    SpawnState() : delay(0.0f), toSpawn(0) {}
};

// WaveState -- queued wave info (one per <wave> child of <wave_info>).
struct WaveState {
    int waveIdx;
    int index;
    std::list<SpawnState> spawners;

    WaveState() : waveIdx(0), index(0) {}
};

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

    // +0x20..+0x28: coin balance. NOT serialised to FruitySave.xml --
    // these go to ItemSave.xml via ItemManager.
    int      m_Coins;              // +0x20
    int      m_CoinsTotal;         // +0x24
    int      m_LevelStartCoins;    // +0x28

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
    // CORRECTED: previously named m_CameraShakeX/Y/Z. Verified by Resume
    // (writes WaveManager::field_0x4c from +0x100, m_Speed_P0 from +0x104,
    // field_0x60 from +0x108) and SaveWaveInfo (inverse direction).
    // There is no camera-shake snapshot in FruitSaveData.
    float    m_Speed_P0;           // +0x100  (WaveManager::field_0x4c)
    float    m_Speed_P0_alias;     // +0x104  (WaveManager::m_Speed_P0)
    float    m_Speed_P1;           // +0x108  (WaveManager::field_0x60)

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
    // Coin API (used by ItemManager)
    // ------------------------------------------------------------------

    // AddCoins @ 0x0010a3bc -- delta to m_Coins; if delta>0 also adds
    // to m_CoinsTotal. Does NOT touch m_LevelStartCoins.
    void AddCoins(int delta);

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

    // Network tweak download (defunct online service). No-op.
    static void DownloadTweaks();

    // ------------------------------------------------------------------
    // Per-frame tick
    // ------------------------------------------------------------------

    // 0x0012b3dc. Achievement in-progress timer ticks.
    void Update(float dt, HUD* hud);

public:

public:

public:

public:

public:

public:

public:

public:

    // ---- AUTO-STUB MERGE: STUB -- gen_stubs.py ----
    // STUB: FruitSaveData::DownloadedTweakValue -- auto stub from binary missing-symbol set
    void DownloadedTweakValue(char const*, int);
    // STUB: FruitSaveData::PublishUnlockedAchievements -- auto stub from binary missing-symbol set
    void PublishUnlockedAchievements();
    // STUB: FruitSaveData::SetTotal -- auto stub from binary missing-symbol set
    void SetTotal(char const*, int, bool, bool);
    // STUB: FruitSaveData::TotalExists -- auto stub from binary missing-symbol set
    void TotalExists(char const*);
    // STUB: FruitSaveData::TotalExists -- auto stub from binary missing-symbol set
    void TotalExists(unsigned int);
    // ---- end AUTO-STUB MERGE ----
};

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
