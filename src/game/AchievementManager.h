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
// Binary addresses (all v1.6.1 entry points; cite the entry, never a PLT thunk):
//   ctor                          0x00117494, 0x001174d0  (two emitted variants)
//   dtor                          0x00117f58, 0x00117ff8  (regular / deleting)
//   GetInstance                   0x00117e08
//   LoadAchievementInfo           0x00118198
//   UnLoadAchievementInfo         0x00117ea4
//   AchievementExists             0x00116ea8
//   UnlockBonusAchievement        0x0011773c
//   UnlockComboAchievement        0x001175e8
//   UnlockComboStarAchievement    0x00117b20
//   UnlockConsecutiveAchievement  0x00117948
//   UnlockEndScoreAchievement     0x00117880
//   UnlockScoreAchievement        0x00117bd0
//   UnlockScoreUnsulliedAchievement 0x00117c8c
//   UnlockSpecificFruitAchievement  0x00117a68
//   UnlockSpecificOrderAchievement  0x001177e0
//   UnlockTotalFruitAchievement   0x00117d48
//   UnlockedAchievement           0x001180a8
//   UnlockAchievementInNetwork    0x00116ee4
//   QueAchievement                0x0011750c

#include <cstdint>
#include <map>
#include <vector>
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

// AchievementInfo — sizeof 0xa8 (168 bytes) on ARM32.
// Binary: operator new(0xa8) @0x00118198 v1.6.1 AchievementInfo ctor.
// Layout (ARM32, 4-byte ptrs, short-enums ABI):
//   0x00  char m_DisplayName[64]         GETSTRING(name attr) -- localized, drawn by popup
//   0x40  char m_Name[64]                raw "id" attribute -- keying/type/AddToQue
//   0x80  uint32_t m_NameHash            StringHash(m_Name) (== id hash; map key)
//   0x84  SmartPtr<Texture> m_Texture    4 bytes (single T*)
//   0x88  const char* m_pDescription     GETSTRING key pointer (constructed *_DESC_XX key); 0 if none
//   0x8c  int m_Total                    XML "total" attribute threshold
//   0x90  int m_Score                    XML "score" attribute
//   0x94  uint32_t m_TypeIndex           0xb (11) sentinel in ctor; 0..10 on valid entry
//   0x98  uint32_t m_ModeBitmask         bit(GAME_MODE_CLASSIC=0)|bit(CASINO=1)|bit(ARCADE=2)|bit(ZEN=3);
//                                        0xFFFFFFFF wildcard for absent/unrecognized mode attr (ALL/ANY)
//   0x9c  bool m_IsGameOver              XML "isGameOver" attribute == 1
//   0x9d  char _pad[7]
//   0xa4  SpecificOrder* m_SpecificOrder
struct AchievementInfo {
    char      m_DisplayName[64];         // 0x00 localized, drawn by popup
    char      m_Name[64];                // 0x40 raw id: keying/type/AddToQue
    uint32_t  m_NameHash;                // 0x80
    Mortar::SmartPtr<Mortar::Texture> m_Texture; // 0x84 (4 bytes on ARM32)
    const char* m_pDescription;          // 0x88 (GETSTRING key ptr; 0 if none)
    int       m_Total;                   // 0x8c (XML "total" attr)
    int       m_Score;                   // 0x90 (XML "score" attr)
    uint32_t  m_TypeIndex;               // 0x94 (0xb sentinel; 0..10)
    uint32_t  m_ModeBitmask;             // 0x98
    bool      m_IsGameOver;              // 0x9c (XML "isGameOver" attr == 1)
    char      _pad[7];                   // 0x9d
    SpecificOrder* m_SpecificOrder;      // 0xa4

    AchievementInfo();
    ~AchievementInfo();
};

#if defined(__bada__)
#include <cstddef>
static_assert(sizeof(AchievementInfo) == 0xa8, "AchievementInfo size mismatch (v1.6.1 @0x00118198)");
static_assert(__builtin_offsetof(AchievementInfo, m_Name)               == 0x40, "AchievementInfo::m_Name");
static_assert(__builtin_offsetof(AchievementInfo, m_NameHash)           == 0x80, "AchievementInfo::m_NameHash");
static_assert(__builtin_offsetof(AchievementInfo, m_Texture)            == 0x84, "AchievementInfo::m_Texture");
static_assert(__builtin_offsetof(AchievementInfo, m_pDescription)       == 0x88, "AchievementInfo::m_pDescription");
static_assert(__builtin_offsetof(AchievementInfo, m_Total)              == 0x8c, "AchievementInfo::m_Total");
static_assert(__builtin_offsetof(AchievementInfo, m_Score)              == 0x90, "AchievementInfo::m_Score");
static_assert(__builtin_offsetof(AchievementInfo, m_TypeIndex)          == 0x94, "AchievementInfo::m_TypeIndex");
static_assert(__builtin_offsetof(AchievementInfo, m_ModeBitmask)        == 0x98, "AchievementInfo::m_ModeBitmask");
static_assert(__builtin_offsetof(AchievementInfo, m_IsGameOver)         == 0x9c, "AchievementInfo::m_IsGameOver");
static_assert(__builtin_offsetof(AchievementInfo, m_SpecificOrder)      == 0xa4, "AchievementInfo::m_SpecificOrder");
#endif

class AchievementManager {
public:
    // v1.6.1 AchievementManager::GetInstance @0x00117e08 — Meyers singleton
    static AchievementManager* GetInstance();

    // v1.6.1 AchievementManager::LoadAchievementInfo @0x00118198 — parse
    // xml/achievementList.xml (camelCase, per v1.6.1 literal)
    void LoadAchievementInfo();

    // v1.6.1 AchievementManager::UnLoadAchievementInfo @0x00117ea4 — nulls the two
    // preamble banner textures LoadAchievementInfo took (NotificationControl::s_banner
    // / s_unlockBanner), frees the m_All entries, then clears all maps. Called from
    // GameDestroy immediately before ItemManager::UnLoadItemData, and again from the
    // singleton dtor at atexit — idempotent, safe to call twice.
    void UnLoadAchievementInfo();

    // ASM-verified: 2026-05-18T00:00 v1.6.1 AchievementManager::AchievementExists @ 0x00116ea8 (asm-inspector)
    // Returns iterator index if hash found, -1 otherwise.
    int  AchievementExists(uint32_t hash);

    // Unlock paths — Binary addresses above
    // ASM-verified: 2026-05-18T00:00 v1.6.1 AchievementManager::UnlockBonusAchievement @ 0x0011773c (asm-inspector)
    unsigned int UnlockBonusAchievement(unsigned long bonusId);
    // ASM-spec v1.6.1 AchievementManager::UnlockComboAchievement @ 0x001175e8
    int  UnlockComboAchievement(int comboLen, int* fruitArr);
    int  UnlockComboStarAchievement(int combo, uint32_t starTypeHash);
    int  UnlockConsecutiveAchievement(int count, unsigned int fruitTypeHash);
    int  UnlockEndScoreAchievement(int score, int hiScore);
    int  UnlockScoreAchievement(int score);
    int  UnlockScoreUnsulliedAchievement(int score);
    int  UnlockSpecificFruitAchievement(int fruitTypeHash, unsigned int count);
    int  UnlockSpecificOrderAchievement(uint32_t newFruitHash);
    // v1.6.1 @0x00117d48 — threshold test only, NO mode-bitmask gate (unlike
    // UnlockScoreAchievement). Returns 0/1 "queued something", not a count.
    int  UnlockTotalFruitAchievement(int total);

    // v1.6.1 AchievementManager::UnlockedAchievement @0x001180a8 — show popup via
    // NotificationControl
    int  UnlockedAchievement(uint32_t hash, HUD* hud);

    // Defunct: NetworkManager — no-op stub; v1.6.1 AchievementManager::UnlockAchievementInNetwork @ 0x00116ee4
    int  UnlockAchievementInNetwork(const char* name);

private:
    AchievementManager();
    ~AchievementManager();

    // v1.6.1 AchievementManager::QueAchievement @0x0011750c — queue unlock; removes
    // entry from m_ByType on success
    int  QueAchievement(AchievementInfo* info,
                        std::map<uint32_t, AchievementInfo*>::iterator& it);

public:
    // Layout members kept public (matches BonusManager/WaveManager/ActorManager
    // convention in this codebase) so the __bada__ offsetof asserts below can see
    // them from namespace scope -- GCC 4.4.1 enforces access control on
    // __builtin_offsetof, unlike some other offsetof() implementations.
    //
    // Binary layout (v1.6.1 ctor @0x00117494): 12 std::map<uint32_t,AchievementInfo*>
    // (each 24 bytes, ARM32 Sourcery) at +0x000..+0x120, then a std::vector<uint32_t>
    // at +0x120. sizeof == 0x12c.
    // Preamble textures (DAT_001096a8/ac/b0) are module-level statics in BSS, NOT struct members.
    std::map<uint32_t, AchievementInfo*> m_All;         // +0x000 (owns)
    std::map<uint32_t, AchievementInfo*> m_ByType[11];  // +0x018..+0x108 (non-owning views)
    // v1.6.1 ctor @0x00117494 constructs a std::vector<u32> at +0x120; LoadAchievementInfo
    // @0x00118198 push_back's each kept achievement id-hash. DEAD FIELD in v1.6.1: no
    // reader (all 14 GetInstance callers audited; zero xrefs to +0x120) -- relic of a
    // refactored-out OpenFeint bulk-publish path. Kept for byte-faithful layout.
    std::vector<uint32_t> m_AllHashes;                  // +0x120
};
// sizeof(AchievementManager) == 0x12c on ARM32 binary: 12*24 (maps) + 12 (vector).
#if defined(__bada__)
#include <cstddef>
static_assert(sizeof(AchievementManager) == 0x12c, "AchievementManager size mismatch");
static_assert(offsetof(AchievementManager, m_AllHashes) == 0x120, "m_AllHashes offset");
#endif

class TiXmlElement;
class FruitSaveData;

// Defunct: achievements parse -- no-op stub; v1.6.1 ParseAchievements @0x00154830
void ParseAchievements(TiXmlElement* root, FruitSaveData* save, bool reset);

#endif // FN_ACHIEVEMENT_MANAGER_H
