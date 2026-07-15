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
    void ClearBestBonuses();                    // Binary @ 0x000feb20
    void SetUpBonusScreen(BonusScreen* screen); // v1.6.1 BonusManager::SetUpBonusScreen @0x0012ede8
    void AddCombo(int comboLen);                // v1.6.1 BonusManager::AddCombo @0x0012e570
    bool UnlockPostGameAchievements();          // Binary @ 0x0010e1cc

    Bonus* GetFirstBestBonus(std::list<Bonus>::iterator& it);
    Bonus* GetNextBestBonus(std::list<Bonus>::iterator& it);

private:
    BonusManager();
    ~BonusManager();
};

// Layout asserts: ARM32 sizes only. GCC 4.4.1 excluded (std::vector/list sizes differ).
#ifdef __bada__
static_assert(sizeof(BonusManager) == 0x20, "BonusManager size mismatch");
static_assert(__builtin_offsetof(BonusManager, m_AllBonuses)        == 0x00, "BonusManager::m_AllBonuses offset");
static_assert(__builtin_offsetof(BonusManager, m_BestBonuses)       == 0x0C, "BonusManager::m_BestBonuses offset");
static_assert(__builtin_offsetof(BonusManager, m_ComboTotalsByLevel) == 0x14, "BonusManager::m_ComboTotalsByLevel offset");
#endif

#endif // FN_GAME_BONUS_MANAGER_H
