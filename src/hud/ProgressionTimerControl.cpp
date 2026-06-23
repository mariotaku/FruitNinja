// Analysed: 2026-05-04T00:00
//
// ProgressionTimerControl : HUDControl3d
// Binary CU range: 0x001579f4..0x00157dbc (vtable @ 0x001e9d00)
//
// Defunct: ProgressionTimerControl -- fully compiled in the binary but no
// construction site exists in the shipping build. All methods are preserved
// with their binary semantics so future revival requires no re-RE. Method
// bodies that were empty in the binary are no-ops here; bodies with real
// field-write semantics are implemented faithfully per the RE doc.

#include "ProgressionTimerControl.h"
#include <cmath>
#include <cstdio>

// ---------------------------------------------------------------------------
// Constructor
// Binary @ 0x00157d08 (C2) / 0x00157dbc (C1)
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
    pos  = Vec3(-230.0f, 140.0f, 0.0f);
    size = Vec3(0.0f, 18.0f, 0.0f);
    m_TextBuf[0] = '\0';
}

// Destructor chain
// Binary @ 0x00157b14 (D0 deleting) / 0x00157ad0 (D1 non-deleting)
ProgressionTimerControl::~ProgressionTimerControl() {
}

// ---------------------------------------------------------------------------
// vtable slot 2 -- Binary @ 0x0015793c (empty bx lr)
// Defunct: ProgressionTimerControl -- no-op stub; v1.6.1 binary @ 0x0015793c
void ProgressionTimerControl::Init() {
    // Binary @ 0x0015793c
    // Defunct: ProgressionTimerControl -- never instantiated in shipping
    //          binary; class fully compiled but no construction site exists.
}

// ---------------------------------------------------------------------------
// vtable slot 3 -- Binary @ 0x00157940 (empty bx lr)
// Defunct: ProgressionTimerControl -- no-op stub; v1.6.1 binary @ 0x00157940
void ProgressionTimerControl::Release() {
    // Binary @ 0x00157940
    // Defunct: ProgressionTimerControl -- never instantiated in shipping
    //          binary; class fully compiled but no construction site exists.
}

// ---------------------------------------------------------------------------
// vtable slot 4 -- Binary @ 0x00157944
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
// vtable slot 6 -- Binary @ 0x00157964 (returns param_1 unchanged, no writes)
// Defunct: ProgressionTimerControl -- no-op stub; v1.6.1 binary @ 0x00157964
void ProgressionTimerControl::PreDraw(float* hudScaleRaw) {
    // Binary @ 0x00157964
    // Defunct: ProgressionTimerControl -- never instantiated in shipping
    //          binary; class fully compiled but no construction site exists.
    (void)hudScaleRaw;
}

// ---------------------------------------------------------------------------
// vtable slot 7 -- Binary @ 0x001579f4
// Binary draws m_TextBuf via Mortar::Font when m_ShowAnim > 0.
// Defunct: ProgressionTimerControl -- no-op stub; v1.6.1 binary @ 0x001579f4
void ProgressionTimerControl::Draw(float* hudScaleRaw) {
    // Binary @ 0x001579f4
    // Defunct: ProgressionTimerControl -- never instantiated in shipping
    //          binary; class fully compiled but no construction site exists.
    (void)hudScaleRaw;
}

// ---------------------------------------------------------------------------
// vtable slot 10 -- Binary @ 0x00157bb0
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
// vtable slot 11 -- Binary @ 0x00157968
// Identical to StopCountdown: Reset() then m_bCountUp = false.
// Defunct: ProgressionTimerControl -- no-op stub; v1.6.1 binary @ 0x00157968
bool ProgressionTimerControl::SetToMultiplayerState() {
    // Binary @ 0x00157968
    // Defunct: ProgressionTimerControl -- never instantiated in shipping
    //          binary; class fully compiled but no construction site exists.
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
