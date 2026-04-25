#ifndef FN_FRUIT_SAVE_DATA_H
#define FN_FRUIT_SAVE_DATA_H

// FruitSaveData — stubs for the persistent save-data subsystem.
//
// Full struct is 0x238 (568) bytes at Game+0x4c; layout documented in
// docs/systems/save-system.md and docs/structs/wave.md. Binary serialises
// via TinyXML to \Halfbrick\FruitNinja\FruitySave.xml.
//
// All members here are signature-only stubs so scoring, bomb-hit, wave
// progression, achievement, and save/load call sites can fire without
// ifdefs. Every body is a no-op or returns a safe default; each method
// comment cites the binary address for future implementation.
//
// Analysed: 2026-04-23T02:00

#include <cstdint>

class HUD;

class FruitSaveData {
public:
    // --- Fields accessed cross-module. Widen to full struct later.
    // Only the ones callers read/write today are modelled here; each
    // keeps its binary offset so fidelity work can extend in place.

    int   m_CurrentScore;                  // +0x64
    int   m_CurrentMissCount;              // +0x68
    uint32_t m_GameMode;                   // +0x6c

    int   m_ModeHighScores[4];             // +0x44
    int   m_ModeBestCombos[4];             // +0x54
    int   m_ModePlayCounts[4];             // +0x1f8

    int   m_CriticalChance;                // +0x110 (default 70)
    uint8_t m_bDojoBGUnlocked;             // +0x32

    int   m_WaveCount;                     // +0x140
    float m_WaveDelay;                     // +0x144
    float m_WaveWait;                      // +0x148

    int   m_blitzSpawnedThisGame;          // +0x188
    int   m_blitzForceSpawnedCounter;      // +0x18c
    float m_blitzSpawnTime;                // +0x190

    int   m_FruitQueueCount;               // +0x7c
    int   m_FruitQueue[32];                // +0x80 (all -1 by default)

    int   m_BombQueueCount;                // +0x208
    int   m_BombQueue[11];                 // +0x20c

    float m_GameTimer1;                    // +0x10c (-1 default)
    float m_BombHitTimer;                  // +0x130
    float m_ShakeIntensity;                // +0x138
    float m_ShakeDecay;                    // +0x13c (1.0 default)

    int   m_VersionInfo;                   // +0x1f4

    // --- Coin fields (used by ItemManager) ----------------------------
    // These are accessed by ItemManager::LoadItemData, BuyItem, SaveItemInfo.
    // Offsets per docs/structs/items.md §FruitSaveData Integration.

    int   m_Coins;                         // +0x20  current coin balance
    int   m_CoinsTotal;                    // +0x24  all-time earned coins
    int   m_LevelStartCoins;              // +0x28  coins at level start (for refund)

    // AddCoins @ 0x0010a3bc — free function in binary, method here for clarity.
    // Adds delta to m_Coins; if delta > 0 also adds to m_CoinsTotal.
    // m_LevelStartCoins is NOT modified by AddCoins (only by level-start snapshot).
    // TODO: persist to disk via SaveItemInfo when save flow is wired.
    void AddCoins(int delta);

    // --- Construction / destruction ------------------------------------

    // 0x00129e74 — default ctor; zeros fields + sets documented defaults.
    FruitSaveData();

    // 0x0010ce90.
    ~FruitSaveData();

    // --- Stat tracking (called from scoring, bomb-hit, achievements) --

    // 0x0012b21c. Increments an entry in the totals map keyed by
    // StringHash(name). Returns the new total (callers store into
    // Game::fruitTotal). The bool flags gate stat broadcasts; stubs
    // ignore them.
    int AddToTotal(const char* name, uint32_t hash, int count,
                   bool trackFruit = false, bool sendNetPacket = false);

    // Convenience overload for the `AddToTotal("bomb", 1)` call style.
    // Hashes the name internally.
    int AddToTotal(const char* name, int count);

    // 0x0012a110. Map lookup by hash; 0 if missing.
    int GetTotal(uint32_t hash) const;

    // 0x00129b94. Clears the in-session combo total.
    void ClearCombo();

    // 0x0012a034. FinishedGame — decrements modifier map counters at
    // end of round.
    void FinishedGame();

    // --- Achievements --------------------------------------------------

    // static helper used by data-parsing / dojo unlock flow.
    static bool IsAchievementUnlocked(uint32_t hash);

    // 0x00124f10 (called from WaveManager::GetNextWave). Unlocks any
    // "total-X" achievements whose thresholds have been reached.
    void UnlockTotals();

    // --- Save / Load ---------------------------------------------------

    // 0x00129ca8. Resets the "active game" snapshot (m_EntityStates
    // list, resume fields).
    void SaveGameState();

    // Daily-reset logic (called from DojoScreen / LoadGame). Stubbed.
    void CheckDatesHaveChanged();

    // Network tweak download (no-op — defunct online service).
    static void DownloadTweaks();

    // --- Per-frame tick ------------------------------------------------

    // 0x0012b3dc. Advances achievement in-progress timers. Called from
    // GameUpdate with raw dt + HUD for popup notifications.
    void Update(float dt, HUD* hud);
};

// --- Save/Load free functions (binary calls them as file-scope fns) ---

// 0x0016ccc8. Snapshot live Game state into pSaveData and write to disk.
void FruitNinja_SaveCurrentData();

// 0x0012a2fc.
void FruitNinja_SaveGame(FruitSaveData* save);

// 0x0012be74. Load save file into pSaveData. Returns true on success.
bool FruitNinja_LoadGame(FruitSaveData* save);

// 0x0016cf40. Called on app termination.
void FruitNinja_SaveOnExit();

#endif  // FN_FRUIT_SAVE_DATA_H
