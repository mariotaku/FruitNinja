#ifndef FN_FRUIT_SAVE_DATA_H
#define FN_FRUIT_SAVE_DATA_H

// FruitSaveData -- the persistent save-data subsystem.
//
// Full struct is 0x240 (576) bytes; layout from
// re-analyst dump of SaveGame @ 0x001530dc /
// LoadGame @ 0x0015591c (v1.6.1 binary). Serialises via
// TinyXML to /Home/FruitySave.xml; port writes to <data_dir>/FruitySave.xml.
//
// Coin balance is NOT a member of FruitSaveData. In the binary the coin
// fields (m_CoinsBalance/m_CoinsTotalEarned/m_CoinsAtGameStart) live at
// game_work +0x20..+0x28 (GameWork struct), not in FruitSaveData. They
// are serialised to ItemSave.xml via ItemManager::SaveItemInfo /
// LoadItemData, not to FruitySave.xml. Access via game_work directly.
//
// Analysed: 2026-04-23T02:00, REVISED 2026-06-12T00:00 (v1.6.1 layout)

#include <cstdint>
#include <map>
#include <list>
#include <string>
#include "GameMode.h"

class HUD;
class TiXmlNode;
class TiXmlElement;
struct SuperFruitState;

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
// FruitSaveData::m_PendingUnlocks (+0x15c) and m_UnlockedAchievements (+0x174).
// Binary layout @ v1.6.1 FruitSaveData::AddToQue @0x00154918:
//   +0x00  char  name[128]  -- strcpy'd from AchievementInfo::m_Name
//   +0x80  float timer      -- 3.0f (queue non-empty) or 2.9f (queue empty); counts down each Update tick
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
// ASM-confirmed layout from copy-ctor @ 0x0012c594 and Resume @ 0x0012bf58.
// sizeof = 0x38 (56 bytes), no trailing padding beyond the last field.
//
// Field layout:
//   +0x00  Vec3f m_Velocity      -- restore -> actor->vel
//   +0x0C  Vec3f m_Position      -- restore -> actor->pos
//   +0x18  Vec3f m_Overlay       -- per-kind overlay (see below); +0x25..+0x27 = pad
//   +0x24  uint8 m_BombHitFlag   -- bomb: 0 = Chuck, != 0 = SetHit; upper 3 bytes pad
//   +0x28  int32 m_KindIndex     -- kind discriminator AND Init type-index
//   +0x2C  float m_Wait          -- Chuck/Hit magnitude (chuck delay); applied only if > 0.0f (XML attr "wait")
//   +0x30  float m_SliceWait     -- Fruit m_SliceTimer snapshot (XML attr "sliceWait"); SAVE-SIZE
//                                   fidelity only -- Resume never reads it. Default -1.0f.
//   +0x34  SuperFruitState* m_pSuperFruitState -- non-null when the saved fruit carried an
//                                   active super-fruit controller (owned; freed by Resume).
//
// m_Overlay interpretation by actor kind (derived from m_KindIndex):
//   Fruit  (kind == 0): m_Overlay = gravity Vec3 -> actor->m_Gravity
//   Bomb   (kind == 1): x = rotAxis_z, y = playerIdx (raw int in float slot),
//                       z = timeScale -- TODO: v1.6.1 SpawnBomb @0x00124b1c bomb-overlay fields
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
    float    m_Wait;           // +0x2C: Chuck/SetHit magnitude (chuck delay); gate: > 0.0f
    float    m_SliceWait;      // +0x30: Fruit m_SliceTimer snapshot; SAVE-SIZE only (Resume ignores)
    SuperFruitState* m_pSuperFruitState;  // +0x34: owned super-fruit restore state (freed by Resume)

    EntityState()
        : m_BombHitFlag(0), m_KindIndex(0), m_Wait(0.0f),
          m_SliceWait(-1.0f), m_pSuperFruitState(0) {
        m_Velocity[0] = m_Velocity[1] = m_Velocity[2] = 0.0f;
        m_Position[0] = m_Position[1] = m_Position[2] = 0.0f;
        m_Overlay[0]  = m_Overlay[1]  = m_Overlay[2]  = 0.0f;
    }
};
#ifdef __bada__
static_assert(sizeof(EntityState) == 0x38, "EntityState size mismatch (binary: 0x38=56)");
#endif

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
    // Field layout. Offsets match the binary's 0x240-byte struct (v1.6.1)
    // so any future memcpy / Game+0x4c accesses stay binary-faithful.
    // ------------------------------------------------------------------

    // +0x00: cumulative slice/event totals across all sessions.
    // Map key is StringHash(name); value is a name+count pair.
    std::map<uint32_t, SliceTotal> m_Totals;

    // +0x18: per-session totals (cleared at session start).
    std::map<uint32_t, SliceTotal> m_SessionTotals;

    // +0x30: written 0 by ctor; no read site found in binary or port. Reserved.
    uint8_t  m_reserved30;  // purpose unknown

    // +0x31: non-zero when a game-in-progress save exists. Gates the
    // <que> ActiveGame block in SaveGame.
    uint8_t  m_bHasActiveGame;

    // +0x32: rate-app prompt flag (XML attr "rated"). Set once by the rating-
    // dialog gate in GameOverScreen::SetStateWait (v1.6.1 @0x00184e04) so the
    // prompt only ever fires once; cleared with "unrated_games" on the
    // LoadGame version-mismatch reset. (Older RE mislabelled this
    // "DojoBGUnlocked" -- no dojo-background reader exists.)
    uint8_t  m_bRated;

    // +0x34: list of saved entity states for resume.
    std::list<EntityState> m_EntityStates;

    // +0x3c: P2P-cancelled flag, serialised as XML attr "p2pCancelled".
    uint8_t  m_bP2PCancelled;

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

    // +0x74: last combo-streak fruit-type snapshot (default -1, sentinel = no streak yet).
    // XML attr "count2" ("consecutiveType" in the later attr set). Confirmed:
    // WaveManager::Resume @ 0x00124b54 writes g_ComboFruitType = save[+0x74];
    // SaveCurrentData @ 0x0016cd08 writes save[+0x74] = g_ComboFruitType.
    int      m_ComboFruitType;     // +0x74

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
    // Resume: m_Speed_P0 -> WaveManager::m_ComboTimer (+0x50); m_Speed_P0_alias -> m_ComboSpeed/m_TargetComboSpeed;
    // m_Speed_P1 (stored as float) -> WaveManager::m_ColdTimer (+0x64, raw float word move).
    // SaveWaveInfo (inverse): m_ComboSpeed -> m_Speed_P0, m_ColdTimer -> m_Speed_P1.
    float    m_Speed_P0;           // +0x100  (WaveManager::m_ComboTimer +0x50)
    float    m_Speed_P0_alias;     // +0x104  (WaveManager::m_ComboSpeed/m_TargetComboSpeed)
    float    m_Speed_P1;           // +0x108  (WaveManager::m_ColdTimer +0x64, stored as float)

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
    float    m_NextComboBonus;     // +0x134 (default -1.0; XML "nextComboBonus")
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
    // (WaveManager::m_SpeedAccum = sd->+0x14c) and SaveWaveInfo (inverse). The XML
    // attr name is "globalWaveDt" but in-code semantic is PROBABILITY_OVERIDE
    // flag word (WaveManager::m_SpeedAccum +0x78). Resume and SaveWaveInfo both confirm.
    float    m_ProbabilityOverideFlag; // +0x14c  WaveManager::m_SpeedAccum (+0x78)

    // +0x150: wave speed scalar (v1.6.1 NEW). Default 1.0f.
    // Persisted in the <state> block as XML attr "globalWaveDt" (written by SaveGame
    // as a "%f" string; read back by ParseSaveFile @0x00154c8c via QueryFloatAttribute).
    // SaveWaveInfo @ 0x001254b0 writes it from WaveManager+0x78.
    // TODO: re-check WaveManager::Resume inverse mapping when resume is wired.
    float    m_WaveScalar_v161;             // +0x150

    // +0x154: queued wave states for resume.
    std::list<WaveState> m_WaveStates;

    // +0x15c: queued pending unlocks; populated by AddToQue, ticked by Update.
    // ASM-verified: 2026-05-18 v1.6.1 FruitSaveData::AddToQue @ 0x00154918 (re-analyst)
    std::map<uint32_t, AchievementItem> m_PendingUnlocks;

    // +0x174: fully unlocked achievements; persisted in <achievements> XML block.
    // ASM-verified: 2026-05-18 v1.6.1 FruitSaveData::Update @ 0x0015498c (re-analyst)
    std::map<uint32_t, AchievementItem> m_UnlockedAchievements;

    // +0x18c..+0x194: blitz mode state.
    int      m_blitzSpawnedThisGame;       // +0x18c
    int      m_blitzForceSpawnedCounter;   // +0x190
    float    m_blitzSpawnTime;             // +0x194

    // +0x198: per-mode score history maps.
    std::map<int, int> m_ModeScoreHistory[4];

    // +0x1f8: save format version (must match GetVersionTotal()).
    int      m_VersionInfo;

    // +0x1fc: written 0 by ctor (v1.6.1 NEW); NOT serialised; no read site found. Reserved.
    uint8_t  m_reserved1fc;                // +0x1fc  purpose unknown
    // +0x1fd..+0x1ff: 3 bytes implicit padding.

    // +0x200: date stamp of most-recent GameOver per mode (XML attr "%s_dolg").
    // Value is GetDaysSince1900() at the time of GameOver. NOT a play count.
    // Used by PlayedModeToday / CheckDatesHaveChanged to gate per-day-cap
    // stat counters (e.g. <MODE>_today totals). The XML attr "_dolg" is a
    // Russian transliteration; semantically "last day this mode was played".
    int      m_LastPlayedDay[4];    // +0x200

    // +0x210: best combo length (fruit count) ever achieved across all sessions.
    // +0x214..+0x23c: fruit-type sequence for that best combo (11 slots, -1 = unused).
    // Updated by SlashEntity combo-resolve block @ 0x0017df88 when a new high-combo
    // is achieved; read by FruitFactControl to display the "best combo" fact card.
    int      m_BestComboLength;    // +0x210
    int      m_BestComboFruits[11]; // +0x214

    // ------------------------------------------------------------------
    // Construction
    // ------------------------------------------------------------------

    // v1.6.1 FruitSaveData::FruitSaveData() @0x00152874 (C1) / @0x00152ab4 (C2) --
    // default ctor; zeros fields + sets documented defaults.
    FruitSaveData();

    // 0x0010ce90.
    ~FruitSaveData();

    // ------------------------------------------------------------------
    // Stat tracking
    // ------------------------------------------------------------------

    // 0x0012b21c. Increments an entry in m_Totals (and m_SessionTotals
    // when trackSession is set). Returns the new total. Hashes name.
    // achievementGate, when true, additionally calls
    // AchievementManager::UnlockSpecificFruitAchievement(hash, newCount).
    int AddToTotal(const char* name, uint32_t hash, int count,
                   bool trackSession = false, bool achievementGate = false);
    int AddToTotal(const char* name, int count);

    // Binary @ 0x00152e58 -- hashes name, delegates to GetTotal(hash).
    int GetTotal(const char* name);
    // Binary @ 0x00152760. Map lookup by hash; 0 if missing.
    // Checks m_Totals first, then falls back to m_SessionTotals.
    int GetTotal(uint32_t hash);

    // Wipes the entire m_Totals map. Called from v1.6.1
    // PauseScreen::QuitGameCallback @0x001a55e0, PauseScreen::RetryGameCallback
    // @0x001a5800, and the GameOverScreen state-0 exit.
    void ClearTotals();

    // Clears a single entry in m_Totals by hash (binary addr TBD; called by ResetSpeed/AddSpeed).
    void ClearTotal(uint32_t hash);

    // v1.6.1 FruitSaveData::ClearCombo @ 0x001526c0 -- resets best-combo record:
    // m_BestComboLength = 0, m_BestComboFruits[0..10] = -1.
    void ClearCombo();

    // 0x0012a034. Decrements modifier counters at end of round.
    void FinishedGame();

    // Copy g_ComboFruitType / g_ComboCount globals into m_ComboFruitType / m_ComboCount.
    // Called by SaveCurrentData before writing to disk.
    void SnapshotComboState();

    // Copy m_ComboFruitType / m_ComboCount back into g_ComboFruitType / g_ComboCount.
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

    // v1.6.1 FruitSaveData::IsAchievementUnlocked(unsigned long) @0x001527e8.
    // Checks whether hash is pending (returns 2) or already unlocked (returns 1),
    // or absent (returns 0).
    // Binary: non-static; checks m_PendingUnlocks then m_UnlockedAchievements.
    int IsAchievementUnlocked(uint32_t hash);

    // v1.6.1 @0x00154344. Unlocks SPECIFIC-type achievements whose threshold is
    // already met by an accumulated total (m_SessionTotals then m_Totals).
    void UnlockTotals();

    // v1.6.1 @0x00154918. Queues an achievement unlock by name+hash. Skips if
    // IsAchievementUnlocked returns non-zero. Returns 1 on success, 0 if skipped.
    // Stagger timer: 2.9f default (empty queue), 3.0f if a popup is already pending.
    int AddToQue(const char* name, uint32_t hash);

    // ------------------------------------------------------------------
    // Save / Load
    // ------------------------------------------------------------------

    // v1.6.1 @0x0015286c: m_EntityStates.clear() only (dead code; zero callers).
    void SaveGameState();

    // v1.6.1 @0x00153050. Resets the per-mode "<MODE>_days" streak counter
    // when the mode wasn't played today or yesterday. Called from SaveGame.
    void CheckDatesHaveChanged();

    // ASM-spec v1.6.1 FruitSaveData::PlayedModeToday @0x00152fd8: param is the binary's
    // global GAME_MODE enum (not int). Returns true iff gameMode was played today
    // (m_LastPlayedDay[gameMode] matches GetDaysSince1900()) AND the per-mode
    // "<MODE>_today" total is > 0. No bounds check on gameMode in the binary --
    // see FruitSaveData.cpp for detail.
    bool PlayedModeToday(GAME_MODE gameMode);

    // Network tweak download (defunct online service). No-op.
    static void DownloadTweaks();

    // ------------------------------------------------------------------
    // Per-frame tick
    // ------------------------------------------------------------------

    // v1.6.1 FruitSaveData::Update @0x0015498c. Achievement in-progress timer ticks.
    void Update(float dt, HUD* hud);

    // Defunct: online tweaks -- no-op stub; v1.6.1 binary @ 0x0012a080
    void DownloadedTweakValue(char const*, int);
    // Defunct: online achievements -- no-op stub; v1.6.1 binary @ 0x0012a194
    void PublishUnlockedAchievements();
    // Binary @ 0x0012b2b0 -- SetTotal: hash name, compute delta vs GetTotal,
    // AddToTotal(delta); returns the OLD total (uint) prior to the set.
    unsigned int SetTotal(char const* name, int value, bool trackSession, bool achievementGate);
    // v1.6.1 TotalExists(char const*) @0x00152e38 -- hash name, delegate to TotalExists(hash).
    bool TotalExists(char const* name);
    // v1.6.1 TotalExists(unsigned long) @0x001526e8 -- true if hash present in m_Totals or m_SessionTotals.
    bool TotalExists(unsigned long hash);
};

#ifdef __bada__
static_assert(sizeof(FruitSaveData) == 576, "FruitSaveData size mismatch (binary v1.6.1: 0x240)");
#endif

// ----------------------------------------------------------------------
// Save/Load free functions (binary calls them as file-scope fns)
// ----------------------------------------------------------------------

// v1.6.1 @0x001cde20 (_Z15SaveCurrentDatab). Snapshot live Game state into
// pSaveData and write to disk. fullSave=true triggers WaveManager::SaveWaveInfo.
void SaveCurrentData(bool fullSave = true);

// v1.6.1 @0x001530dc (_Z8SaveGameP13FruitSaveData). Serialise a FruitSaveData to FruitySave.xml.
void SaveGame(FruitSaveData* save);

// v1.6.1 @0x0015591c (_Z8LoadGameP13FruitSaveData). Load FruitySave.xml into the given FruitSaveData.
// Returns true on success.
bool LoadGame(FruitSaveData* save);

// v1.6.1 @0x001ca458 (_Z13GetIsSavingBoolv). Returns pointer to the file-scope isSaving flag.
// True while SaveCurrentData is executing; GameTaskSaveOnExit checks to avoid re-entrant saves.
bool* GetIsSavingBool();

// ParseVersionInfo -- v1.6.1 @0x00152f30 (_Z16ParseVersionInfoPKcP13FruitSaveData).
// Parses the save-file version string and writes the packed int to sd->m_VersionInfo.
void ParseVersionInfo(const char* s, FruitSaveData* sd);

// ParseSaveFile -- v1.6.1 @0x00154c8c (_Z13ParseSaveFileP9TiXmlNodeP13FruitSaveData).
// Recursive tag-walker invoked by LoadGame on the <save_file> root element. Dispatches
// each element by tag name into the matching field group; container tags (save_file,
// state) recurse into their children. Uses the binary's exact element/attr names.
// Contract notes:
// - <save_file>: seeds m_ModeHighScores[0] (CLASSIC) from the global "highscore"
//   attr before the per-mode attrs (legacy pre-mode-split migration).
// - <total>: entries with score <= 0 are skipped; the seven lifetime keys
//   (all/games/totalscore/sessions/bomb/coming_soon/unrated_games) are forced
//   into m_SessionTotals so they survive ClearTotals() on quit/retry.
// - <state>: m_GameMode is assigned unconditionally, then the handler early-returns
//   (no state attrs, no child recursion) if the mode is unrecognised (> 3) or
//   m_VersionInfo != GetVersionTotal().
void ParseSaveFile(TiXmlNode* node, FruitSaveData* data);

// ParseWaveInfo -- v1.6.1 @0x00154510 (_Z13ParseWaveInfoP12TiXmlElementP13FruitSaveData).
// Reads the <wave_info> attrs (waveCount/numberOfWavesSpawned/waveDelay/waveWait/blitz*)
// and rebuilds m_WaveStates from <wave>/<spawner> children. Always returns 1.
int ParseWaveInfo(TiXmlElement* elem, FruitSaveData* data);

// FruitCounter -- v1.6.1 @0x00159f10 (_Z12FruitCounterPKciiPv).
// Iteration callback with (name, count, extra, ctx) signature.
// Accumulates count into the file-scope total_fruit global; returns 1.
int FruitCounter(const char* name, int count, int extra, void* ctx);

// GetLoadFileFullPath -- v1.6.1 @0x0015262c (_Z19GetLoadFileFullPathv).
// Returns the relative save-file path used for loading on Bada ("FruitySave.xml").
// DIFFERS: port I/O uses GetSavePath() returning <save_dir>/FruitySave.xml
//   (per-platform save_dir -- see src/platform/SaveDirSDL.h); binary hardcodes relative path.
const char* GetLoadFileFullPath();

// GetSaveFileFullPath -- v1.6.1 @0x00152610 (_Z19GetSaveFileFullPathv).
// Returns the absolute Bada home path used for saving ("/Home/FruitySave.xml").
// DIFFERS: port I/O uses GetSavePath() returning <save_dir>/FruitySave.xml
//   (per-platform save_dir -- see src/platform/SaveDirSDL.h); binary hardcodes Bada home.
const char* GetSaveFileFullPath();

// DlTwVal -- v1.6.1 @0x00152dc4 (_Z7DlTwValPKciiPv) ("Downloaded Tweak Value")
// Server-side configuration callback. Validates key (must start with "FNT") and
// count > 0, then calls FruitSaveData::DownloadedTweakValue(key, val).
// Returns 1 (success indicator for the download callback protocol).
int DlTwVal(const char* key, int val, int count, void* data);

// GetUserFilePath -- v1.6.1 @0x00154494 (_Z15GetUserFilePathPcPKci).
// Resolves a user-data file path into outBuf (max maxLen bytes) and returns outBuf.
// Dead function: no live callers in v1.6.1 binary.
char* GetUserFilePath(char* outBuf, const char* filename, int maxLen);

// GetIsSavingBool -- v1.6.1 @0x001ca458 (_Z14GetIsSavingBoolv).
// Returns pointer to the file-global isSaving flag (see FruitSaveData.cpp).
// Used by GameTaskSaveOnExit to guard against re-entrant saves.
bool* GetIsSavingBool();

#endif  // FN_FRUIT_SAVE_DATA_H
