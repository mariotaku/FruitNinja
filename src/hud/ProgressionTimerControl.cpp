// ProgressionTimerControl : HUDControl3d
//
// Defunct: ProgressionTimerControl -- fully compiled in the binary but no
// construction site exists in the shipping build (v1.6.1 has no PLT thunk for
// either ctor, @0x001aa8d8 / @0x001aa9b8). All methods are preserved with their
// binary semantics so future revival requires no re-RE.
//
// "Defunct" == UNREACHABLE, not "empty". Only Init/Release/PreDraw are literally
// `bx lr` in the binary; Reset, Draw, Update and SetToMultiplayerState have real
// bodies. Draw is the one genuine port gap (see its TODO).

#include "ProgressionTimerControl.h"
#include <cmath>
#include <cstdio>

// ---------------------------------------------------------------------------
// Constructor
// v1.6.1 ProgressionTimerControl::{ctor} @0x001aa9b8 (C1) / C2 @0x001aa8d8 (neither reachable -- no PLT thunk)
// Pos = Vec3(-230, 140, 0). Derivation: Vec3(10,-20,0) + Vec3(480,320,0)*Vec3(-0.5,0.5,0)
//   = Vec3(10-240, -20+160, 0) = Vec3(-230, 140, 0).
// Size = Vec3(0, 18, 0).
// Defunct: ProgressionTimerControl -- never instantiated in shipping binary.
ProgressionTimerControl::ProgressionTimerControl()
    : HUDControl3d()
    , m_TotalTime(0.0f)
    , m_RemainingTime(0.0f)
    , m_ShowAnim(0.0f)
    , m_bIsActive(false)
    , m_bPaused(false)
    , m_bCountUp(false)
    , m_bAutoStopOnExpire(false)
    , m_OnExpiredDelegate()
{
    pos  = _Vector3<float>(-230.0f, 140.0f, 0.0f);
    size = _Vector3<float>(0.0f, 18.0f, 0.0f);
    m_TextBuf[0] = '\0';
}

// Destructor chain
// Binary @ 0x00157b14 (D0 deleting) / 0x00157ad0 (D1 non-deleting)
ProgressionTimerControl::~ProgressionTimerControl() {
}

// ---------------------------------------------------------------------------
// vtable slot 2 -- v1.6.1 ProgressionTimerControl::Init @0x001aa424 (empty bx lr)
// Defunct: ProgressionTimerControl -- no-op stub; v1.6.1 ProgressionTimerControl::Init @ 0x001aa424
void ProgressionTimerControl::Init() {
}

// ---------------------------------------------------------------------------
// vtable slot 3 -- v1.6.1 ProgressionTimerControl::Release @0x001aa428 (empty bx lr)
// Defunct: ProgressionTimerControl -- no-op stub; v1.6.1 ProgressionTimerControl::Release @ 0x001aa428
void ProgressionTimerControl::Release() {
}

// ---------------------------------------------------------------------------
// vtable slot 4 -- v1.6.1 ProgressionTimerControl::Reset @0x001aa42c
// NOT a no-op: the binary body writes 5 fields.
// Zeros m_TotalTime, m_RemainingTime, m_bIsActive, m_bPaused, m_bAutoStopOnExpire.
// Does NOT touch m_bCountUp or m_ShowAnim (binary does not write those).
void ProgressionTimerControl::Reset() {
    m_TotalTime        = 0.0f;
    m_RemainingTime    = 0.0f;
    m_bIsActive        = false;
    m_bPaused          = false;
    m_bAutoStopOnExpire= false;
}

// ---------------------------------------------------------------------------
// vtable slot 6 -- v1.6.1 ProgressionTimerControl::PreDraw @0x001aa450 (returns param_1 unchanged, no writes)
// Defunct: ProgressionTimerControl -- no-op stub; v1.6.1 ProgressionTimerControl::PreDraw @ 0x001aa450
void ProgressionTimerControl::PreDraw(float* hudScaleRaw) {
    (void)hudScaleRaw;
}

// ---------------------------------------------------------------------------
// vtable slot 7 -- v1.6.1 ProgressionTimerControl::Draw @0x001aa50c
// TODO: v1.6.1 0x001aa50c (ProgressionTimerControl::Draw) — NOT a no-op in the binary and
// NOT correctly described as a Defunct stub: it really renders m_TextBuf through
// game_work.pM_Fonts[2], via
//     DrawString(pos.x + size.x * -0.6f,
//                pos.y + (1.0f - m_ShowAnim) * (1.0f - m_ShowAnim) * 50.0f,
//                /*size*/ 32, /*flags*/ 0xd)
// The port body below is an unimplemented gap. It is harmless only because the class has no
// construction site in v1.6.1 (no ctor PLT thunk), so Draw is unreachable. Implement this if
// the class is ever revived.
void ProgressionTimerControl::Draw(float* hudScaleRaw) {
    (void)hudScaleRaw;
}

// ---------------------------------------------------------------------------
// vtable slot 10 -- v1.6.1 ProgressionTimerControl::Update @0x001aa7b8
// Drives m_ShowAnim toward target at +/-3.0/s based on m_bCountUp (false=fade
// out toward 0, true=fade in toward 1). If active && !paused, decrements
// m_RemainingTime by dt; at zero calls OnTimeExpired(); sprintf's
// ceil(m_RemainingTime) into m_TextBuf each frame while active.
void ProgressionTimerControl::Update(float dt) {
    // Animate m_ShowAnim: count-up mode fades in (toward 1), else fades out (toward 0).
    if (m_bCountUp) {
        m_ShowAnim += 3.0f * dt;
        if (m_ShowAnim > 1.0f) {
            m_ShowAnim = 1.0f;
        }
    } else {
        m_ShowAnim -= 3.0f * dt;
        if (m_ShowAnim < 0.0f) {
            m_ShowAnim = 0.0f;
        }
    }

    if (m_bIsActive && !m_bPaused) {
        m_RemainingTime -= dt;
        if (m_RemainingTime <= 0.0f) {
            m_RemainingTime = 0.0f;
            OnTimeExpired();
        }
        // Format remaining time as ceiling integer each frame.
        float ceiling = ceilf(m_RemainingTime);
        snprintf(m_TextBuf, sizeof(m_TextBuf), "%d", (int)ceiling);
    }
}

// ---------------------------------------------------------------------------
// vtable slot 11 -- v1.6.1 ProgressionTimerControl::SetToMultiplayerState @0x001aa454
// NOT a no-op: the binary calls vtable slot 4 (Reset) then sets m_bCountUp = false,
// i.e. exactly StopCountdown().
bool ProgressionTimerControl::SetToMultiplayerState() {
    StopCountdown();
    return true;
}

// ---------------------------------------------------------------------------
// vtable slot 12 -- Binary @ 0x0015818c
// Returns 4. Not Defunct -- live data used by HUD type dispatch.
int ProgressionTimerControl::GetType() {
    return 4;
}

// ---------------------------------------------------------------------------
// Non-virtual -- Binary @ 0x0015797c
// Reset() + m_bCountUp = false.
void ProgressionTimerControl::StopCountdown() {
    Reset();
    m_bCountUp = false;
}

// ---------------------------------------------------------------------------
// Non-virtual -- Binary @ 0x00157990
// m_RemainingTime = m_TotalTime.
void ProgressionTimerControl::ResetTimer() {
    m_RemainingTime = m_TotalTime;
}

// ---------------------------------------------------------------------------
// Non-virtual -- Binary @ 0x0015799c
// Writes total/remaining/delegate/autoStop/active=true/countUp.
// Initial sprintf("%d", ceil(duration)) into m_TextBuf.
// Does NOT touch m_bPaused or m_ShowAnim.
void ProgressionTimerControl::StartCountdown(float duration,
                                              Mortar::Delegate0<void> onExpired,
                                              bool countUp, bool autoStop) {
    m_TotalTime         = duration;
    m_RemainingTime     = duration;
    m_OnExpiredDelegate = onExpired;
    m_bAutoStopOnExpire = autoStop;
    m_bIsActive         = true;
    m_bCountUp          = countUp;

    // Initial text: ceiling of duration as integer string.
    float ceiling = ceilf(duration);
    snprintf(m_TextBuf, sizeof(m_TextBuf), "%d", (int)ceiling);
}

// ---------------------------------------------------------------------------
// Non-virtual -- Binary @ 0x00157b8c
// if m_bAutoStopOnExpire: m_bCountUp=false; Reset(); fire m_OnExpiredDelegate.
void ProgressionTimerControl::OnTimeExpired() {
    if (m_bAutoStopOnExpire) {
        m_bCountUp = false;
    }
    Reset();
    m_OnExpiredDelegate();
}
