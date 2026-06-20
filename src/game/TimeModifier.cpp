// Analysed: 2026-05-03T00:00

#include "TimeModifier.h"
#include "PowerUpManager.h"
#include "Game.h"
#include "hud/TimeControl.h"
#include "ItemParseUtil.h"
#include <tinyxml2.h>
#include "game/GameWork.h"

// ASM-verified: 2026-06-20T00:00Z v1.6.1 TimeModifier::TimeModifier @ 0x143808 (asm-inspector)
TimeModifier::TimeModifier()
    : GameModifier()
    , m_DtScale(1.0f)
    , m_TransitionRate(0.0f)
    , m_CurrentDtMod(1.0f)
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
            Game* game = Game::GetInstance();
            // TODO: route through HUD::GetTimeControl() once exposed
            if (game && game_work.mCountDown)
                game_work.mCountDown->AddTime(m_AddTime);   // binary @ 0x001204f0
            return 1;
        }
    }

    // (2) Stop-clock contribution.
    if (m_bStopClock)
        PowerUpManager::GetInstance()->StopClock(m_BonusAccum);

    // (3) Slow-clock contribution.
    if (m_TimeSlow != 1.0f)
        PowerUpManager::GetInstance()->SlowClock(m_TimeSlow);

    // (4) Lerp m_CurrentDtMod toward m_DtScale at m_TransitionRate.
    if (m_TransitionRate <= 0.0f) {
        m_CurrentDtMod = m_DtScale;
    } else {
        if (m_TransitionRate < m_BonusAccum || m_Duration <= 0.0f) {
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
            float t = m_BonusAccum / m_TransitionRate;
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

// ASM-verified: 2026-05-03 binary @ 0x001200fc..0x00120188 (asm-inspector)
// @ 0x001200fc
void TimeModifier::ParseSpecific(TiXmlElement* xml) {
    const char* stopAttr = xml->Attribute("stopClock");
    m_bStopClock = (stopAttr && CompareWords(stopAttr, "true"));

    xml->QueryFloatAttribute("slowClock", &m_TimeSlow);
    xml->QueryFloatAttribute("addClock",  &m_AddTime);
    m_AddTimeDelay = 0;
    if (m_AddTime != 0.0f) m_AddTimeDelay = 1;

    // Defaults written AFTER queries (binary order):
    m_TransitionRate = 0.0f;   // binary @ 0x0012015a, vstr +0x24
    m_DtScale        = 1.0f;   // binary @ 0x0012015e, vstr +0x20
    m_CurrentDtMod   = 1.0f;   // binary @ 0x00120162, vstr +0x28  -- previously missing

    TiXmlElement dt = xml->FirstChildElement("dt_speed");
    if (dt) {
        dt.QueryFloatAttribute("transitionTime", &m_TransitionRate);
        dt.QueryFloatAttribute("dt",             &m_DtScale);
    }
}

GameModifier* TimeModifier::Clone() {
    TimeModifier* c = new TimeModifier();
    *c = *this;
    return c;
}
