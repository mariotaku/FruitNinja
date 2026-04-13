//
// FruitCamera : MortarCamera (0x16C bytes)
// See docs/engine/camera.md for full layout and method decompilation.
//

#include "FruitCamera.h"
#include "render/MatrixManager.h"
#include "render/DisplayManager.h"
#include <cmath>
#include <cstdlib>

// Analysed: 2026-04-06T00:45
// Matches constructor at 0x00180e40 / 0x00180de0
// m_Target and m_ShakeDir initialized from _Vector2<float>::Zero (BSS, = 0.0, 0.0)
// Field assignment order matches binary (compiler interleaves reads/writes)
FruitCamera::FruitCamera()
    : m_pFollowEntity(nullptr),
      m_CameraMode(0),
      m_field134(0), m_field136(0),
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
    m_field14c = (float)m_field136;
    m_field150 = (float)m_field134;

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

// Non-virtual (0x001810ac) — perspType 0 (standard)
//
// Matches binary FruitCamera::SetupPerspective (0x00181200):
//   eye    = (m_Target.x, m_Target.y, 1)   ; offset by shake target
//   target = (m_Target.x, m_Target.y, 0)
//   up     = (0, 1, 0)
//   SetupOrtho(160, -160, -240, 240, 2000, -6000)
//
// Axis convention: X ∈ [-160, +160] vertical (short, "top-to-bottom" in landscape),
//                  Y ∈ [-240, +240] horizontal (long).
// MenuButton positions are stored in this centred space directly
// (e.g. Quit at (182, -106)).
//
// Note: the Bada binary multiplies this ortho by a 90° screen-rotation matrix
// via DisplayManager::m_ScreenRotationMatrix to handle its portrait physical
// framebuffer. The SDL port renders natively to a landscape window and does not
// need that rotation — GL's `(top, bottom, left, right)` mapping is already
// correct for our landscape viewport.
void FruitCamera::SetupPerspective(int perspType, bool forceUpdate) {
    (void)perspType;
    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();

    // Cache path
    if (!m_bDirty && !m_bInitialized && !forceUpdate) {
        Matrix44 viewMat44;
        m_localToWorld.ToMatrix44(viewMat44);
        mm.GetViewStack().SetCurrentMatrix(viewMat44);
        mm.GetProjectionStack().SetCurrentMatrix(m_projection);
        mm.UploadAll();
        mm.GetWorldStack().Reset();
        return;
    }

    // View: camera looks straight down +Z with optional shake offset.
    Vec3 eye(m_Target.x, m_Target.y, 1.0f);
    Vec3 at (m_Target.x, m_Target.y, 0.0f);
    Vec3 up (0.0f, 1.0f, 0.0f);
    mm.SetupLookAt(eye, at, up);
    m_localToWorld = Matrix43::FromMatrix44(mm.GetViewStack().m_Current);

    // Projection: literal binary ortho — centred at origin.
    mm.SetupOrtho(160.0f, -160.0f, -240.0f, 240.0f, 2000.0f, -6000.0f);

    // Cache
    m_projection = mm.GetProjectionStack().m_Current;
    m_bDirty = false;
    m_bInitialized = false;

    mm.GetWorldStack().Reset();
}

// 0x00180d10
void FruitCamera::CreateCameraShake(const Vec3& impact, float intensity, float dirScale) {
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
