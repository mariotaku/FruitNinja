// Analysed: 2026-04-30T00:00

#include "ScoreModifier.h"
#include "PowerUpManager.h"

ScoreModifier::ScoreModifier()
    : GameModifier()
    , m_GainAdd(0)
    , m_GainMultiply(1)
    , m_LossAdd(0)
    , m_LossMultiply(1)
    , m_ApplyCount(0)
    , m_bDeferPoints(false)
    , _pad35{0, 0, 0}
    , field_0x38(0)
{}

// @ 0x0011cb44
void ScoreModifier::ResetSpecific() {
    m_LossAdd      = 0;
    m_LossMultiply = 1;
    m_GainAdd      = 0;
    m_GainMultiply = 1;
}

// @ 0x0011cb70 — per-frame multiply into PowerUpManager score slots
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

// @ 0x0011cbe8
void ScoreModifier::ApplyModifier(bool isPurchased, float* extra) {
    GameModifier::ApplyModifier(isPurchased, extra);
    if (m_bDeferPoints) {
        // TODO: m_pOwner->AddDeferedPoints(0);
        // TODO: SetScoreDelegate(Delegate1<int,int>::Callee<ScoreModifier>(this, &ScoreModifier::operator()));
    }
    ++m_ApplyCount;
}

// @ 0x0011cd44
void ScoreModifier::RemoveModifier() {
    if (m_bDeferPoints) {
        // TODO: SetScoreDelegate(DefaultScoreDelegate global delegate);
    }
}

// @ 0x0011ccb0
void ScoreModifier::ParseSpecific(TiXmlElement* /*xml*/) {
    // TODO: parse gainAdd/gainMultiply/lossAdd/lossMultiply/deferPoints from XML
}

// @ 0x0011cc6c
GameModifier* ScoreModifier::Clone() {
    ScoreModifier* c = new ScoreModifier();
    *c = *this;
    return c;
}
