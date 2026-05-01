// Analysed: 2026-04-30T00:00

#include "WaveModifier.h"
#include "PowerUpManager.h"
#include "WaveManager.h"

WaveModifier::WaveModifier()
    : GameModifier()
    , m_BombMult(1.0f)
    , m_BombScale(1.0f)
    , m_FruitMult(1.0f)
    , m_DtMod(1.0f)
    , m_OverideProbabilityPool(10000)
    , m_CritChanceMod(1.0f)
{}

// @ 0x001280e4
int WaveModifier::UpdateSpecific(float /*dt*/) {
    WaveManager*    w = WaveManager::GetInstance();
    PowerUpManager* p = PowerUpManager::GetInstance();
    w->FruitMultiplyer(m_FruitMult);
    w->BombMultiplyer(m_BombMult);
    w->BombScale(m_BombScale);
    w->CriticalChanceMod(m_CritChanceMod);
    p->PowerupDtModMultiply(m_DtMod);
    return 0;
}

// @ 0x0012836c
void WaveModifier::ParseSpecific(TiXmlElement* /*xml*/) {
    // TODO: parse bombMultiplyer/bombScale/fruitMultiplyer/dtMod/
    //       criticalChance/overrideProbabilityPool + <override> children
}

GameModifier* WaveModifier::Clone() {
    WaveModifier* c = new WaveModifier();
    *c = *this;
    c->m_OverideEntries.clear();
    return c;
}
