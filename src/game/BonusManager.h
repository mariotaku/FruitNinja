#ifndef FN_GAME_BONUS_MANAGER_H
#define FN_GAME_BONUS_MANAGER_H

// BonusManager -- post-game bonus award tracker.
// sizeof 0x20 = 32 bytes.
// Singleton ctor @ binary BSS, Init @ v1.6.1 0x0012f53c, ClearBestBonuses @ 0x000feb20.

#include "Bonus.h"
#include <vector>
#include <list>
#include <cstdint>

class BonusScreen;

class BonusManager {
public:
    std::vector<BonusType> m_AllBonuses;        // +0x00  sizeof 12
    std::list<Bonus>       m_BestBonuses;       // +0x0C  sizeof 8
    std::vector<int>       m_ComboTotalsByLevel; // +0x14  sizeof 12

    static BonusManager* GetInstance();

    void Init();                                // v1.6.1 BonusManager::Init @0x0012f53c
    void ClearBestBonuses();                    // v1.6.1 BonusManager::ClearBestBonuses @0x0012eb38
    void SetUpBonusScreen(BonusScreen* screen); // v1.6.1 BonusManager::SetUpBonusScreen @0x0012ede8
    void AddCombo(int comboLen);                // v1.6.1 BonusManager::AddCombo @0x0012e570
    bool UnlockPostGameAchievements();          // v1.6.1 BonusManager::UnlockPostGameAchievements @0x0012eae4

    Bonus* GetFirstBestBonus(std::list<Bonus>::iterator& it);
    Bonus* GetNextBestBonus(std::list<Bonus>::iterator& it);

private:
    BonusManager();
    ~BonusManager();
};

// Port specific: releases the 12 bonus_icon_* textures held by
// BonusManager::m_AllBonuses (+0x00) -- each Bonus owns m_StarTexture at +0xD0,
// loaded in Bonus::Parse / BonusType::Parse. v1.6.1 has NO unload path for
// m_AllBonuses: ClearBestBonuses @0x000feb20 only clears m_BestBonuses (+0x0C),
// and BonusManager is a singleton, so the SmartPtrs are dropped by the
// singleton's destructor in the static-dtor/atexit chain. That is harmless on
// Bada (GL context still live at atexit) but leaks the GL texture names here,
// because the port's atexit runs after SDL_GL_DeleteContext. Called from
// GameDestroy, before MeshManager::Destroy().
//
// Also drops m_BestBonuses via the faithful ClearBestBonuses(), since those are
// by-value Bonus copies that each hold their own m_StarTexture reference.
//
// Not idempotency-safe for reuse: BonusManager::Init() must run again (it
// re-parses bonusAwards.xml) before the icons are available. Only call at
// shutdown.
void BonusManager_UnloadTextures();

// Layout asserts: ARM32 sizes only. GCC 4.4.1 excluded (std::vector/list sizes differ).
#ifdef __bada__
static_assert(sizeof(BonusManager) == 0x20, "BonusManager size mismatch");
static_assert(__builtin_offsetof(BonusManager, m_AllBonuses)        == 0x00, "BonusManager::m_AllBonuses offset");
static_assert(__builtin_offsetof(BonusManager, m_BestBonuses)       == 0x0C, "BonusManager::m_BestBonuses offset");
static_assert(__builtin_offsetof(BonusManager, m_ComboTotalsByLevel) == 0x14, "BonusManager::m_ComboTotalsByLevel offset");
#endif

#endif // FN_GAME_BONUS_MANAGER_H
