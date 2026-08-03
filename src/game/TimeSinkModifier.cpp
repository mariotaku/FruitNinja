// TimeSinkModifier — v1.6.1 time-from-score modifier.
// Binary ctor @ 0x0014d9e8, ParseSpecific @ 0x0014dbf4,
// ApplyModifier @ 0x0014dc88, GetType @ 0x0014e1b8.

#include "TimeSinkModifier.h"
#include "GameWork.h"
#include "entities/Fruit.h"
#include "hud/TimeControl.h"
#include "engine/util/Delegate.h"
#include "engine/core/SystemManager.h"

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
// Binary: if m_BonusAccum(+0x0c)<=0 && m_Accumulator(+0x24)>=0 (not already
// active, and not in "wait-for-immediate-AddTime" mode), register
// ScoreNotification as Delegate2<void,int,int> on GetScoreNotification()
// (Event2<int,int> @ 0x00104390) BEFORE chaining base ApplyModifier (which
// sets m_BonusAccum = m_Duration and would make the gate false if checked
// after). Register-under-gate first, base last.
//
// FruitWasSlicedSink is a per-fruit-slice variant subscribed nowhere in this
// function per the binary; ApplyModifier only ever wires ScoreNotification.
void TimeSinkModifier::ApplyModifier(bool isPurchased, float* extra) {
    if (m_BonusAccum <= 0.0f && m_Accumulator >= 0.0f) {
        GetScoreNotification() += Mortar::Delegate2<void,int,int>::Make(this, &TimeSinkModifier::ScoreNotification);
    }
    GameModifier::ApplyModifier(isPurchased, extra);
}

// @ 0x0014db60 — unsubscribe ScoreNotification from the score signal
// (mirrors ApplyModifier's +=; no gate on the -= side in the binary).
void TimeSinkModifier::RemoveModifier() {
    GetScoreNotification() -= Mortar::Delegate2<void,int,int>::Make(this, &TimeSinkModifier::ScoreNotification);
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
// Binary (vcmpe.f32 s13,#0 on m_Accumulator at +0x24):
//   GE branch (m_Accumulator >= 0):  m_Accumulator += (float)points * m_Multiplier;  return;
//   fall-through (m_Accumulator < 0): TimeControl::AddTime((float)points * m_Multiplier)
//       on g_GameData TimeControl slot (indirect GOT 0x2d92a0 + 0x184 == game_work.mCountDown +0x180).
void TimeSinkModifier::ScoreNotification(int points, int /*extra*/) {
    float v = (float)points;
    // v1.6.1 TimeSinkModifier::ScoreNotification @0x0014dac4: the only gate is the
    // VFP GE from `vcmpe.f32 s13,#0`. The negative arm is
    // `ldr r0,[r3,#0x184]; b TimeControl::AddTime` at 0x0014dafc -- an unconditional
    // tail-call with no null test on mCountDown.
    if (m_Accumulator >= 0.0f) {
        m_Accumulator += v * m_Multiplier;
    } else {
        game_work.mCountDown->AddTime(v * m_Multiplier);
    }
}

// @ 0x0014da7c — per-fruit-slice variant of ScoreNotification (byte-identical body).
// Subscribed to g_FruitWasSliced (Fruit.cpp file-static, GOT 0x332a34).
// Binary (vcmpe.f32 s13,#0 -> vmlage/vstrge/bxge): when m_Accumulator >= 0 it
// ACCUMULATES (m_Accumulator += score*mult) and returns; when m_Accumulator < 0
// it tail-calls TimeControl::AddTime(score*mult, game_work+0x184). arg3 (Entity*
// slasher) is never referenced in the binary body — only this (r0) and score (r2).
void TimeSinkModifier::FruitWasSlicedSink(Fruit* /*fruit*/, int score, Mortar::Entity* /*slasher*/) {
    float v = (float)score;
    // v1.6.1 TimeSinkModifier::FruitWasSlicedSink @0x0014da7c: body is byte-identical
    // to ScoreNotification apart from the score register. The negative arm tail-calls
    // TimeControl::AddTime on game_work+0x184 with no null test.
    if (m_Accumulator < 0.0f) {
        game_work.mCountDown->AddTime(v * m_Multiplier);
    } else {
        m_Accumulator += v * m_Multiplier;
    }
}

GameModifier* TimeSinkModifier::Clone() {
    TimeSinkModifier* c = new TimeSinkModifier();
    c->m_Duration    = m_Duration;
    c->m_reserved08  = m_reserved08;
    c->m_BonusAccum  = m_BonusAccum;
    c->m_bDeferred   = m_bDeferred;
    c->m_DeferTime   = m_DeferTime;
    c->m_bApplied    = m_bApplied;
    c->m_pDeferInfo  = m_pDeferInfo;
    c->m_Multiplier  = m_Multiplier;
    c->m_Accumulator = m_Accumulator;
    return c;
}
