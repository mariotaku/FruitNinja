#ifndef FN_HUD_PROGRESSION_TIMER_CONTROL_H
#define FN_HUD_PROGRESSION_TIMER_CONTROL_H

// Defunct: ProgressionTimerControl -- fully compiled in binary but never
// instantiated in the shipping build. Class shape, vtable layout, and field
// offsets are preserved per stub-don't-skip policy.
// Binary vtable @ 0x001e9d00. Ctor @ 0x00157d08 / 0x00157dbc.

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
    Delegate0<void> m_OnExpiredDelegate;

    // Binary @ 0x00157d08 (C2) / 0x00157dbc (C1)
    // Pos = Vec3(-230, 140, 0), size = Vec3(0, 18, 0)
    ProgressionTimerControl();

    // Binary @ 0x00157b14 (D0 deleting) / 0x00157ad0 (D1 non-deleting)
    virtual ~ProgressionTimerControl();

    // vtable slot 2 -- Binary @ 0x0015793c (empty bx lr)
    // Defunct: ProgressionTimerControl -- no-op stub; binary @ 0x0015793c
    void Init() override;

    // vtable slot 3 -- Binary @ 0x00157940 (empty bx lr)
    // Defunct: ProgressionTimerControl -- no-op stub; binary @ 0x00157940
    void Release() override;

    // vtable slot 4 -- Binary @ 0x00157944
    // Zeros m_TotalTime/m_RemainingTime/m_bIsActive/m_bPaused/m_bAutoStopOnExpire.
    // Does NOT touch m_bCountUp or m_ShowAnim.
    void Reset() override;

    // vtable slot 6 -- Binary @ 0x00157964 (returns param_1 unchanged)
    // Defunct: ProgressionTimerControl -- no-op stub; binary @ 0x00157964
    void PreDraw(const Vec3& hudScale) override;

    // vtable slot 7 -- Binary @ 0x001579f4
    // Binary draws m_TextBuf via Mortar::Font when m_ShowAnim > 0.
    // Defunct: ProgressionTimerControl -- no-op stub; binary @ 0x001579f4
    void Draw(const Vec3& hudScale, int layerMask) override;

    // vtable slot 10 -- Binary @ 0x00157bb0
    // Drives m_ShowAnim toward 0/1 at +/-3/s based on m_bCountUp; if active
    // && !paused, decrements m_RemainingTime, calls OnTimeExpired at zero,
    // sprintf's ceil(remaining) into m_TextBuf each frame.
    void Update(float dt) override;

    // vtable slot 11 -- Binary @ 0x00157968
    // Defunct: ProgressionTimerControl -- no-op stub; binary @ 0x00157968
    // Identical to StopCountdown: Reset() then m_bCountUp = false.
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
    void StartCountdown(float duration, Delegate0<void> onExpired,
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
