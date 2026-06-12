// TimeSinkModifier — v1.6.1 time-from-score modifier.
// Binary ctor @ 0x0014d9e8, ParseSpecific @ 0x0014dbf4,
// ApplyModifier @ 0x0014dc88, GetType @ 0x0014e1b8.

#include "TimeSinkModifier.h"
#include "GameWork.h"
#include "entities/Fruit.h"
#include "hud/TimeControl.h"
#include "engine/util/Delegate.h"
#include <tinyxml2.h>

TimeSinkModifier::TimeSinkModifier()
    : GameModifier()
    , m_Multiplier(0.0f)    // binary ctor @ 0x0014d9e8: +0x20 = 0.0f
    , m_Accumulator(-1.0f)  // binary ctor @ 0x0014da30: +0x24 = -1.0f (0xbf800000)
{}

TimeSinkModifier::~TimeSinkModifier() {}

void TimeSinkModifier::ResetSpecific() {
    Fruit::FruitWasSlicedEvent() -=
        Mortar::Delegate3<void, Fruit*, int, Mortar::Entity*>::Make(
            this, &TimeSinkModifier::FruitWasSlicedSink);
}

int TimeSinkModifier::UpdateSpecific(float /*dt*/) { return 0; }

// @ 0x0014dc88
// Binary: if not deferred and m_Accumulator >= 0, register ScoreNotification
// as Delegate2<void,int,int> on the score signal; also register FruitWasSlicedSink
// on g_FruitWasSliced (Fruit.cpp file-static, GOT 0x332a34).
// TODO: 0x0014dc88 — register ScoreNotification on score signal (score signal not yet ported).
void TimeSinkModifier::ApplyModifier(bool isPurchased, float* extra) {
    GameModifier::ApplyModifier(isPurchased, extra);
    if (!m_bDeferred && m_Accumulator >= 0.0f) {
        Fruit::FruitWasSlicedEvent() +=
            Mortar::Delegate3<void, Fruit*, int, Mortar::Entity*>::Make(
                this, &TimeSinkModifier::FruitWasSlicedSink);
    }
    // TODO: 0x0014dc88 — register ScoreNotification as Delegate2<void,int,int> on score signal.
}

// @ 0x0014dbf4
// Binary reads 'value' -> m_Multiplier (+0x20).
// If 'immediate' attr PRESENT -> m_Accumulator (+0x24) = 0.0f.
// If 'immediate' attr ABSENT  -> m_Accumulator (+0x24) = -1.0f.
void TimeSinkModifier::ParseSpecific(TiXmlElement* xml) {
    if (!xml) return;
    xml->QueryFloatAttribute("value", &m_Multiplier);
    const char* imm = xml->Attribute("immediate");
    if (imm) {
        m_Accumulator = 0.0f;
    } else {
        m_Accumulator = -1.0f;
    }
}

// @ 0x0014dac4
// Binary: v = (float)points;
//   if m_Accumulator < 0 -> m_Accumulator += v * m_Multiplier
//   else                 -> TimeControl::AddTime(v * m_Multiplier)
// The TimeControl instance is fetched from game_work+0x180 (mCountDown).
// TODO: 0x0014dac4 — wire to score notification signal via Delegate2
void TimeSinkModifier::ScoreNotification(int points, int /*extra*/) {
    float v = (float)points;
    if (m_Accumulator < 0.0f) {
        m_Accumulator += v * m_Multiplier;
    } else {
        if (game_work.mCountDown) {
            game_work.mCountDown->AddTime(v * m_Multiplier);
        }
    }
}

// @ 0x0014da7c — similar accumulate/immediate path for per-fruit-slice.
// Subscribed to g_FruitWasSliced (Fruit.cpp file-static, GOT 0x332a34).
void TimeSinkModifier::FruitWasSlicedSink(Fruit* /*fruit*/, int score, Mortar::Entity* /*slasher*/) {
    float v = (float)score;
    if (m_Accumulator < 0.0f) {
        m_Accumulator += v * m_Multiplier;
    } else {
        if (game_work.mCountDown) {
            game_work.mCountDown->AddTime(v * m_Multiplier);
        }
    }
}

GameModifier* TimeSinkModifier::Clone() {
    TimeSinkModifier* c = new TimeSinkModifier();
    c->m_Duration    = m_Duration;
    c->field_0x08    = field_0x08;
    c->m_BonusAccum  = m_BonusAccum;
    c->m_bDeferred   = m_bDeferred;
    c->m_DeferTime   = m_DeferTime;
    c->m_bApplied    = m_bApplied;
    c->m_pDeferInfo  = m_pDeferInfo;
    c->m_Multiplier  = m_Multiplier;
    c->m_Accumulator = m_Accumulator;
    return c;
}
