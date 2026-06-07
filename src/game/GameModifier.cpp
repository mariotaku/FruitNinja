#include "GameModifier.h"

#include <tinyxml2.h>
#include "game/GameWork.h"
#include "game/FruitSaveData.h"

// Binary @ 0x001179c4 -- GameModifier::Update(float)
// Base dispatcher. Returns 0 = still alive, 1 = expired.
//   (1) Deferred-apply gate: while armed (m_bApplied) it waits until the saved
//       time-remaining drops to/below m_DeferStart, then fires ApplyModifier
//       once (vtable slot 5) and disarms.
//   (2) Counts down m_Duration_remaining; expires when it crosses 0.
//   (3) Otherwise dispatches UpdateSpecific(dt) (vtable slot 4).
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
        ApplyModifier(false, nullptr);
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

// Binary @ 0x00117DA0 -- GameModifier::Parse(TiXmlElement*)
// Reads the two base XML attributes ("length" -> m_Duration, "waitUntilTime"
// -> m_DeferStart), arms the deferred-apply flag when waitUntilTime is set,
// then dispatches ParseSpecific() (vtable slot 8).
void GameModifier::Parse(TiXmlElement* xml) {
    // field_0x10 (m_bDeferred) is unconditionally set to 1 here in the binary.
    m_bDeferred = true;

    // QueryFloatAttribute leaves the target untouched when the attribute is
    // absent, matching the binary's behaviour (the ctor-initialised defaults
    // survive). Names resolved from DAT @ 0x1b9f91 / 0x1ba28c.
    xml->QueryFloatAttribute("length", &m_Duration);            // field1_0x4
    xml->QueryFloatAttribute("waitUntilTime", &m_DeferStart);   // field11_0x14

    // Arm deferred apply only when a real wait threshold was supplied.
    // Binary: vcmpe s14(m_DeferStart), s15(-1.0); strb.gt -> set when > -1.0.
    if (m_DeferStart > -1.0f) {
        m_bApplied = true;
    }

    ParseSpecific(xml);
}

// Binary @ 0x001179AC -- GameModifier::Reset()
// Clears the per-frame countdown then dispatches ResetSpecific() (vtable
// slot 2). The constant loaded at 0x001179c0 is 0.0f (verified from the
// literal pool), NOT the -1.0f sentinel used elsewhere.
void GameModifier::Reset() {
    m_Duration_remaining = 0.0f;   // field6_0xc = DAT_001179c0 (0.0f)
    ResetSpecific();
}
