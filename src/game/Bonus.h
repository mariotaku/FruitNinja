#ifndef FN_GAME_BONUS_H
#define FN_GAME_BONUS_H

// Bonus / BonusType -- per-round award structs.
// Bonus  sizeof 0xD4 binary ctor @ 0x0010005c, dtor @ 0x0010fa40.
// BonusType sizeof 0x28 binary ctor @ 0x0010df00.

#include <vector>
#include <map>
#include <cstdint>
#include "engine/util/SmartPtr.h"
#include "engine/asset/Texture.h"

#include "engine/xml/TiXmlElement.h"

// ---------------------------------------------------------------------------
// Bonus
// sizeof 0xD4 = 212 bytes
// ---------------------------------------------------------------------------
class Bonus {
public:
    int                             m_MinSliced;        // +0x00
    int                             m_MaxSliced;        // +0x04
    std::map<unsigned long, int>     m_MinFruit;         // +0x08  sizeof 24
    std::map<unsigned long, int>     m_MaxFruit;         // +0x20  sizeof 24
    int                             m_DivisibleBy;      // +0x38
    int                             m_Tier;             // +0x3C  (binary ctor default 5; -1 used as invalid sentinel)
    char                            m_NameTemplate[64]; // +0x40
    char                            m_DisplayName[64];  // +0x80
    std::vector<uint32_t>           m_PatternHashes;    // +0xC0  sizeof 12
    uint32_t                        m_AchievementHash;  // +0xCC
    Mortar::SmartPtr<Mortar::Texture>       m_StarTexture;      // +0xD0  sizeof 4

    Bonus();                                            // Binary @ 0x0010005c
    Bonus(const Bonus& rhs);                            // Binary @ 0x00110090
    ~Bonus();                                           // TODO: address unresolved (0x0010fa40's
                                                         // PLT thunk resolves to std::list<Fruit*>::{dtor},
                                                         // but Bonus has no such member)
    Bonus& operator=(const Bonus& rhs);

    void Parse(TiXmlElement* e); // Binary @ 0x0012f0f8
    int  IsAchieved(int score, std::map<unsigned long, int>& fruitCounts); // Binary @ 0x0010df38

    bool operator<(const Bonus& rhs) const { return m_Tier < rhs.m_Tier; } // ascending sort (binary @ 0x0010ed2c)
};

// Layout asserts: ARM32 sizes only (binary target). Not checked on MSVC x64 host.
// sizeof(Bonus) = 0xD4 (212). ASM: new_allocator<Bonus>::allocate @0x00130360 element stride
// #0xd4 (overflow guard 0x013521CF = 2^32/0xD4). No std::map tail-padding -- map members hold
// 4-aligned _Rb_tree pointers (uint32_t keys in heap nodes), so max alignment is 4 and the
// struct ends at m_StarTexture(0xD0)+4 = 0xD4.
#ifdef __bada__
static_assert(sizeof(Bonus) == 0xD4, "Bonus size mismatch");
static_assert(__builtin_offsetof(Bonus, m_MinSliced)      == 0x00, "Bonus::m_MinSliced offset");
static_assert(__builtin_offsetof(Bonus, m_MaxSliced)      == 0x04, "Bonus::m_MaxSliced offset");
static_assert(__builtin_offsetof(Bonus, m_MinFruit)       == 0x08, "Bonus::m_MinFruit offset");
static_assert(__builtin_offsetof(Bonus, m_MaxFruit)       == 0x20, "Bonus::m_MaxFruit offset");
static_assert(__builtin_offsetof(Bonus, m_DivisibleBy)    == 0x38, "Bonus::m_DivisibleBy offset");
static_assert(__builtin_offsetof(Bonus, m_Tier)           == 0x3C, "Bonus::m_Tier offset");
static_assert(__builtin_offsetof(Bonus, m_NameTemplate)   == 0x40, "Bonus::m_NameTemplate offset");
static_assert(__builtin_offsetof(Bonus, m_DisplayName)    == 0x80, "Bonus::m_DisplayName offset");
static_assert(__builtin_offsetof(Bonus, m_PatternHashes)  == 0xC0, "Bonus::m_PatternHashes offset");
static_assert(__builtin_offsetof(Bonus, m_AchievementHash)== 0xCC, "Bonus::m_AchievementHash offset");
static_assert(__builtin_offsetof(Bonus, m_StarTexture)    == 0xD0, "Bonus::m_StarTexture offset");
#endif

// ---------------------------------------------------------------------------
// BonusType
// sizeof 0x28 = 40 bytes
// ---------------------------------------------------------------------------
class BonusType {
public:
    std::map<unsigned long, int>   m_RequiredHashes; // +0x00  sizeof 24
    std::vector<Bonus>        m_Bonuses;         // +0x18  sizeof 12
    bool                      m_HasAchievement;  // +0x24

    BonusType();                                 // Binary @ 0x0010df00
    BonusType(const BonusType& rhs);             // TODO: address unresolved (0x0010df1c's PLT
                                                  // thunk resolves to std::list<Bonus>::_M_transfer,
                                                  // but BonusType holds std::vector<Bonus>)
    ~BonusType();
    BonusType& operator=(const BonusType& rhs);

    void   Parse(TiXmlElement* e);       // Binary @ 0x0012f3a4
    Bonus* GetBest();                            // Binary @ 0x0010e094
    bool   UnlockAchievements();                 // Binary @ 0x0010e12c
};

// Layout asserts: ARM32 sizes only. GCC 4.4.1 excluded (see Bonus asserts above).
#ifdef __bada__
static_assert(sizeof(BonusType) == 0x28, "BonusType size mismatch");
static_assert(__builtin_offsetof(BonusType, m_RequiredHashes) == 0x00, "BonusType::m_RequiredHashes offset");
static_assert(__builtin_offsetof(BonusType, m_Bonuses)        == 0x18, "BonusType::m_Bonuses offset");
static_assert(__builtin_offsetof(BonusType, m_HasAchievement) == 0x24, "BonusType::m_HasAchievement offset");
#endif

#endif // FN_GAME_BONUS_H
