#ifndef FN_ACHIEVEMENT_MANAGER_H
#define FN_ACHIEVEMENT_MANAGER_H

// Analysed: 2026-04-30T00:00
// (Extended 2026-05-03 with full struct layout from RE §7)
//
// AchievementManager — achievement tracking singleton.
// sizeof(AchievementManager) == 0x120 (288 bytes).
//   m_All          : std::map<uint32_t, AchievementInfo*>  @ 0x000 (owns heap entries)
//   m_ByType[11]   : std::map<uint32_t, AchievementInfo*>  @ 0x018..0x108 (non-owning views)
//
// Binary addresses:
//   ctor (real)                   0x00108930
//   ctor (alias)                  0x00108954
//   ctor thunk                    0x001059c0
//   dtor (regular)                0x00109028
//   dtor (deleting)               0x00109078
//   GetInstance                   0x00108f64
//   LoadAchievementInfo           0x00109200
//   UnLoadAchievementInfo         0x00108fb4
//   AchievementExists             0x00108ea4
//   UnlockBonusAchievement        0x00108af0  (was 0x00108de4)
//   UnlockComboAchievement        0x00108a10  (was 0x00108b3c)
//   UnlockComboStarAchievement    0x00108c40  (was incorrectly namespaced in old stub)
//   UnlockConsecutiveAchievement  0x00108c40
//   UnlockEndScoreAchievement     0x00108e14
//   UnlockScoreAchievement        0x00108d44
//   UnlockScoreUnsulliedAchievement 0x00108d94
//   UnlockSpecificFruitAchievement  0x00108a88
//   UnlockSpecificOrderAchievement  0x00108b58  (was 0x001089cc -- that addr is SpecificOrder::Check)
//   UnlockTotalFruitAchievement   0x00108eec
//   UnlockedAchievement           0x001090d0
//   UnlockAchievementInNetwork    0x001085a0
//   QueAchievement                0x00108978  (was 0x001084a0)

#include <cstdint>
#include <map>
#include "engine/util/SmartPtr.h"
#include "engine/asset/Texture.h"

class HUD;
class SpecificOrder;

// AchievementType constants (0..10) — from RE binary analysis
enum AchievementType {
    ACHIEVEMENT_TYPE_TOTAL            = 0,  // UnlockTotalFruitAchievement
    ACHIEVEMENT_TYPE_SCORE            = 1,  // UnlockScoreAchievement
    ACHIEVEMENT_TYPE_SCORE_UNSULLIED  = 2,  // UnlockScoreUnsulliedAchievement
    ACHIEVEMENT_TYPE_END_SCORE        = 3,  // UnlockEndScoreAchievement
    ACHIEVEMENT_TYPE_SPECIFIC         = 4,  // UnlockSpecificFruitAchievement
    ACHIEVEMENT_TYPE_CONSECUTIVE      = 5,  // UnlockConsecutiveAchievement
    ACHIEVEMENT_TYPE_CONSECUTIVE_ANY  = 6,  // (variant of consecutive, any fruit)
    ACHIEVEMENT_TYPE_COMBO            = 7,  // UnlockComboAchievement
    ACHIEVEMENT_TYPE_COMBO_STAR       = 8,  // UnlockComboStarAchievement
    ACHIEVEMENT_TYPE_SPECIFIC_ORDER   = 9,  // UnlockSpecificOrderAchievement
    ACHIEVEMENT_TYPE_BONUS            = 10, // UnlockBonusAchievement
};

// AchievementInfo — sizeof 0x1A0 (416 bytes)
// Binary layout confirmed via RE §7.
struct AchievementInfo {
    char      m_Description[64];                       // 0x000 (XML "description", GETSTRING-localised)
    char      m_Name[64];                              // 0x040 (XML "name" — also save-key string)
    uint32_t  m_NameHash;                              // 0x080
    Mortar::SmartPtr<Mortar::Texture> m_Texture;               // 0x084 (8 bytes)
    char      m_LongText[256];                         // 0x088 (optional element child text)
    int       m_Threshold;                             // 0x188 (XML "value" attr)
    int       m_Points;                                // 0x18C (XML "points" attr)
    int       m_TypeIndex;                             // 0x190 (-1 sentinel; 0..10)
    uint32_t  m_ModeBitmask;                           // 0x194
    bool      m_RequiresUnsullied;                     // 0x198
    char      _pad[3];                                 // 0x199
    SpecificOrder* m_SpecificOrder;                    // 0x19C

    AchievementInfo();
    ~AchievementInfo();
};
// Note: sizeof(AchievementInfo) == 0x1A0 on ARM32 binary.
// x64 port differs due to pointer/string sizes; assert omitted.

class AchievementManager {
public:
    // Binary @ 0x00108f64 — Meyers singleton
    static AchievementManager* GetInstance();

    // Binary @ 0x00109200 — parse xml/achievementlist.xml
    void LoadAchievementInfo();

    // Binary @ 0x00108fb4 — free m_All entries, clear all maps
    void UnLoadAchievementInfo();

    // Binary @ 0x00108ea4 — returns iterator index if hash found, -1 otherwise
    int  AchievementExists(uint32_t hash);

    // Unlock paths — Binary addresses above
    // ASM-verified: 2026-05-18T00:00 binary @ 0x00108af0..0x00108b4f (asm-inspector)
    unsigned int UnlockBonusAchievement(unsigned long bonusId);
    // ASM-verified: 2026-05-18 binary @ 0x00108a10 (re-analyst)
    int  UnlockComboAchievement(int comboLen, int* fruitArr);
    int  UnlockComboStarAchievement(int combo, uint32_t starTypeHash);
    int  UnlockConsecutiveAchievement(int count, unsigned int fruitTypeHash);
    int  UnlockEndScoreAchievement(int score, int hiScore);
    int  UnlockScoreAchievement(int score);
    int  UnlockScoreUnsulliedAchievement(int score);
    int  UnlockSpecificFruitAchievement(int fruitTypeHash, unsigned int count);
    int  UnlockSpecificOrderAchievement(uint32_t newFruitHash);
    int  UnlockTotalFruitAchievement(int total);

    // Binary @ 0x001090d0 — show popup via NotificationControl
    int  UnlockedAchievement(uint32_t hash, HUD* hud);

    // Binary @ 0x001085a0 — Defunct: NetworkManager — no-op stub
    int  UnlockAchievementInNetwork(const char* name);

private:
    AchievementManager();
    ~AchievementManager();

    // Binary @ 0x001084a0 — queue unlock; removes entry from m_ByType on success
    int  QueAchievement(AchievementInfo* info,
                        std::map<uint32_t, AchievementInfo*>::iterator& it);

    // Binary layout: 12 std::map<uint32_t,AchievementInfo*> (each 24 bytes, ARM32 Sourcery).
    // Ctor @ 0x00108930 constructs m_All at +0x000, then 11 m_ByType at +0x018..+0x108.
    // Dtor @ 0x00109028 destroys them from +0x120 down to +0x000 (12 total, stride 0x18).
    // Preamble textures (DAT_001096a8/ac/b0) are module-level statics in BSS, NOT struct members.
    std::map<uint32_t, AchievementInfo*> m_All;         // +0x000 (owns)
    std::map<uint32_t, AchievementInfo*> m_ByType[11];  // +0x018..+0x108 (non-owning views)
};
// sizeof(AchievementManager) == 0x120 (288 bytes) on ARM32 binary:
// 12 * sizeof(std::map) = 12 * 24 = 288.
#if defined(__bada__)
#include <cstddef>
static_assert(sizeof(AchievementManager) == 0x120, "AchievementManager size mismatch");
#endif

#endif // FN_ACHIEVEMENT_MANAGER_H
