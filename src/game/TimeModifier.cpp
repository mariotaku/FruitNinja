// Analysed: 2026-04-30T00:00

#include "TimeModifier.h"
#include "PowerUpManager.h"

TimeModifier::TimeModifier()
    : GameModifier()
    , m_DtScale(1.0f)
    , m_TransitionRate(0.0f)
    , m_CurrentDtMod(0.0f)
    , m_bStopClock(false)
    , _pad2d{0, 0, 0}
    , m_TimeSlow(1.0f)
    , m_AddTime(0.0f)
    , m_AddTimeDelay(0)
{}

// @ 0x0011ff4c
void TimeModifier::ResetSpecific() {
    m_CurrentDtMod = 0.0f;
}

// @ 0x0011ffbc
int TimeModifier::UpdateSpecific(float dt) {
    // (1) Frame-delayed AddTime fires once when counter hits 0.
    if (m_AddTimeDelay > 0) {
        if (--m_AddTimeDelay == 0) {
            // TODO: TimeControl::AddTime(m_AddTime);
            return 1;
        }
    }

    // (2) Stop-clock contribution.
    if (m_bStopClock)
        PowerUpManager::GetInstance()->StopClock(m_Duration_remaining);

    // (3) Slow-clock contribution.
    if (m_TimeSlow != 1.0f)
        PowerUpManager::GetInstance()->SlowClock(m_TimeSlow);

    // (4) Lerp m_CurrentDtMod toward m_DtScale at m_TransitionRate.
    if (m_TransitionRate <= 0.0f) {
        m_CurrentDtMod = m_DtScale;
    } else {
        if (m_TransitionRate < m_Duration_remaining || m_Duration <= 0.0f) {
            // ASM-verified: 2026-05-02 binary @ 0x12003a -- STEADY-STATE: lots of time remaining
            float target = m_DtScale;
            if (target >= m_CurrentDtMod) {
                m_CurrentDtMod += dt / m_TransitionRate;
                if (m_CurrentDtMod > target) m_CurrentDtMod = target;
            } else {
                m_CurrentDtMod -= dt / m_TransitionRate;
                if (m_CurrentDtMod < target) m_CurrentDtMod = target;
            }
        } else {
            // FADE-OUT: about to expire
            float t = m_Duration_remaining / m_TransitionRate;
            float v = (m_DtScale - 1.0f) * t + 1.0f;
            if (m_DtScale > 1.0f) {
                if (m_CurrentDtMod < v) v = m_CurrentDtMod;  // clamp from above
            } else {
                if (v < m_CurrentDtMod) v = m_CurrentDtMod;  // clamp from below
            }
            m_CurrentDtMod = v;
        }
    }

    // (5) Apply this frame's value to the composite dt-mod.
    PowerUpManager::GetInstance()->ApplyDtMod(m_CurrentDtMod);
    return 0;
}

// @ 0x001200fc
void TimeModifier::ParseSpecific(TiXmlElement* /*xml*/) {
    // TODO: parse duration/stop/slow/addTime/<scale rate="" amount=""/> from XML
}

GameModifier* TimeModifier::Clone() {
    TimeModifier* c = new TimeModifier();
    *c = *this;
    return c;
}
