#include "GameModifier.h"

#include "game/GameWork.h"
#include "game/FruitSaveData.h"
#include "game/PowerUp.h"
#include "game/PowerUpManager.h"
#include "util/StringHash.h"

// Binary @ 0x13fdc4 -- GameModifier::Update(float)
// Base dispatcher. Returns 0 = still alive, 1 = expired.
//   (1) Deferred-apply gate: while m_bApplied (+0x18) is set, waits until the saved
//       time-remaining drops to/below m_DeferTime, then fires OnDeferComplete
//       (vtable slot 5 @ 0x140890) and clears m_bApplied.
//   (2) Counts down m_BonusAccum; expires when it crosses 0.
//   (3) Otherwise dispatches UpdateSpecific(dt) (vtable slot 4, PURE).
// The "current time" the gate compares against is
//   game_work.m_SaveData->m_TimeRemainingSave   ([g_Game+0x4c]+0x10c)
// i.e. the persisted countdown value, not a wall clock.
int GameModifier::Update(float dt) {
    if (m_bApplied) {
        // vcmpe s14(curTime), s15(m_DeferTime); bls -> apply when curTime <= m_DeferTime.
        // Fall-through (curTime > m_DeferTime, i.e. m_DeferTime < curTime) -> still waiting.
        float curTime = game_work.m_SaveData->m_TimeRemainingSave;
        if (m_DeferTime < curTime) {
            return 0;
        }
        OnDeferComplete(false, nullptr);   // vtable slot 5 @ 0x140890
        m_bApplied = false;
    }
    if (m_BonusAccum > 0.0f) {
        m_BonusAccum -= dt;
        if (m_BonusAccum <= 0.0f) {
            return 1;
        }
    }
    return UpdateSpecific(dt);
}

// Binary @ 0x140890 -- GameModifier::OnDeferComplete(bool, float*)
// Slot 5. Folds m_Duration (+0x04) into m_BonusAccum; clamps by two cached
// power-up name-hash lookups via PowerUpManager::GetActiveSingle; scales by
// PowerUpManager::m_WaveDtModPrev (field_0x74). DAT consts: 0.01f, 0.0f,
// 50.0f (aa0), 0.333f (aa4), 0.1f (aa8).
// ASM-verified: 2026-06-13T18:00Z v1.6.1 binary @ 0x00140890 (asm-inspector)
void GameModifier::OnDeferComplete(bool /*unused*/, float* pExtra) {
    // 1) fold m_Duration (+0x04) into m_BonusAccum -- binary vldr.32 s15,[r0,#4]
    float acc = m_Duration;
    if (m_BonusAccum > 0.0f) acc += m_BonusAccum;
    m_BonusAccum = acc;

    // 2) clamp by pExtra if deferred
    if (pExtra && m_bDeferred) {
        float lo = (m_Duration > 0.0f) ? 0.01f : 0.0f;
        if (*pExtra < lo) *pExtra = lo;
        m_BonusAccum = *pExtra;
    }

    // 3) two cached StringHash powerup-name ids (guarded statics)
    // ASM-spec v1.6.1 GameModifier::ApplyModifier @0x00140890: the powers are
    // "overtime" (+5s) and "freeze" (+50s) -- NOT "doubleTime"/"frenzy". The wrong
    // literals queried non-existent powers, so a deferred mod (x2 uses deferPoints)
    // got the wrong remaining-time clamp when a freeze banana overlapped.
    static uint32_t s_hashA = 0;
    static bool     s_initA = false;
    if (!s_initA) { s_hashA = StringHash("overtime"); s_initA = true; }

    static uint32_t s_hashB = 0;
    static bool     s_initB = false;
    if (!s_initB) { s_hashB = StringHash("freeze");   s_initB = true; }

    float baseTime = game_work.m_SaveData ? game_work.m_SaveData->m_TimeRemainingSave : 0.0f;
    float bonusA = 0.0f;
    if (PowerUpManager::GetInstance()->GetActiveSingle(s_hashA)) bonusA = 5.0f;

    float bonusB = 0.0f;
    if (m_pDeferInfo == nullptr ||
        static_cast<const PowerUp*>(m_pDeferInfo)->m_NameHash != s_hashB) {
        if (PowerUpManager::GetInstance()->GetActiveSingle(s_hashB)) bonusB = 50.0f;
    } else {
        bonusB = 50.0f;
    }

    float target = baseTime + bonusA + bonusB;
    float mult   = PowerUpManager::GetInstance()->m_WaveDtModPrev;
    if (mult > 0.0f && target < m_BonusAccum / mult) {
        float v = target * mult - 0.333f;
        if (v < 0.1f) v = 0.1f;
        m_BonusAccum = v;
    }
}

// GameModifier::ParseSpecific -- pure virtual base body (empty; called by
// SlashModifier::ParseSpecific via super). Binary slot 9 = __cxa_pure_virtual,
// but sub-classes that super-chain need a callable body.
void GameModifier::ParseSpecific(TiXmlElement* /*xml*/) {
    // Base: no-op. Subclasses may call this via super without ill effect.
}

// Binary @ 0x00140890 (GOT thunk 0x00114f04) -- GameModifier::ApplyModifier(bool, float*)
// base body. PURE in binary vtable (slot 8 = __cxa_pure_virtual); the base body
// exists as a real function that all 7 subclass ApplyModifier overrides call
// directly as a non-virtual "super()" (confirmed xrefs into the 0x00114f04 GOT
// thunk from ExplodyFruitModifier/ScoreModifier/SlashModifier/TimeSinkModifier/
// ComboModifier/SpawnModifier/WaveModifier::ApplyModifier).
//
// ASM-spec v1.6.1 GameModifier::ApplyModifier @0x00140890: re-confirmed via
// decompile -- Ghidra's own demangled name for the function AT this address is
// "GameModifier::ApplyModifier", and vtable slot 5 (0x2cc6f4, read from the
// live vtable @ 0x2cc6d8) ALSO stores this exact address. The compiler folded
// GameModifier::ApplyModifier (base body, called non-virtually by every
// subclass override's super() chain) and GameModifier::OnDeferComplete (slot 5,
// virtual dispatch from Update) into ONE compiled function -- both take the
// same (bool, float*) signature and perform the identical fold-clamp-scale
// body (ICF). The two remain distinct C++ methods in the port (OnDeferComplete
// is still reached polymorphically via vtable slot 5 from Update); ApplyModifier
// delegates to it so both call sites run byte-identical logic, matching the
// binary's single merged function.
// The delegation is QUALIFIED on purpose: subclass overrides reach this base body
// through a direct `bl 0x00140890` (GOT thunk 0x00114f04), a non-virtual super()
// call -- the base body never re-enters the vtable. Leaving it unqualified makes
// any subclass that overrides BOTH ApplyModifier and OnDeferComplete recurse
// forever (base -> virtual OnDeferComplete -> subclass -> base ...); that is
// exactly what deadlocked the freeze/frenzy powerups, which drive a WaveModifier.
// The genuinely polymorphic slot-5 dispatch lives in Update(), not here.
void GameModifier::ApplyModifier(bool isPurchased, float* extra) {
    GameModifier::OnDeferComplete(isPurchased, extra);
}

// Binary @ 0x00117DA0 -- GameModifier::Parse(TiXmlElement*)
// Reads the two base XML attributes ("length" -> m_Duration, "waitUntilTime"
// -> m_DeferTime), arms the deferred-apply flag when waitUntilTime is set,
// then dispatches ParseSpecific() (vtable slot 9, PURE).
void GameModifier::Parse(TiXmlElement* xml) {
    // Binary unconditionally sets m_bConfigured (+0x10) = 1 at entry.
    m_bConfigured = 1;

    // QueryFloatAttribute leaves the target untouched when the attribute is
    // absent, matching the binary's behaviour (the ctor-initialised defaults
    // survive). Names resolved from DAT @ 0x1b9f91 / 0x1ba28c.
    xml->QueryFloatAttribute("length", &m_Duration);          // +0x04
    xml->QueryFloatAttribute("waitUntilTime", &m_DeferTime);  // +0x14

    // Arm deferred apply only when a real wait threshold was supplied.
    // Binary: vcmpe s14(m_DeferTime), s15(-1.0); strb.gt -> set m_bApplied(+0x18)=1 when > -1.0.
    if (m_DeferTime > -1.0f) {
        m_bApplied = true;
    }

    ParseSpecific(xml);
}

// Binary @ 0x001179AC -- GameModifier::Reset()
// Clears the bonus accumulator then dispatches ResetSpecific() (vtable
// slot 2, PURE). The constant loaded is 0.0f (verified from literal pool),
// NOT the -1.0f sentinel used elsewhere.
void GameModifier::Reset() {
    m_BonusAccum = 0.0f;   // +0x0c = DAT_001179c0 (0.0f)
    ResetSpecific();
}
