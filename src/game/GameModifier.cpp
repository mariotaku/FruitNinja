#include "GameModifier.h"

#include <tinyxml2.h>
#include "game/GameWork.h"
#include "game/FruitSaveData.h"

// Binary @ 0x13fdc4 -- GameModifier::Update(float)
// Base dispatcher. Returns 0 = still alive, 1 = expired.
//   (1) Deferred-apply gate: while m_bApplied (+0x18) is set, waits until the saved
//       time-remaining drops to/below m_DeferStart, then fires OnDeferComplete
//       (vtable slot 5 @ 0x140890) and clears m_bApplied.
//   (2) Counts down m_Duration_remaining; expires when it crosses 0.
//   (3) Otherwise dispatches UpdateSpecific(dt) (vtable slot 4, PURE).
// The "current time" the gate compares against is
//   game_work.m_SaveData->m_TimeRemainingSave   ([g_Game+0x4c]+0x10c)
// i.e. the persisted countdown value, not a wall clock.
int GameModifier::Update(float dt) {
    if (m_bApplied) {
        // vcmpe s14(curTime), s15(m_DeferStart); bls -> apply when curTime <= m_DeferStart.
        // Fall-through (curTime > m_DeferStart, i.e. m_DeferStart < curTime) -> still waiting.
        float curTime = game_work.m_SaveData->m_TimeRemainingSave;
        if (m_DeferStart < curTime) {
            return 0;
        }
        OnDeferComplete();   // vtable slot 5 @ 0x140890
        m_bApplied = false;
    }
    if (m_Duration_remaining > 0.0f) {
        m_Duration_remaining -= dt;
        if (m_Duration_remaining <= 0.0f) {
            return 1;
        }
    }
    return UpdateSpecific(dt);
}

// GameModifier::ParseSpecific -- pure virtual base body (empty; called by
// SlashModifier::ParseSpecific via super). Binary slot 9 = __cxa_pure_virtual,
// but sub-classes that super-chain need a callable body.
void GameModifier::ParseSpecific(TiXmlElement* /*xml*/) {
    // Base: no-op. Subclasses may call this via super without ill effect.
}

// Binary @ 0x00133378 -- GameModifier::ApplyModifier(bool, float*) base body.
// PURE in binary vtable (slot 8 = __cxa_pure_virtual); however the base body
// exists as a non-virtual thunk that subclasses call directly
// (ScoreModifier::ApplyModifier, etc. chain via GameModifier::ApplyModifier).
// Body: set m_Duration_remaining = m_Duration.
void GameModifier::ApplyModifier(bool /*isPurchased*/, float* /*extra*/) {
    m_Duration_remaining = m_Duration;
}

// Binary @ 0x00117DA0 -- GameModifier::Parse(TiXmlElement*)
// Reads the two base XML attributes ("length" -> m_Duration, "waitUntilTime"
// -> m_DeferStart), arms the deferred-apply flag when waitUntilTime is set,
// then dispatches ParseSpecific() (vtable slot 9, PURE).
void GameModifier::Parse(TiXmlElement* xml) {
    // Binary unconditionally stores 1 into field_0x10 (+0x10) at entry.
    field_0x10 = 1.0f;

    // QueryFloatAttribute leaves the target untouched when the attribute is
    // absent, matching the binary's behaviour (the ctor-initialised defaults
    // survive). Names resolved from DAT @ 0x1b9f91 / 0x1ba28c.
    xml->QueryFloatAttribute("length", &m_Duration);            // +0x04
    xml->QueryFloatAttribute("waitUntilTime", &m_DeferStart);   // +0x14

    // Arm deferred apply only when a real wait threshold was supplied.
    // Binary: vcmpe s14(m_DeferStart), s15(-1.0); strb.gt -> set m_bApplied(+0x18)=1 when > -1.0.
    if (m_DeferStart > -1.0f) {
        m_bApplied = true;
    }

    ParseSpecific(xml);
}

// Binary @ 0x001179AC -- GameModifier::Reset()
// Clears the per-frame countdown then dispatches ResetSpecific() (vtable
// slot 2, PURE). The constant loaded is 0.0f (verified from literal pool),
// NOT the -1.0f sentinel used elsewhere.
void GameModifier::Reset() {
    m_Duration_remaining = 0.0f;   // +0x0c = DAT_001179c0 (0.0f)
    ResetSpecific();
}
