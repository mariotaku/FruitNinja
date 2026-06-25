#ifndef FN_GAME_GLOBAL_PROBABILITY_OVERIDE_H
#define FN_GAME_GLOBAL_PROBABILITY_OVERIDE_H

// GlobalProbabilityOveride — probability override entry for wave spawning.
//
// ASM-spec v1.6.1 GlobalProbabilityOveride @0x00120ab0 (ctor) / ~ @0x00120fb0 (thunk 0x0010916c):
//   sizeof 0x30, non-virtual dtor (delete[] m_SaveKey; m_TypeChances.~vector());
//   5 virtual slots @ vtable 0x002cc350. Create-path (ParseGlobalProbabilityOverides
//   @0x00129718) + subclasses PointBased(0x40)/Timed(0x30) -- TODO, not yet ported.

#include <vector>
#include <stdint.h>

// TODO: v1.6.1 -- TypeChance fields not yet RE'd (POD placeholder)
struct TypeChance {
    int   m_Type;
    float m_Chance;
};

class GlobalProbabilityOveride {
public:
    GlobalProbabilityOveride();
    ~GlobalProbabilityOveride();

    // 5 virtual slots @ vtable 0x002cc350
    virtual bool CanSpawn();              // TODO: v1.6.1 0x00120d2c -- stub
    virtual void ParseSpecific();         // TODO: v1.6.1 0x00121c78 -- stub
    virtual void CheckForOverride();      // TODO: v1.6.1 0x001211cc -- stub
    virtual void PushbackSpawn();         // TODO: v1.6.1 0x00120b70 -- stub
    virtual void NewGameStarted();        // TODO: v1.6.1 0x00121c7c -- stub

    uint32_t              m_RandomSeed;           // +0x04 (ctor=0)
    std::vector<TypeChance> m_TypeChances;        // +0x08 (12B value-vector)
    char*                 m_SaveKey;              // +0x14 (ctor=0)
    uint32_t              m_SaveSubId;            // +0x18 (ctor=0)
    uint32_t              m_ModeMask;             // +0x1c (ctor=0xFFFFFFFF)
    int32_t               m_AmountMin;            // +0x20 (ctor=0)
    int32_t               m_AmountMax;            // +0x24 (ctor=0)
    bool                  m_AlwaysAllow;          // +0x28 (ctor=true)
    // +0x29..0x2b: pad
    int32_t               m_MinFruitCount;        // +0x2c (ctor=0)
    // sizeof 0x30
};

#ifdef __bada__
#include <stddef.h>
static_assert(sizeof(GlobalProbabilityOveride) == 0x30,
    "GlobalProbabilityOveride size mismatch");
static_assert(offsetof(GlobalProbabilityOveride, m_RandomSeed) == 0x04,
    "GlobalProbabilityOveride::m_RandomSeed offset mismatch");
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

#endif // FN_GAME_GLOBAL_PROBABILITY_OVERIDE_H
