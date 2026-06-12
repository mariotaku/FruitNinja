// Analysed: 2026-05-03T00:00

#include "ScoreModifier.h"
#include "PowerUp.h"
#include "PowerUpManager.h"
#include "ScoreDelegate.h"
#include "ItemParseUtil.h"
#include <tinyxml2.h>
#include <cstdint>

ScoreModifier::ScoreModifier()
    : GameModifier()
    , m_GainAdd(0)
    , m_GainMultiply(1)
    , m_LossAdd(0)
    , m_LossMultiply(1)
    , m_RepeatCount(0)
    , m_bDeferPoints(false)
    , _pad35{0, 0, 0}
    , m_ApplyCount(0)
{}

// @ 0x0011cb44
// Binary writes: [r0,#0x28]=0 (m_LossAdd), [r0,#0x2c]=1 (m_LossMultiply),
//                [r0,#0x20]=0 (m_GainAdd),  [r0,#0x24]=1 (m_GainMultiply).
// m_bDeferPoints (+0x34) is NOT reset here — binary @ 0x0011cb44 has no strb.
void ScoreModifier::ResetSpecific() {
    m_LossAdd      = 0;
    m_LossMultiply = 1;
    m_GainAdd      = 0;
    m_GainMultiply = 1;
    // Binary @ 0x0011cb44 -- m_bDeferPoints NOT reset here.
}

// @ 0x0011cb70 — per-frame multiply into PowerUpManager score slots
int ScoreModifier::UpdateSpecific(float /*dt*/) {
    if (!m_bDeferPoints) {
        PowerUpManager* m = PowerUpManager::GetInstance();
        m->AddToScoreGainAdd(m_RepeatCount * m_GainAdd);
        m->AddToScoreLossAdd(m_RepeatCount * m_LossAdd);
        for (int i = 0; i < m_RepeatCount; ++i) {
            m->AddToScoreGainMultiply(m_GainMultiply);
            m->AddToScoreLossMultiply(m_LossMultiply);
        }
    }
    return 0;
}

// @ 0x0011cbe8
void ScoreModifier::ApplyModifier(bool isPurchased, float* extra) {
    GameModifier::ApplyModifier(isPurchased, extra);
    if (m_bDeferPoints) {
        static_cast<PowerUp*>(m_pDeferInfo)->AddDeferedPoints(0);   // clears -1 sentinel (binary L11790c)
        SetScoreDelegate(this);           // Callee<ScoreModifier> trampoline (binary L11cc1c)
    }
    ++m_ApplyCount;
}

// @ 0x0011cd44
void ScoreModifier::RemoveModifier() {
    if (m_bDeferPoints) {
        SetDefaultScoreDelegate();       // Global<int,int>(&DefaultScoreDelegate) (binary L11cda4)
    }
}

// @ 0x0011ccb0
// ASM-verified: 2026-05-03T00:00 binary @ 0x0011ccb0..0x0011cd20 (asm-inspector)
void ScoreModifier::ParseSpecific(TiXmlElement* xml) {
    TiXmlElement* mult = xml->FirstChildElement("multiplier");
    ResetSpecific();
    if (mult) {
        mult->QueryIntAttribute("gainAdd",      &m_GainAdd);
        mult->QueryIntAttribute("gainMultiply", &m_GainMultiply);
        mult->QueryIntAttribute("lossAdd",      &m_LossAdd);
        mult->QueryIntAttribute("lossMultiply", &m_LossMultiply);
        const char* defer = mult->Attribute("deferPoints");
        m_bDeferPoints = (defer && CompareWords(defer, "true"));
    }
}

// @ 0x0011cc6c
GameModifier* ScoreModifier::Clone() {
    ScoreModifier* c = new ScoreModifier();
    *c = *this;
    return c;
}

// ASM-verified: 2026-05-18 binary @ 0x0011cb58 (re-analyst)
int ScoreModifier::DeferPoints(int points) {
    static_cast<PowerUp*>(m_pDeferInfo)->AddDeferedPoints(points);
    m_ApplyCount += points;   // +0x38 in binary: apply/score accumulator
    return 0;
}
