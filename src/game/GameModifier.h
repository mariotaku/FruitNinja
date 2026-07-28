#ifndef FN_GAME_MODIFIER_H
#define FN_GAME_MODIFIER_H

//
// GameModifier — abstract base for ScoreModifier / TimeModifier /
// SlashModifier / WaveModifier / ComboModifier / TimeSinkModifier /
// ExplodyFruitModifier / SpawnModifier. Binary size 0x20 (32 bytes).
//
// v1.6.1: ctor @ 0x00133378; D1 @ 0x00143760; D0 @ 0x00144ac4.
// vtable @ 0x2cc6d8 (stored vptr = 0x2cc6e0 = ZTV+8). EXACTLY 11 slots (0-10).
// Word after slot 10 is 0x2811d8 = RTTI typename "7PowerUp" — NOT a vtable entry.
// The Wave-1 "slots 11/12/13" were RTTI strings + a PLT stub, not vtable slots.
//
// vtable layout (v1.6.1):
//   [0]  ~GameModifier (D1, non-deleting)    0x143760
//   [1]  ~GameModifier (D0, deleting)        0x144ac4
//   [2]  ResetSpecific()          PURE        0x360434
//   [3]  Update(float dt)                    0x13fdc4  (base dispatcher)
//   [4]  UpdateSpecific(float dt) PURE        0x360434
//   [5]  OnDeferComplete()                   0x140890  (on-defer-fire hook)
//   [6]  RemoveModifier()                    0x143784  (base no-op)
//   [7]  GetType() -> uint32_t               0x143788  (base returns 0xffffffff)
//   [8]  ApplyModifier(bool,float*) PURE      0x360434
//   [9]  ParseSpecific(xml)        PURE        0x360434
//   [10] Clone()                  PURE        0x3602bc
//
// DIFFERS: port uses -1 (signed int) for GetType base; binary returns 0xffffffff
// (same bit pattern as unsigned). No functional difference at call sites.

#include <cstdint>

#include "engine/xml/TiXmlElement.h"

class PowerUp;

class GameModifier {
public:
    // +0x00: vtable (implicit)

    // +0x04: XML duration (initial timer)
    float m_Duration;

    // +0x08: scratch float, copied verbatim by every subclass Clone(). No write
    // site in the base ctor (disasm @ 0x00133378 only touches +0x4/+0xc/+0x10/
    // +0x14/+0x18/+0x19/+0x1c) and no read site in OnDeferComplete @ 0x00140890.
    // Semantic unresolved; preserved for Clone fidelity. Reserved.
    float m_reserved08;  // purpose unknown

    // +0x0c: bonus/duration accumulator; decremented each frame; expiry when <= 0
    float m_BonusAccum;

    // +0x10: parsed/configured flag (uint8; ctor strb 0; Parse sets = 1 as first action)
    uint8_t m_bConfigured;
    uint8_t _pad11[3];

    // +0x14: deferred-start timer threshold (-1.0f = no deferral)
    float m_DeferTime;

    // +0x18: gate: 1 while deferred-apply is pending; cleared after OnDeferComplete fires
    // (struct+0x18, read by Update @ 0x13fdc4 as the deferred-pending gate)
    bool m_bApplied;

    // +0x19: deferred flag (ctor = 1; adjacent to m_bApplied per binary bool pair)
    // DIFFERS: binary +0x18/+0x19 are adjacent bools {m_bApplied, m_bDeferred};
    // port maps the pair as two separate bytes to match the ctor writes
    // (p_pad[0x14]=0, p_pad[0x15]=1 in ctor @ 0x133378).
    bool m_bDeferred;
    uint8_t _pad1a[2];

    // +0x1c: defer-record ptr (NOT PowerUp* owner); OnDeferComplete reads *(this+0x1c)+0xc
    void* m_pDeferInfo;

    GameModifier()
        : m_Duration(0.0f)
        , m_reserved08(0.0f)
        , m_BonusAccum(0.0f)
        , m_bConfigured(0)
        , _pad11{0, 0, 0}
        , m_DeferTime(-1.0f)
        , m_bApplied(false)
        , m_bDeferred(true)
        , _pad1a{0, 0}
        , m_pDeferInfo(nullptr)
    {}

    // [0]/[1] ~GameModifier — MUST stay out-of-line (defined in GameModifier.cpp).
    // The binary's subclass destructors call it via PLT thunk 0x001127c0, so the
    // derived vptr store that precedes the call survives. Defining the body inline
    // here lets GCC inline the base dtor into every subclass D0/D1 and then
    // dead-store-eliminate the derived vptr write, leaving only "vtable for
    // GameModifier" in the derived deleting destructor — the opposite of the
    // binary. Keep it out-of-line.
    virtual ~GameModifier();

    // [2] ResetSpecific — clears per-modifier state; PURE in binary (0x360434)
    virtual void ResetSpecific() = 0;

    // [3] Update(float dt) @ 0x13fdc4 — base dispatcher (returns 0=alive, 1=expired).
    virtual int Update(float dt);

    // [4] UpdateSpecific(float dt) — PURE in binary (0x360434)
    virtual int UpdateSpecific(float dt) = 0;

    // [5] OnDeferComplete @ 0x140890 — called by Update when defer fires;
    // clamps m_BonusAccum via PowerUpManager multipliers
    // and two cached StringHash powerup-name ids.
    virtual void OnDeferComplete(bool unused, float* pExtra);

    // [6] RemoveModifier @ 0x143784 — base no-op
    virtual void RemoveModifier() {}

    // [7] GetType — base returns -1 (bit pattern 0xffffffff matches binary 0x143788);
    // DIFFERS: binary returns unsigned 0xffffffff; port uses int -1 (same bit pattern)
    // to allow subclass int overrides (Score=2, Time=0, Wave=1, Slash=3, etc.)
    virtual int GetType() { return -1; }

    // [8] ApplyModifier(bool,float*) — PURE in binary (0x360434); base body @
    // 0x140890 is the SAME compiled function as OnDeferComplete (slot 5) — the
    // fold m_Duration into m_BonusAccum + pExtra clamp + overtime/freeze
    // PowerUpManager clamp+scale. Subclasses call this via super() as the last
    // step of their own override (see #331); the port body delegates to
    // OnDeferComplete() so both call sites share one implementation. That
    // delegation is QUALIFIED (GameModifier::OnDeferComplete) and must stay so:
    // the base body is a super() target and never re-enters the vtable. Making
    // it virtual recurses forever through any subclass overriding both slots
    // (WaveModifier does) -- see the comment on the definition in GameModifier.cpp.
    virtual void ApplyModifier(bool isPurchased, float* extra) = 0;

    // [9] ParseSpecific — PURE in binary (0x360434)
    virtual void ParseSpecific(TiXmlElement* xml) = 0;

    // [10] Clone() — PURE in binary (slot 10 @ 0x3602bc PLT stub).
    // Heap-alloc new instance; subclasses return a concrete copy.
    virtual GameModifier* Clone() = 0;

    // Binary @ 0x00117DA0 — reads base XML attributes ("length", "waitUntilTime")
    // then dispatches ParseSpecific(xml).
    void Parse(TiXmlElement*);
    // Binary @ 0x001179AC — clears m_BonusAccum then dispatches ResetSpecific().
    void Reset();
};

#ifdef __bada__
#include <cstddef>
static_assert(sizeof(GameModifier) == 0x20, "GameModifier must be 0x20 bytes");
static_assert(offsetof(GameModifier, m_BonusAccum)  == 0x0c, "m_BonusAccum");
static_assert(offsetof(GameModifier, m_bConfigured) == 0x10, "m_bConfigured");
static_assert(offsetof(GameModifier, m_DeferTime)   == 0x14, "m_DeferTime");
static_assert(offsetof(GameModifier, m_bApplied)    == 0x18, "m_bApplied");
static_assert(offsetof(GameModifier, m_pDeferInfo)  == 0x1c, "m_pDeferInfo");
#endif

#endif // FN_GAME_MODIFIER_H
