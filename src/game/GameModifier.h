#ifndef FN_GAME_MODIFIER_H
#define FN_GAME_MODIFIER_H

// Analysed: 2026-04-30T00:00
//
// GameModifier — abstract base for ScoreModifier / TimeModifier /
// SlashModifier / WaveModifier. Binary size 0x20 (32 bytes).
//
// vtable @ 0x001e8cc0. Stored pointer is (real_vt + 8) so virtual slot 0
// is at stored offset 0.
//
// vtable layout:
//   [0] ~Modifier (regular dtor)
//   [1] ~Modifier (deleting dtor)
//   [2] ResetSpecific()
//   [3] Update(dt)       — base dispatcher
//   [4] UpdateSpecific(dt)
//   [5] ApplyModifier(bool isPurchased, float* extra)
//   [6] RemoveModifier()
//   [7] GetType() -> 0=Time,1=Wave,2=Score,3=Slash
//   [8] ParseSpecific(TiXmlElement*)
//   [9] Clone()
//
// Binary addresses:
//   ctor            0x0011a160
//   dtor            0x0011a19c
//   RemoveModifier  0x0011a1b8 (no-op base)
//   ApplyModifier   0x00118178
//   Update          0x001179c4 (base dispatcher)

#include <cstdint>

namespace tinyxml2 { class XMLElement; }
typedef tinyxml2::XMLElement TiXmlElement;

class PowerUp;

class GameModifier {
public:
    // +0x00: vtable (implicit)

    // +0x04: XML duration (initial timer)
    float m_Duration;

    // +0x08: pad
    uint32_t field_0x08;

    // +0x0c: duration remaining; decremented each frame; expiry when <= 0
    float m_Duration_remaining;

    // +0x10: deferred-activation flag (if true, wait until TimeControl passes m_DeferStart)
    bool m_bDeferred;
    uint8_t _pad11[3];

    // +0x14: deferred-start timer threshold (-1.0f = no deferral)
    float m_DeferStart;

    // +0x18: true after first ApplyModifier; gates double-application
    bool m_bApplied;
    uint8_t _pad19[3];

    // +0x1c: back-pointer to parent PowerUp
    PowerUp* m_pOwner;

    GameModifier()
        : m_Duration(0.0f)
        , field_0x08(0)
        , m_Duration_remaining(0.0f)
        , m_bDeferred(false)
        , _pad11{0, 0, 0}
        , m_DeferStart(-1.0f)
        , m_bApplied(false)
        , _pad19{0, 0, 0}
        , m_pOwner(nullptr)
    {}

    virtual ~GameModifier() {}

    // [2] ResetSpecific — clears per-modifier state; base is no-op
    virtual void ResetSpecific() {}

    // [3] Update(dt) @ 0x001179c4 — base dispatcher (returns 0=alive, 1=expired)
    virtual int Update(float dt) {
        if (m_bApplied) {
            // TODO: if (m_DeferStart < TimeControl_GetCurrentTime()) wait
            ApplyModifier(false, nullptr);
            m_bApplied = false;
        }
        if (m_Duration_remaining > 0.0f) {
            m_Duration_remaining -= dt;
            if (m_Duration_remaining <= 0.0f) return 1;
        }
        return UpdateSpecific(dt);
    }

    // [4] UpdateSpecific(dt) — per-frame override; base no-op returns 0
    virtual int UpdateSpecific(float /*dt*/) { return 0; }

    // [5] ApplyModifier @ 0x00118178 — base writes m_Duration_remaining = m_Duration
    virtual void ApplyModifier(bool /*isPurchased*/, float* /*extra*/) {
        m_Duration_remaining = m_Duration;
    }

    // [6] RemoveModifier @ 0x0011a1b8 — base no-op
    virtual void RemoveModifier() {}

    // [7] GetType — 0=Time, 1=Wave, 2=Score, 3=Slash
    virtual int GetType() { return -1; }

    // [8] ParseSpecific
    virtual void ParseSpecific(TiXmlElement* /*xml*/) {}

    // [9] Clone — heap-alloc new instance
    virtual GameModifier* Clone() { return nullptr; }

    // ---- AUTO-STUB MERGE: STUB -- gen_stubs.py ----
    // STUB: GameModifier::Parse -- auto stub from binary missing-symbol set
    void Parse(TiXmlElement*);
    // STUB: GameModifier::Reset -- auto stub from binary missing-symbol set
    void Reset();
    // ---- end AUTO-STUB MERGE ----
};

#endif // FN_GAME_MODIFIER_H
