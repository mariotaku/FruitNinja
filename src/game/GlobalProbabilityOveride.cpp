#include "game/GlobalProbabilityOveride.h"

// ASM-spec v1.6.1 GlobalProbabilityOveride::GlobalProbabilityOveride @0x00120ab0
GlobalProbabilityOveride::GlobalProbabilityOveride()
    : m_RandomSeed(0)
    , m_SaveKey(0)
    , m_SaveSubId(0)
    , m_ModeMask(0xFFFFFFFF)
    , m_AmountMin(0)
    , m_AmountMax(0)
    , m_AlwaysAllow(true)
    , m_MinFruitCount(0)
{
}

// ASM-spec v1.6.1 GlobalProbabilityOveride::~GlobalProbabilityOveride @0x00120fb0
GlobalProbabilityOveride::~GlobalProbabilityOveride()
{
    if (m_SaveKey) {
        operator delete[](m_SaveKey);
        m_SaveKey = 0;
    }
    // m_TypeChances destructs naturally (frees its buffer)
}

// Virtual stub bodies (vtable emitted here, out-of-line)

// TODO: v1.6.1 0x00120d2c -- stub
bool GlobalProbabilityOveride::CanSpawn()
{
    return false;
}

// TODO: v1.6.1 0x00121c78 -- stub
void GlobalProbabilityOveride::ParseSpecific()
{
}

// TODO: v1.6.1 0x001211cc -- stub
void GlobalProbabilityOveride::CheckForOverride()
{
}

// TODO: v1.6.1 0x00120b70 -- stub
void GlobalProbabilityOveride::PushbackSpawn()
{
}

// TODO: v1.6.1 0x00121c7c -- stub
void GlobalProbabilityOveride::NewGameStarted()
{
}
