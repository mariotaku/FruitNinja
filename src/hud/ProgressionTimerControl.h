#ifndef FN_HUD_PROGRESSION_TIMER_CONTROL_H
#define FN_HUD_PROGRESSION_TIMER_CONTROL_H

// Defunct: ProgressionTimerControl -- fully compiled in binary but never
// instantiated in the shipping build (confirmed: v1.6.1 has NO PLT thunk for
// either ctor, @0x001aa8d8 / @0x001aa9b8). Class shape, vtable layout, and field
// offsets are preserved per stub-don't-skip policy.
//
// "Defunct" here means UNREACHABLE, not "empty in the binary". Only Init
// (@0x001aa424), Release (@0x001aa428) and PreDraw (@0x001aa450) are literally
// `bx lr`. Reset (@0x001aa42c), Draw (@0x001aa50c), Update (@0x001aa7b8) and
// SetToMultiplayerState (@0x001aa454) all have real bodies -- do not describe
// them as no-op stubs.

#include "HUDControl3d.h"
#include "util/Delegate.h"
#include <cstdint>

class ProgressionTimerControl : public HUDControl3d {
public:
    // +0x7C: total countdown duration (seconds)
    float m_TotalTime;

    // +0x80: remaining time (counts down from m_TotalTime)
    float m_RemainingTime;

    // +0x84: show/hide fade interpolator (0..1, drives in at +3/s, out at -3/s)
    float m_ShowAnim;

    // +0x88: text buffer; sprintf'd with ceil(m_RemainingTime) each frame
    // Binary: plain char[64], NOT AsciiString.
    char m_TextBuf[64];

    // +0xC8: true while countdown is running
    bool m_bIsActive;

    // +0xC9: true while paused (timer does not decrement)
    bool m_bPaused;

    // +0xCA: count direction (false=down, true=up); drives m_ShowAnim direction
    bool m_bCountUp;

    // +0xCB: if true, StopCountdown() is called when timer reaches zero
    bool m_bAutoStopOnExpire;

    // +0xCC: delegate fired when timer expires (36 bytes)
    Mortar::Delegate0<void> m_OnExpiredDelegate;

    // v1.6.1 ctors @0x001aa8d8 / @0x001aa9b8 (neither has a PLT thunk -> never called)
    // Pos = Vec3(-230, 140, 0), size = Vec3(0, 18, 0)
    ProgressionTimerControl();

    // Binary @ 0x00157b14 (D0 deleting) / 0x00157ad0 (D1 non-deleting)
    virtual ~ProgressionTimerControl();

    // vtable slot 2 -- v1.6.1 ProgressionTimerControl::Init @0x001aa424 (empty bx lr)
    // Defunct: ProgressionTimerControl -- no-op stub; v1.6.1 ProgressionTimerControl::Init @ 0x001aa424
    void Init() override;

    // vtable slot 3 -- v1.6.1 ProgressionTimerControl::Release @0x001aa428 (empty bx lr)
    // Defunct: ProgressionTimerControl -- no-op stub; v1.6.1 ProgressionTimerControl::Release @ 0x001aa428
    void Release() override;

    // vtable slot 4 -- v1.6.1 ProgressionTimerControl::Reset @0x001aa42c
    // NOT empty: writes 5 fields. Zeros m_TotalTime/m_RemainingTime/m_bIsActive/
    // m_bPaused/m_bAutoStopOnExpire. Does NOT touch m_bCountUp or m_ShowAnim.
    void Reset() override;

    // vtable slot 6 -- v1.6.1 ProgressionTimerControl::PreDraw @0x001aa450 (returns param_1 unchanged)
    // Defunct: ProgressionTimerControl -- no-op stub; v1.6.1 ProgressionTimerControl::PreDraw @ 0x001aa450
    void PreDraw(float* hudScaleRaw) override;

    // vtable slot 7 -- v1.6.1 ProgressionTimerControl::Draw @0x001aa50c
    // NOT empty in the binary: it really draws m_TextBuf. The port body is an
    // unimplemented gap, not a faithful no-op -- see the TODO in the .cpp.
    void Draw(float* hudScaleRaw) override;

    // vtable slot 10 -- v1.6.1 ProgressionTimerControl::Update @0x001aa7b8
    // Drives m_ShowAnim toward 0/1 at +/-3/s based on m_bCountUp; if active
    // && !paused, decrements m_RemainingTime, calls OnTimeExpired at zero,
    // sprintf's ceil(remaining) into m_TextBuf each frame.
    void Update(float dt) override;

    // vtable slot 11 -- v1.6.1 ProgressionTimerControl::SetToMultiplayerState @0x001aa454
    // NOT empty: calls vtable slot 4 (Reset) then sets m_bCountUp = false --
    // i.e. identical to StopCountdown. Implemented faithfully below.
    bool SetToMultiplayerState() override;

    // vtable slot 12 -- Binary @ 0x0015818c
    // Returns 4 (live data -- not Defunct).
    int GetType() override;

    // Non-virtual -- Binary @ 0x0015797c
    // Reset() + m_bCountUp = false.
    void StopCountdown();

    // Non-virtual -- Binary @ 0x00157990
    // m_RemainingTime = m_TotalTime.
    void ResetTimer();

    // Non-virtual -- Binary @ 0x0015799c
    // Writes total/remaining/delegate/autoStop/active=true/countUp;
    // initial sprintf("%d", ceil(duration)) into m_TextBuf.
    // Does NOT touch m_bPaused or m_ShowAnim.
    void StartCountdown(float duration, Mortar::Delegate0<void> onExpired,
                        bool countUp, bool autoStop);

    // Non-virtual -- Binary @ 0x00157b8c
    // if m_bAutoStopOnExpire: m_bCountUp=false, Reset(); then fires m_OnExpiredDelegate.
    void OnTimeExpired();

#ifdef __bada__
    // Layout assertions -- only valid under Bada/ARM cross-toolchain.
#endif
};

#ifdef __bada__
static_assert(sizeof(ProgressionTimerControl) == 240,
              "ProgressionTimerControl size mismatch");
static_assert(__builtin_offsetof(ProgressionTimerControl, m_TotalTime)         == 0x7C, "m_TotalTime offset");
static_assert(__builtin_offsetof(ProgressionTimerControl, m_RemainingTime)     == 0x80, "m_RemainingTime offset");
static_assert(__builtin_offsetof(ProgressionTimerControl, m_ShowAnim)          == 0x84, "m_ShowAnim offset");
static_assert(__builtin_offsetof(ProgressionTimerControl, m_TextBuf)           == 0x88, "m_TextBuf offset");
static_assert(__builtin_offsetof(ProgressionTimerControl, m_bIsActive)         == 0xC8, "m_bIsActive offset");
static_assert(__builtin_offsetof(ProgressionTimerControl, m_bPaused)           == 0xC9, "m_bPaused offset");
static_assert(__builtin_offsetof(ProgressionTimerControl, m_bCountUp)          == 0xCA, "m_bCountUp offset");
static_assert(__builtin_offsetof(ProgressionTimerControl, m_bAutoStopOnExpire) == 0xCB, "m_bAutoStopOnExpire offset");
static_assert(__builtin_offsetof(ProgressionTimerControl, m_OnExpiredDelegate) == 0xCC, "m_OnExpiredDelegate offset");
#endif

#endif // FN_HUD_PROGRESSION_TIMER_CONTROL_H
