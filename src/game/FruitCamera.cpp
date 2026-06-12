//
// FruitCamera : MortarCamera (0x1a8 bytes, v1.6.1)
//

#include "FruitCamera.h"
#include "platform/InputEvent.h"
#include "render/MatrixManager.h"
#include "render/DisplayManager.h"
#include "math/MathUtil.h"
#include <cmath>
#include <cstdlib>

// ctor @ 0x1edb48 (v1.6.1)
// DIFFERS: v1.5.1 port had m_field150, m_DistanceMag, m_LookAtSnapshot in +0x14c..+0x168 region.
//   v1.6.1 layout collapses those into m_LookAt(Vec3)@+0x150, m_Zoom@+0x15c, m_RollOut@+0x160,
//   m_ZoomT@+0x164, m_ZoomTarget(Vec3)@+0x168, m_ZoomScale@+0x174, m_RollScale@+0x178,
//   and shifts m_ShakeIntensity/m_ShakeIntensityInit to +0x17c/+0x180 before m_OnZoomDone@+0x184.
FruitCamera::FruitCamera()
    : m_pFollowEntity(0),
      m_CameraMode(0),
      m_TiltYaw(0), m_TiltPitch(0),
      m_ShakeDir(Vec2::Zero()),
      m_ShakeAngle(0), _pad142(0),
      m_Target(Vec2::Zero()),
      m_field14c(1.0f),
      m_LookAt(0.0f, 0.0f, 0.0f),
      m_Zoom(1.0f),
      m_RollOut(0), _pad162(0),
      m_ZoomT(0.0f),
      m_ZoomTarget(0.0f, 0.0f, 0.0f),
      m_ZoomScale(1.0f),
      m_RollScale(1.0f),
      m_ShakeIntensity(0.0f),
      m_ShakeIntensityInit(0.0f),
      m_OnZoomDone()
{
    // TODO: 0x1edb48 — resolve exact DAT value for m_ZoomTarget initial vec from FruitCamera ctor
    // TODO: 0x1edb48 — resolve m_field14c initial value (spec says =1.0; confirm from DAT)
}

FruitCamera::~FruitCamera() {
}

// Vtable slot 3 override (v1.6.1 @ 0x1edf24)
// Per spec:
//   1. UpdateShake(dt)
//   2. LookAt(+0x150) = global look DAT
//   3. switch m_CameraMode(+0x130): 0=idle, 1=follow, 2=zoom-in, 3=zoom-out
//   4. m_Zoom(+0x15c) = Lerp(1.0, m_ZoomScale, InverseSquareTransition(ZoomT))
//   5. LookAt = Lerp(globalLook, m_ZoomTarget, SinTransition(ZoomT, k))
//   6. m_RollOut = (short)(Lerp(0, m_RollScale, InverseSquareTransition(ZoomT)) * k)
//   7. add shake offset: Vec3(m_Target.x, m_Target.y, 0) += into LookAt
// ASM-verified: 2026-05-17 binary @ 0x00180c8c..0x00180d0e (re-analyst) [v1.5.1].
// v1.6.1 UpdateCamera at 0x1edf24 extends this with the zoom state machine.
void FruitCamera::UpdateCamera(float dt) {
    m_field14c = (float)m_TiltPitch;
    // Note: v1.5.1 had m_field150=(float)m_TiltYaw; v1.6.1 layout may differ.
    // TODO: 0x1edf24 — confirm m_field14c/second cast in v1.6.1 UpdateCamera

    UpdateShake(dt);

    // LookAt snapshot — written each frame from global look DAT
    // TODO: 0x1edf24 — resolve global look DAT address for m_LookAt assignment
    m_LookAt = m_lookAt;

    switch (m_CameraMode) {
    case 0:
        UpdateIdle(dt);
        break;
    case 1:
        UpdateFollow(dt);
        break;
    case 2:
        // Zoom-in: ZoomT += 3*dt, clamp to 1.0; on done fire Delegate0
        m_ZoomT += 3.0f * dt;
        if (m_ZoomT >= 1.0f) {
            m_ZoomT = 1.0f;
            // TODO: 0x1edf24 — fire m_OnZoomDone when ZoomT clamps to 1.0
            //   (requires #29 Delegate/Event infra): m_OnZoomDone();
            m_CameraMode = 0;
        }
        break;
    case 3:
        // Zoom-out: ZoomT += -10*dt, clamp to 0; on done fire Delegate0
        m_ZoomT += -10.0f * dt;
        if (m_ZoomT <= 0.0f) {
            m_ZoomT = 0.0f;
            // TODO: 0x1edf24 — fire m_OnZoomDone when ZoomT clamps to 0.0
            //   (requires #29 Delegate/Event infra): m_OnZoomDone();
            m_CameraMode = 0;
        }
        break;
    default:
        break;
    }

    // m_Zoom = Lerp(1.0, m_ZoomScale, InverseSquareTransition(ZoomT))
    // InverseSquareTransition(t) = 1 - (1-t)^2 = 2t - t^2
    {
        float t = m_ZoomT;
        float f = 2.0f * t - t * t;   // InverseSquareTransition(t)
        m_Zoom = 1.0f + (m_ZoomScale - 1.0f) * f;
    }

    // LookAt = Lerp(globalLook, m_ZoomTarget, SinTransition(ZoomT, k))
    // SinTransition: sin(t * pi/2) smoothstep variant
    // TODO: 0x1edf24 — resolve exact SinTransition(ZoomT, k) constant k from DAT
    {
        float t = m_ZoomT;
        float f = sinf(t * 1.5707963f);   // sin(t * pi/2); TODO confirm k multiplier
        m_LookAt.x = m_lookAt.x + (m_ZoomTarget.x - m_lookAt.x) * f;
        m_LookAt.y = m_lookAt.y + (m_ZoomTarget.y - m_lookAt.y) * f;
        m_LookAt.z = m_lookAt.z + (m_ZoomTarget.z - m_lookAt.z) * f;
    }

    // m_RollOut = (short)(Lerp(0, m_RollScale, InverseSquareTransition(ZoomT)) * k)
    // TODO: 0x1edf24 — resolve roll multiplier constant k from DAT
    {
        float t = m_ZoomT;
        float f = 2.0f * t - t * t;
        m_RollOut = (uint16_t)(int)(m_RollScale * f);
    }

    // Add shake offset to LookAt
    m_LookAt.x += m_Target.x;
    m_LookAt.y += m_Target.y;
}

// 0x1ed77c (v1.6.1 IdleCamera — sets mode 0)
void FruitCamera::IdleCamera() {
    m_pFollowEntity = 0;
    m_CameraMode = 0;
}

// Empty in idle mode
void FruitCamera::UpdateIdle(float dt) {
    (void)dt;
}

// Binary @ 0x00180c50 (v1.5.1) / v1.6.1 equivalent — delta-preserving follow.
// ASM-verified semantic (re-analyst): delta = m_lookAt - entity->pos;
//   m_lookAt = entity->pos; m_pos -= delta;
// Net: m_pos += (entity->pos - oldLookAt), preserving (m_pos - m_lookAt).
void FruitCamera::UpdateFollow(float dt) {
    (void)dt;
    if (m_pFollowEntity == 0) {
        IdleCamera();
    } else {
        Vec3 delta = m_lookAt - m_pFollowEntity->pos;
        m_lookAt = m_pFollowEntity->pos;
        m_pos -= delta;
    }
}

// Binary @ 0x00180b2c — bind follow entity, reset tilt to (0,0), up=(0,1,0)
void FruitCamera::FollowEntity(Mortar::Entity* entity) {
    if (entity) {
        m_pFollowEntity = entity;
        m_CameraMode = 1;
    }
    m_TiltYaw   = 0;
    m_TiltPitch = 0;
    m_up = Vec3(0.0f, 1.0f, 0.0f);
}

// Binary @ 0x00180a0c — return m_pFollowEntity iff mode==1
Mortar::Entity* FruitCamera::GetFollowEntity() {
    return (m_CameraMode == 1) ? m_pFollowEntity : 0;
}

// Non-virtual (0x001810ac) — 4-type ortho dispatch.
//
// ASM-verified: 2026-05-06T00:00 binary @ 0x001810ac..0x001813d1 (asm-inspector)
// ASM-verified: 2026-05-06T00:00 binary @ 0x0019e7a8..0x0019e828 (asm-inspector)
void FruitCamera::SetupPerspective(PERSPECIVE_TYPE perspType, bool forceUpdate) {
    MatrixManager& mm = MatrixManager::GetInstance();

    if (!m_bDirty && !forceUpdate) {
        Matrix44 viewMat44;
        m_localToWorld.ToMatrix44(viewMat44);
        mm.GetViewStack().SetCurrentMatrix(viewMat44);
        mm.GetProjectionStack().SetCurrentMatrix(m_projection);
        mm.UploadAll();
        mm.GetWorldStack().Reset();
        return;
    }

    Vec3 eye, at;
    if (perspType == PT_GENERIC) {
        eye = Vec3(0.0f, 0.0f, 1.0f);
        at  = Vec3(0.0f, 0.0f, 0.0f);
    } else {
        eye = Vec3(m_Target.x, m_Target.y, 1.0f);
        at  = Vec3(m_Target.x, m_Target.y, 0.0f);
    }
    Vec3 up(0.0f, 1.0f, 0.0f);
    mm.SetupLookAt(eye, up, at);
    m_localToWorld = Matrix43::FromMatrix44(mm.GetViewStack().m_Current);

    // ASM-verified: 2026-05-16 binary @ 0x001810ac..0x001813f4 (re-analyst).
    mm.SetupOrtho(160.0f, -160.0f, -240.0f, 240.0f, 2000.0f, -6000.0f);

    m_projection = mm.GetProjectionStack().m_Current;
    m_bDirty = false;
    m_bInitialized = false;

    mm.GetWorldStack().Reset();
}

// Binary @ 0x00180d10 — shake angle from impact, dir = (cos,sin)*9*dirScale
// ASM-verified: 2026-05-17 binary @ 0x00180d10..0x00180d68 (re-analyst).
// DIFFERS: original = Math::Atan2Idx 16-bit-angle-index trig; port uses
//          atan2f/sinf/cosf with the (radians to 16-bit-index) conversion
//          factor 65536/2pi.
void FruitCamera::CreateCameraShake(Vec3 impact, float intensity, float dirScale) {
    m_ShakeAngle = (uint16_t)(int)(atan2f(impact.y, impact.x) * 65536.0f / 6.2831853f);

    float angle_rad = (float)m_ShakeAngle * 6.2831853f / 65536.0f;
    m_ShakeDir.x = cosf(angle_rad) * 9.0f * dirScale;
    m_ShakeDir.y = sinf(angle_rad) * 9.0f * dirScale;

    m_ShakeIntensityInit = intensity;
    m_ShakeIntensity = intensity;
}

// 0x00180ea0 — constants verified from literal pool at 0x00181068
void FruitCamera::UpdateShake(float dt) {
    static const float LERP_FACTOR    = 0.2f;    // DAT_00181068
    static const float SNAP_NEG       = -0.01f;  // DAT_0018106c
    static const float SNAP_POS       = 0.01f;   // DAT_00181070
    static const float DAMP_FACTOR    = 0.8f;    // DAT_00181074

    if (m_ShakeIntensity <= 0.0f) {
        if (m_Target.x >= SNAP_NEG && m_Target.x <= SNAP_POS)
            m_Target.x = 0.0f;
        else
            m_Target.x *= DAMP_FACTOR;

        if (m_Target.y >= SNAP_NEG && m_Target.y <= SNAP_POS)
            m_Target.y = 0.0f;
        else
            m_Target.y *= DAMP_FACTOR;
    } else {
        m_ShakeIntensity -= dt;

        float dx = m_Target.x - m_ShakeDir.x;
        float dy = m_Target.y - m_ShakeDir.y;
        if (dx * dx + dy * dy < 16.0f) {
            m_ShakeAngle += 0x6388 + (uint16_t)(rand() % 0x38E0);

            float ratio = (m_ShakeIntensityInit > 0.0f) ?
                (m_ShakeIntensity / m_ShakeIntensityInit) : 0.0f;
            float scale = ratio * 9.0f;
            float angle_rad = (float)m_ShakeAngle * 6.2831853f / 65536.0f;
            m_ShakeDir.x = cosf(angle_rad) * scale;
            m_ShakeDir.y = sinf(angle_rad) * scale;
        }

        float ratio = (m_ShakeIntensityInit > 0.0f) ?
            (m_ShakeIntensity / m_ShakeIntensityInit) : 0.0f;
        float t = ratio + 1.0f;
        m_Target.x += (m_ShakeDir.x - m_Target.x) * LERP_FACTOR * t;
        m_Target.y += (m_ShakeDir.y - m_Target.y) * LERP_FACTOR * t;

        m_bDirty = true;
    }
}

// StartZoomIn — arm zoom-in mode; binary body not yet fully RE'd.
// TODO: 0x1edf24 — resolve StartZoomIn / ZoomIn binary symbol and exact signature
void FruitCamera::StartZoomIn(const Vec3& target, float zoomScale, float rollScale,
                               Mortar::Delegate0<void> onDone) {
    m_ZoomTarget = target;
    m_ZoomScale  = zoomScale;
    m_RollScale  = rollScale;
    m_OnZoomDone = onDone;
    m_ZoomT      = 0.0f;
    m_CameraMode = 2;
}

// StartZoomOut — arm zoom-out mode.
// TODO: 0x1edf24 — resolve StartZoomOut / ZoomOut binary symbol and exact signature
void FruitCamera::StartZoomOut(Mortar::Delegate0<void> onDone) {
    m_OnZoomDone = onDone;
    m_ZoomT      = 1.0f;
    m_CameraMode = 3;
}

// ---------------------------------------------------------------------------
// Debug input handlers — Defunct: debug input; no caller registers them in
// retail binary. Bodies preserved as working debug-fly for screenshot use.
// ---------------------------------------------------------------------------

bool FruitCamera::DebugFlyUp(InputEvent* e) {
    (void)e;
    m_pos.y    += 10.0f;
    m_lookAt.y += 10.0f;
    return true;
}

bool FruitCamera::DebugFlyDown(InputEvent* e) {
    (void)e;
    m_pos.y    -= 10.0f;
    m_lookAt.y -= 10.0f;
    return true;
}

bool FruitCamera::DebugFlyLeft(InputEvent* e) {
    (void)e;
    m_pos.x    -= 10.0f;
    m_lookAt.x -= 10.0f;
    return true;
}

bool FruitCamera::DebugFlyRight(InputEvent* e) {
    (void)e;
    m_pos.x    += 10.0f;
    m_lookAt.x += 10.0f;
    return true;
}

bool FruitCamera::DebugTiltLeft(InputEvent* e) {
    (void)e;
    m_TiltYaw = (uint16_t)(m_TiltYaw + 0x96);
    return true;
}

bool FruitCamera::DebugTiltRight(InputEvent* e) {
    (void)e;
    m_TiltYaw = (uint16_t)(m_TiltYaw - 0x96);
    return true;
}

bool FruitCamera::DebugTiltDown(InputEvent* e) {
    (void)e;
    m_TiltPitch = (uint16_t)(m_TiltPitch - 0x96);
    return true;
}

bool FruitCamera::DebugTiltUp(InputEvent* e) {
    (void)e;
    m_TiltPitch = (uint16_t)(m_TiltPitch + 0x96);
    return true;
}

bool FruitCamera::DebugZoomDown(InputEvent* e) {
    (void)e;
    m_pos = m_lookAt + (m_pos - m_lookAt) * 0.99f;
    return true;
}

bool FruitCamera::DebugZoomUp(InputEvent* e) {
    (void)e;
    m_pos = m_lookAt + (m_pos - m_lookAt) * 1.01f;
    return true;
}
