#ifndef FN_GAME_MODIFIER_H
#define FN_GAME_MODIFIER_H

//
// GameModifier — abstract base for ScoreModifier / TimeModifier /
// SlashModifier / WaveModifier / ComboModifier / TimeSinkModifier /
// ExplodyFruitModifier / SpawnModifier. Binary size 0x20 (32 bytes).
//
// v1.6.1: ctor @ 0x00133378; D1 @ 0x00143760; D0 @ 0x00144ac4.
// vtable @ 0x2cc6d8 (stored vptr = 0x2cc6e0 = ZTV+8). 14 slots total.
//
// vtable layout (v1.6.1):
//   [0]  ~GameModifier (D1, non-deleting)    0x143760
//   [1]  ~GameModifier (D0, deleting)        0x144ac4
//   [2]  ResetSpecific()          PURE        0x360434
//   [3]  Update(float dt)                    0x13fdc4  (base dispatcher)
//   [4]  UpdateSpecific(float dt) PURE        0x360434
//   [5]  ApplyModifier/defer-complete         0x140890  (on-defer-fire hook)
//   [6]  RemoveModifier()                    0x143784  (base no-op)
//   [7]  GetType() -> uint32_t               0x143788  (base returns 0xffffffff)
//   [8]  ApplyModifier(bool,float*) PURE      0x360434
//   [9]  ParseSpecific(xml)        PURE        0x360434
//   [10] (subclass hook)                     0x3602bc  (thunk)
//   [11] (subclass hook)                     0x2811d8  (thunk)
//   [12] (subclass hook)                     0x3602bc  (thunk)
//   [13] (subclass hook)                     0x2811e4  (thunk)
//
// DIFFERS: port uses -1 (signed int) for GetType base; binary returns 0xffffffff
// (same bit pattern as unsigned). No functional difference at call sites.

#include <cstdint>

namespace tinyxml2 { class XMLElement; }
typedef tinyxml2::XMLElement TiXmlElement;

class PowerUp;

class GameModifier {
public:
    // +0x00: vtable (implicit)

    // +0x04: XML duration (initial timer)
    float m_Duration;

    // +0x08: unidentified float (ctor writes 0.0f)
    float field_0x08;

    // +0x0c: duration remaining; decremented each frame; expiry when <= 0
    float m_Duration_remaining;

    // +0x10: unidentified float (ctor writes 0.0f; Parse sets to 1 unconditionally
    // — binary field_0x10 is separate from the bool pair at +0x18/+0x19)
    // DIFFERS: binary +0x10 purpose unresolved; ctor=0, Parse=1 (float or int).
    float field_0x10;

    // +0x14: deferred-start timer threshold (-1.0f = no deferral)
    float m_DeferStart;

    // +0x18: gate: 1 while deferred-apply is pending; cleared after ApplyModifier fires
    // (struct+0x18, read by Update @ 0x13fdc4 as the deferred-pending gate)
    bool m_bApplied;

    // +0x19: deferred flag (ctor = 1; adjacent to m_bApplied per binary bool pair)
    // DIFFERS: binary +0x18/+0x19 are adjacent bools {m_bApplied, m_bDeferred};
    // port maps the pair as two separate bytes to match the ctor writes
    // (p_pad[0x14]=0, p_pad[0x15]=1 in ctor @ 0x133378).
    bool m_bDeferred;
    uint8_t _pad1a[2];

    // +0x1c: back-pointer to parent PowerUp
    PowerUp* m_pOwner;

    GameModifier()
        : m_Duration(0.0f)
        , field_0x08(0.0f)
        , m_Duration_remaining(0.0f)
        , field_0x10(0.0f)
        , m_DeferStart(-1.0f)
        , m_bApplied(false)
        , m_bDeferred(true)
        , _pad1a{0, 0}
        , m_pOwner(nullptr)
    {}

    virtual ~GameModifier() {}

    // [2] ResetSpecific — clears per-modifier state; PURE in binary (0x360434)
    virtual void ResetSpecific() = 0;

    // [3] Update(float dt) @ 0x13fdc4 — base dispatcher (returns 0=alive, 1=expired).
    virtual int Update(float dt);

    // [4] UpdateSpecific(float dt) — PURE in binary (0x360434)
    virtual int UpdateSpecific(float dt) = 0;

    // [5] ApplyModifier/defer-complete @ 0x140890 — called by Update when defer fires;
    // accumulates m_Value(+0x08), clamps via PowerUpManager multipliers.
    // Base body is the full "apply the score/time bonus" implementation.
    // TODO: 0x00140890 — full defer-complete body not yet ported (score/time accumulation)
    virtual void OnDeferComplete() {}

    // [6] RemoveModifier @ 0x143784 — base no-op
    virtual void RemoveModifier() {}

    // [7] GetType — base returns -1 (bit pattern 0xffffffff matches binary 0x143788);
    // DIFFERS: binary returns unsigned 0xffffffff; port uses int -1 (same bit pattern)
    // to allow subclass int overrides (Score=2, Time=0, Wave=1, Slash=3, etc.)
    virtual int GetType() { return -1; }

    // [8] ApplyModifier(bool,float*) — PURE in binary (0x360434); base body
    // writes m_Duration_remaining = m_Duration (subclasses call this via super).
    virtual void ApplyModifier(bool isPurchased, float* extra) = 0;

    // [9] ParseSpecific — PURE in binary (0x360434)
    virtual void ParseSpecific(TiXmlElement* xml) = 0;

    // [10] Clone() — binary slot 10 @ 0x3602bc (thunk; spec notes "likely Clone()/extra pure").
    // Heap-alloc new instance; subclasses return a concrete copy.
    virtual GameModifier* Clone() { return nullptr; }

    // [11-13] subclass hook slots (binary thunks: 0x2811d8, 0x3602bc, 0x2811e4)
    // TODO: 0x2811d8 — slot 11 purpose unresolved (subclass hook)
    // TODO: 0x3602bc — slot 12 purpose unresolved (thunk)
    // TODO: 0x2811e4 — slot 13 purpose unresolved (subclass hook)
    virtual void Slot11() {}
    virtual void Slot12() {}
    virtual void Slot13() {}

    // Binary @ 0x00117DA0 — reads base XML attributes ("length", "waitUntilTime")
    // then dispatches ParseSpecific(xml).
    void Parse(TiXmlElement*);
    // Binary @ 0x001179AC — clears m_Duration_remaining then dispatches ResetSpecific().
    void Reset();
};

#endif // FN_GAME_MODIFIER_H
