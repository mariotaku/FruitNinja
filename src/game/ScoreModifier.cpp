// Analysed: 2026-05-03T00:00

#include "ScoreModifier.h"
#include "PowerUp.h"
#include "PowerUpManager.h"
#include "ScoreDelegate.h"
#include "ItemParseUtil.h"
#include <cstdint>

ScoreModifier::ScoreModifier()
    : GameModifier()
    , m_GainAdd(0)
    , m_GainMultiply(1)
    , m_LossAdd(0)
    , m_LossMultiply(1)
    , m_ApplyCount(0)
    , m_bDeferPoints(false)
    , _pad35{0, 0, 0}
    , m_DeferAccum(0)
{}

// ASM-spec v1.6.1 ScoreModifier::ResetSpecific @ 0x00147898: writes m_LossAdd=0, m_LossMultiply=1, m_GainAdd=0, m_GainMultiply=1; m_bDeferPoints NOT reset (no strb in binary).
void ScoreModifier::ResetSpecific() {
    m_LossAdd      = 0;
    m_LossMultiply = 1;
    m_GainAdd      = 0;
    m_GainMultiply = 1;
}

// @ 0x001478e0 — per-frame multiply into PowerUpManager score slots
int ScoreModifier::UpdateSpecific(float /*dt*/) {
    if (!m_bDeferPoints) {
        PowerUpManager* m = PowerUpManager::GetInstance();
        m->AddToScoreGainAdd(m_ApplyCount * m_GainAdd);
        m->AddToScoreLossAdd(m_ApplyCount * m_LossAdd);
        for (int i = 0; i < m_ApplyCount; ++i) {
            m->AddToScoreGainMultiply(m_GainMultiply);
            m->AddToScoreLossMultiply(m_LossMultiply);
        }
    }
    return 0;
}

// ASM-verified: 2026-06-20T00:00Z v1.6.1 ScoreModifier::ApplyModifier @ 0x0014798c (asm-inspector/re-analyst) -- increments m_ApplyCount@+0x30
// @ 0x0014798c
void ScoreModifier::ApplyModifier(bool isPurchased, float* extra) {
    GameModifier::ApplyModifier(isPurchased, extra);
    if (m_bDeferPoints) {
        static_cast<PowerUp*>(m_pDeferInfo)->AddDeferedPoints(0);   // clears -1 sentinel
        SetScoreDelegate(Mortar::Delegate1<int,int>::Make(this, &ScoreModifier::DeferPoints));  // v1.6.1 SetScoreDelegate @ 0x0011a440
    }
    ++m_ApplyCount;
}

// ASM-spec v1.6.1 ScoreModifier::RemoveModifier @0x00147b8c
// The port body is a genuine 4-instruction tail call into SetDefaultScoreDelegate()
// (ScoreDelegate.cpp), which has no symbol in the binary's .symtab -- so nothing pairs and
// asm-verify can never match this. Semantically equal: the binary constructs an empty
// Delegate1<int,int> in place and SetScoreDelegate substitutes DefaultScoreDelegate on null.
void ScoreModifier::RemoveModifier() {
    if (m_bDeferPoints) {
        SetDefaultScoreDelegate();       // Global<int,int>(&DefaultScoreDelegate) (SetDefaultScoreDelegate addr unresolved in v1.6.1 .symtab)
    }
}

// ASM-verified: 2026-06-20T00:00Z v1.6.1 ScoreModifier::ParseSpecific @ 0x00147abc (re-analyst)
void ScoreModifier::ParseSpecific(TiXmlElement* xml) {
    TiXmlElement mult = xml->FirstChildElement("multiplier");
    ResetSpecific();
    if (mult) {
        mult.QueryIntAttribute("gainAdd",      &m_GainAdd);
        mult.QueryIntAttribute("gainMultiply", &m_GainMultiply);
        mult.QueryIntAttribute("lossAdd",      &m_LossAdd);
        mult.QueryIntAttribute("lossMultiply", &m_LossMultiply);
        const char* defer = mult.Attribute("deferPoints");
        m_bDeferPoints = (defer && CompareWords(defer, "true"));
    }
}

// ASM-spec v1.6.1 ScoreModifier::Clone @ 0x00147a58: memberwise-copy all fields then force clone m_bConfigured=0
GameModifier* ScoreModifier::Clone() {
    ScoreModifier* c = new ScoreModifier();
    *c = *this;
    c->m_bConfigured = 0;
    return c;
}

// ASM-verified: 2026-06-20T00:00Z v1.6.1 ScoreModifier::DeferPoints @ 0x001478b8 (re-analyst) -- accumulates into m_DeferAccum@+0x38
// @ 0x001478b8
int ScoreModifier::DeferPoints(int points) {
    static_cast<PowerUp*>(m_pDeferInfo)->AddDeferedPoints(points);
    m_DeferAccum += points;
    return 0;
}
