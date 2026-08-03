#ifndef FN_GAME_GLOBAL_PROBABILITY_OVERIDE_H
#define FN_GAME_GLOBAL_PROBABILITY_OVERIDE_H

// GlobalProbabilityOveride — probability override entry for wave spawning.
//
// v1.6.1 base ctor @0x00120ab0, dtor @0x00120fb0, sizeof 0x30.
// 5 virtual slots @ vtable 0x002cc350.
// Subclasses:
//   GlobalProbabilityOveridePointBased: sizeof 0x40, vtable @0x002cc310
//   GlobalProbabilityOverideTimed:      sizeof 0x30, vtable @0x002cc330
//
// Non-virtual parse/pick helpers: Parse @0x001215e4, PickFruit @0x00120e04,
//   FruitWasKilled @0x00120a7c, FruitWasThrown @0x00120dc4.
// T_872 @0x0012118c (rand[0,1)), T_877 @0x001212e0 (int-range-pick).

#include <vector>
#include <string>
#include <stdint.h>

struct TiXmlElement;
class Fruit;

// TypeChance — stride 0x0c, field 0 is std::string.
// ASM-spec v1.6.1 TypeChance::TypeChance @0x00121cac
struct TypeChance {
    std::string m_TypeName;  // +0x00 (4B on ARM32 Sourcery libstdc++)
    int32_t     m_Chance;    // +0x04 ctor default 100
    int32_t     m_CumChance; // +0x08 cumulative threshold; PickFruit compares rand < this

    TypeChance() : m_Chance(100), m_CumChance(0) {}
};

#ifdef __bada__
static_assert(sizeof(TypeChance) == 0x0c, "TypeChance size mismatch");
static_assert(offsetof(TypeChance, m_Chance)    == 0x04, "TypeChance::m_Chance offset mismatch");
static_assert(offsetof(TypeChance, m_CumChance) == 0x08, "TypeChance::m_CumChance offset mismatch");
#endif

class GlobalProbabilityOveride {
public:
    GlobalProbabilityOveride();
    // Non-virtual: the binary has NO dtor vtable slot (5 slots total, CanSpawn
    // at slot 0). WaveManager::Destroy @0x00123b54 calls
    // ~GlobalProbabilityOveride(this) as a direct static call + operator_delete,
    // never through the vptr (~GlobalProbabilityOveride @0x00120fb0 has zero
    // vtable xrefs). Do not re-add `virtual` here -- it would insert two
    // Itanium-ABI dtor slots (D1+D0) ahead of CanSpawn and shift every
    // dispatch index by 2.
    ~GlobalProbabilityOveride();

    // v1.6.1 vtable @0x002cc350 — 5 slots
    virtual bool CanSpawn();                            // slot0 @0x00120d2c
    virtual void ParseSpecific(TiXmlElement* e);        // slot1 @0x00121c78 (base no-op)
    virtual bool CheckForOverride(int& outType);        // slot2 @0x001211cc
    virtual void PushbackSpawn();                       // slot3 @0x00120b70
    virtual void NewGameStarted();                      // slot4 @0x00121c7c (base no-op)

    // Non-virtual methods
    void Parse(TiXmlElement* e);        // @0x001215e4 — reads attrs + <fruit> children
    int  PickFruit();                   // @0x00120e04 — rand pick from m_TypeChances
    void FruitWasKilled(Fruit* f);      // @0x00120a7c — delegate handler
    void FruitWasThrown(Fruit* f);      // @0x00120dc4 — delegate handler

    // +0x04: cumulative chance total accumulated in Parse; Rand32 modulus in PickFruit
    int32_t               m_TotalChance;        // +0x04 (ctor=0; was m_RandomSeed)
    std::vector<TypeChance> m_TypeChances;      // +0x08 (12B value-vector on ARM32)
    char*                 m_SaveKey;            // +0x14 (ctor=0; heap-dup'd in Parse)
    uint32_t              m_SaveSubId;          // +0x18 (ctor=0; StringHash of m_SaveKey)
    uint32_t              m_ModeMask;           // +0x1c (ctor=0xFFFFFFFF)
    int32_t               m_AmountMin;          // +0x20 (ctor=0; XML "minWait")
    int32_t               m_AmountMax;          // +0x24 (ctor=0; XML "maxWait")
    bool                  m_AlwaysAllow;        // +0x28 (ctor=true; XML "canSpawnWithPowers")
    // +0x29..0x2b: pad
    int32_t               m_MinFruitCount;      // +0x2c (ctor=0; XML "dontSpawnBeforeWave")
    // sizeof 0x30
};

#ifdef __bada__
#include <stddef.h>
static_assert(sizeof(GlobalProbabilityOveride) == 0x30,
    "GlobalProbabilityOveride size mismatch");
static_assert(offsetof(GlobalProbabilityOveride, m_TotalChance) == 0x04,
    "GlobalProbabilityOveride::m_TotalChance offset mismatch");
static_assert(offsetof(GlobalProbabilityOveride, m_TypeChances) == 0x08,
    "GlobalProbabilityOveride::m_TypeChances offset mismatch");
static_assert(offsetof(GlobalProbabilityOveride, m_SaveKey) == 0x14,
    "GlobalProbabilityOveride::m_SaveKey offset mismatch");
static_assert(offsetof(GlobalProbabilityOveride, m_SaveSubId) == 0x18,
    "GlobalProbabilityOveride::m_SaveSubId offset mismatch");
static_assert(offsetof(GlobalProbabilityOveride, m_ModeMask) == 0x1c,
    "GlobalProbabilityOveride::m_ModeMask offset mismatch");
static_assert(offsetof(GlobalProbabilityOveride, m_AmountMin) == 0x20,
    "GlobalProbabilityOveride::m_AmountMin offset mismatch");
static_assert(offsetof(GlobalProbabilityOveride, m_AmountMax) == 0x24,
    "GlobalProbabilityOveride::m_AmountMax offset mismatch");
static_assert(offsetof(GlobalProbabilityOveride, m_AlwaysAllow) == 0x28,
    "GlobalProbabilityOveride::m_AlwaysAllow offset mismatch");
static_assert(offsetof(GlobalProbabilityOveride, m_MinFruitCount) == 0x2c,
    "GlobalProbabilityOveride::m_MinFruitCount offset mismatch");
#endif

// GlobalProbabilityOveridePointBased — sizeof 0x40, vtable @0x002cc310.
// v1.6.1 ctor @0x0012ac30.
// Overrides: slot1 ParseSpecific=0x00120c44, slot2 CheckForOverride=0x00121320,
//            slot3 PushbackSpawn=0x00120bf4, slot4 NewGameStarted=0x0012140c.
// slot0 CanSpawn inherits base @0x00120d2c.
class GlobalProbabilityOveridePointBased : public GlobalProbabilityOveride {
public:
    GlobalProbabilityOveridePointBased();
    ~GlobalProbabilityOveridePointBased(); // non-virtual, see base dtor note

    void ParseSpecific(TiXmlElement* e);    // slot1 @0x00120c44
    bool CheckForOverride(int& outType);    // slot2 @0x00121320
    void PushbackSpawn();                   // slot3 @0x00120bf4
    void NewGameStarted();                  // slot4 @0x0012140c

    int32_t m_Every;    // +0x30 (XML "every"/"everyMin")
    int32_t m_EveryMax; // +0x34 (XML "everyMax"; default = m_Every)
    int32_t m_From;     // +0x38 (XML "from"/"fromMin")
    int32_t m_FromMax;  // +0x3c (XML "fromMax"; default = m_From)
    // sizeof 0x40
};

#ifdef __bada__
static_assert(sizeof(GlobalProbabilityOveridePointBased) == 0x40,
    "GlobalProbabilityOveridePointBased size mismatch");
static_assert(offsetof(GlobalProbabilityOveridePointBased, m_Every) == 0x30,
    "GlobalProbabilityOveridePointBased::m_Every offset mismatch");
#endif

// GlobalProbabilityOverideTimed — sizeof 0x30, vtable @0x002cc330.
// v1.6.1 ctor @0x0012ac64. No extra fields (ParseSpecific @0x00120aac is no-op).
// Reuses base m_AmountMin/m_AmountMax as timed min/max-wait.
// Overrides: slot1=0x00120aac (no-op), slot2 CheckForOverride=0x00120e90,
//            slot3 PushbackSpawn=0x00120bac, slot4 NewGameStarted=0x0012145c.
class GlobalProbabilityOverideTimed : public GlobalProbabilityOveride {
public:
    GlobalProbabilityOverideTimed();
    ~GlobalProbabilityOverideTimed(); // non-virtual, see base dtor note

    void ParseSpecific(TiXmlElement* e);    // slot1 @0x00120aac (no-op)
    bool CheckForOverride(int& outType);    // slot2 @0x00120e90
    void PushbackSpawn();                   // slot3 @0x00120bac
    void NewGameStarted();                  // slot4 @0x0012145c
    // sizeof 0x30 (no extra fields)
};

#ifdef __bada__
static_assert(sizeof(GlobalProbabilityOverideTimed) == 0x30,
    "GlobalProbabilityOverideTimed size mismatch");
#endif

#endif // FN_GAME_GLOBAL_PROBABILITY_OVERIDE_H
