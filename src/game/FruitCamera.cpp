//
// FruitCamera : MortarCamera (0x16C bytes)
//

#include "FruitCamera.h"
#include "platform/InputEvent.h"
#include "render/MatrixManager.h"
#include "render/DisplayManager.h"
#include "math/MathUtil.h"
#include <cmath>
#include <cstdlib>

// Analysed: 2026-05-04T00:00
// Matches constructor at 0x00180e40 / 0x00180de0
// m_Target and m_ShakeDir initialized from _Vector2<float>::Zero (BSS, = 0.0, 0.0)
// Field assignment order matches binary (compiler interleaves reads/writes)
FruitCamera::FruitCamera()
    : m_pFollowEntity(0),
      m_CameraMode(0),
      m_TiltYaw(0), m_TiltPitch(0),
      m_ShakeDir(Vec2::Zero()),
      m_ShakeAngle(0), _pad142(0),
      m_Target(Vec2::Zero()),
      m_field14c(0.0f), m_field150(0.0f),
      m_DistanceMag(0.0f),
      m_ShakeIntensity(0.0f), m_ShakeIntensityInit(0.0f)
{
}

FruitCamera::~FruitCamera() {
}

// Vtable slot 3 override (0x00180c8c)
void FruitCamera::UpdateCamera(float dt) {
    m_field14c = (float)m_TiltPitch;
    m_field150 = (float)m_TiltYaw;

    Vec3 delta = m_pos - m_lookAt;
    m_DistanceMag = delta.Magnitude();

    m_LookAtSnapshot = m_lookAt;

    UpdateShake(dt);

    if (m_CameraMode == 0)
        UpdateIdle(dt);
    else if (m_CameraMode == 1)
        UpdateFollow(dt);
}

// 0x00180a08 — empty in idle mode
void FruitCamera::UpdateIdle(float dt) {
    (void)dt;
}

// 0x00180c50
void FruitCamera::UpdateFollow(float dt) {
    (void)dt;
    if (m_pFollowEntity == 0) {
        IdleCamera();
    }
    // TODO: follow entity position when entity system is wired
}

// 0x00180a20
void FruitCamera::IdleCamera() {
    m_pFollowEntity = 0;
    m_CameraMode = 0;
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
    return (m_CameraMode == 1) ? m_pFollowEntity : nullptr;
}

// Non-virtual (0x001810ac) — 4-type ortho dispatch.
//
// ASM-verified: 2026-05-06T00:00 binary @ 0x001810ac..0x001813d1 (asm-inspector)
// ASM-verified: 2026-05-06T00:00 binary @ 0x0019e7a8..0x0019e828 (asm-inspector)
//
// Cases 1/2/3 not needed by GameDraw (only PT_STANDARD is called).
// TODO: 0x001810ac — PT_ROTATED_CW / PT_ROTATED_CCW / PT_GENERIC ortho variants
void FruitCamera::SetupPerspective(PERSPECIVE_TYPE perspType, bool forceUpdate) {
    (void)perspType;
    MatrixManager& mm = MatrixManager::GetInstance();

    // Cache condition matches binary: `if (!m_bDirty && !forceUpdate)`.
    if (!m_bDirty && !forceUpdate) {
        Matrix44 viewMat44;
        m_localToWorld.ToMatrix44(viewMat44);
        mm.GetViewStack().SetCurrentMatrix(viewMat44);
        mm.GetProjectionStack().SetCurrentMatrix(m_projection);
        mm.UploadAll();
        mm.GetWorldStack().Reset();
        return;
    }

    // View: camera looks straight down +Z with optional shake target offset.
    // Binary @ 0x00181180..0x0018118e passes (eye, up, at) positionally.
    // Port matches the positional order: SetupLookAt(eye, upHint, target).
    // The port reinterprets slot 3 as `target` (canonical glLookAt) because
    // it skips the binary's compensating orientation-matrix multiply --
    // see MatrixManager::SetupLookAt comment.
    Vec3 eye(m_Target.x, m_Target.y, 1.0f);
    Vec3 up (0.0f, 1.0f, 0.0f);
    Vec3 at (m_Target.x, m_Target.y, 0.0f);
    mm.SetupLookAt(eye, up, at);
    m_localToWorld = Matrix43::FromMatrix44(mm.GetViewStack().m_Current);

    // ASM-verified: 2026-05-16 binary @ 0x001810ac..0x001813f4 (re-analyst).
    // Literal pool @ 0x001813d8: top=+160, bottom=-160, left=-240, right=+240,
    // near=+2000, far=-6000. Visible world is X=[-240,+240], Y=[-160,+160],
    // camera lookAt at origin (m_Target shake offset only). No Y/Z translation.
    // This is the ONLY ortho config GameDraw uses; PT_ROTATED variants
    // (anonymous_namespace::SetPerspective glFrustumf) are EGL-init only and
    // are overwritten every frame by this call. Spawn formulas in
    // WaveManager / Bomb / Fruit deliberately land entities INSIDE this band
    // (e.g. bombs at pos.y ~= -64); the visible "pop in" is the chuck-delay
    // countdown freeze (m_Countdown = 0.21f from DAT_0012258c), not a
    // viewport / camera bug.
    mm.SetupOrtho(160.0f, -160.0f, -240.0f, 240.0f, 2000.0f, -6000.0f);

    // Cache
    m_projection = mm.GetProjectionStack().m_Current;
    m_bDirty = false;
    m_bInitialized = false;

    mm.GetWorldStack().Reset();
}

// Binary @ 0x00180d10 — shake angle from impact, dir = (cos,sin)*9*dirScale
// DIFFERS: original = Math::Atan2Idx fixed-point trig; port uses sinf/cosf
//          because Math::SinIdx now wraps sinf anyway (semantically identical).
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

// ---------------------------------------------------------------------------
// Debug input handlers — Defunct: debug input; no caller registers them in
// retail binary. Bodies preserved as working debug-fly for screenshot use.
// TODO: 0x001... — g_DebugInputInhibited flag not present in port; always allow.
// ---------------------------------------------------------------------------

// Binary @ 0x00180a2c — debug pan +Y by 10
bool FruitCamera::DebugFlyUp(InputEvent* e) {
    (void)e;
    m_pos.y    += 10.0f;
    m_lookAt.y += 10.0f;
    return true;
}

// Binary @ 0x00180a6c — debug pan -Y by 10
bool FruitCamera::DebugFlyDown(InputEvent* e) {
    (void)e;
    m_pos.y    -= 10.0f;
    m_lookAt.y -= 10.0f;
    return true;
}

// Binary @ 0x00180aac — debug pan -X by 10
bool FruitCamera::DebugFlyLeft(InputEvent* e) {
    (void)e;
    m_pos.x    -= 10.0f;
    m_lookAt.x -= 10.0f;
    return true;
}

// Binary @ 0x00180aec — debug pan +X by 10
bool FruitCamera::DebugFlyRight(InputEvent* e) {
    (void)e;
    m_pos.x    += 10.0f;
    m_lookAt.x += 10.0f;
    return true;
}

// Binary @ 0x0018151c — orbit yaw += +0x96
bool FruitCamera::DebugTiltLeft(InputEvent* e) {
    (void)e;
    m_TiltYaw = (uint16_t)(m_TiltYaw + 0x96);
    return true;
}

// Binary @ 0x00181400 — orbit yaw += -0x96
bool FruitCamera::DebugTiltRight(InputEvent* e) {
    (void)e;
    m_TiltYaw = (uint16_t)(m_TiltYaw - 0x96);
    return true;
}

// Binary @ 0x00181638 — orbit pitch += -0x96
bool FruitCamera::DebugTiltDown(InputEvent* e) {
    (void)e;
    m_TiltPitch = (uint16_t)(m_TiltPitch - 0x96);
    return true;
}

// Binary @ 0x00181754 — orbit pitch += +0x96
bool FruitCamera::DebugTiltUp(InputEvent* e) {
    (void)e;
    m_TiltPitch = (uint16_t)(m_TiltPitch + 0x96);
    return true;
}

// Binary @ 0x00180b70 — debug zoom in: pos = lookAt + (pos-lookAt)*0.99
bool FruitCamera::DebugZoomDown(InputEvent* e) {
    (void)e;
    m_pos = m_lookAt + (m_pos - m_lookAt) * 0.99f;
    return true;
}

// Binary @ 0x00180be0 — debug zoom out: pos = lookAt + (pos-lookAt)*1.01
bool FruitCamera::DebugZoomUp(InputEvent* e) {
    (void)e;
    m_pos = m_lookAt + (m_pos - m_lookAt) * 1.01f;
    return true;
}
