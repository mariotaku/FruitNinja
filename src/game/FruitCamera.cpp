//
// FruitCamera — reimplemented from docs/engine/camera.md
// Original: ctor 0x00180de0, SetupPerspective 0x001810ac
//
// Key difference from original: the Bada binary uses asymmetric ortho
// (240, -240, -480, 160) to compensate for portrait→landscape rotation.
// The SDL port runs landscape-native, so we use a symmetric ortho centered
// at (480, 320) to match HUDControl3d::Draw's Vec3(480, 320, 0) offset.
//

#include "FruitCamera.h"
#include "render/MatrixManager.h"
#include "render/DisplayManager.h"
#include <cmath>
#include <cstdio>

// Matches 0x00180de0
FruitCamera::FruitCamera()
    : m_pFollowEntity(0),
      m_CameraMode(0),
      m_field134(0), m_field136(0),
      m_ShakeDir_x(0.0f), m_ShakeDir_y(0.0f),
      m_ShakeAngle(0),
      m_TargetX(0.0f), m_TargetY(0.0f),
      m_field14c(0.0f), m_field150(0.0f),
      m_DistanceMag(0.0f),
      m_ShakeIntensity(0.0f), m_ShakeIntensityInit(0.0f)
{
}

FruitCamera::~FruitCamera() {
}

// Matches 0x00180c8c
void FruitCamera::UpdateCamera(float dt) {
    m_field14c = (float)m_field136;
    m_field150 = (float)m_field134;

    m_LookAtSnapshot = m_lookAt;

    UpdateShake(dt);

    if (m_CameraMode == 0)
        UpdateIdle(dt);
    else if (m_CameraMode == 1)
        UpdateFollow(dt);
}

// Matches 0x00180a08 — empty in idle mode
void FruitCamera::UpdateIdle(float dt) {
    (void)dt;
}

// Matches 0x00180c50
void FruitCamera::UpdateFollow(float dt) {
    (void)dt;
    if (m_pFollowEntity == 0) {
        IdleCamera();
    }
    // TODO: follow entity position when entity system is wired
}

// Matches 0x00180a20
void FruitCamera::IdleCamera() {
    m_pFollowEntity = 0;
    m_CameraMode = 0;
}

// Matches FruitCamera::SetupPerspective (0x001810ac) for perspType==0
//
// Original Bada binary: the device is portrait 480×800, game renders landscape.
// The ortho params (240, -240, -480, 160) compensate for this rotation.
// The view matrix from SetupLookAt(eye=(targetX, targetY, 1), ...) shifts
// the camera to center on the HUD offset point (480, 320).
//
// Port: SDL window is already landscape 480×320 (or scaled).
// We set up a symmetric ortho + LookAt that makes HUDControl3d's
// Vec3(480, 320, 0) offset map to screen center.
void FruitCamera::SetupPerspective(int perspType, bool forceUpdate) {
    (void)perspType;
    (void)forceUpdate;

    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();
    Mortar::MortarRectangle rect = Mortar::DisplayManager::GetInstance().GetWindowSize();
    float w = (float)rect.Width();   // 480
    float h = (float)rect.Height();  // 320

    // Original Bada: SetupLookAt(eye=(targetX,targetY,1), target=(0,1,0), up=(targetX,targetY,0))
    // The view matrix shifts the scene based on camera shake target.
    // For the port: the ortho is already centered at (480,320), so we use identity view
    // and apply shake offset directly to the ortho center instead.
    //
    // When shake is active, shift the ortho center by (m_TargetX, m_TargetY).
    // When inactive, targets decay to 0 → no shift.
    mm.GetViewStack().Reset();

    // Cache view matrix
    m_viewMatrix = mm.GetViewStack().m_Current;

    // Ortho projection: 480×320 units centered at (480 + shakeX, 320 + shakeY).
    // HUDControl3d adds Vec3(480, 320, 0) to positions, so HUD (0,0) = screen center.
    // Shake shifts the ortho center, creating the camera shake effect.
    float cx = w + m_TargetX;       // 480 + shake offset X
    float cy = h + m_TargetY;       // 320 + shake offset Y
    float hw = w / 2.0f;            // 240
    float hh = h / 2.0f;            // 160

    float top    = cy + hh;         // center + half height
    float bottom = cy - hh;         // center - half height
    float left   = cx - hw;         // center - half width
    float right  = cx + hw;         // center + half width

    mm.SetupOrtho(top, bottom, left, right, 2000.0f, -6000.0f);

    // Cache projection matrix
    m_projection = mm.GetProjectionStack().m_Current;

    // Reset world stack (matches original: last line of SetupPerspective)
    mm.GetWorldStack().Reset();
}

// Matches 0x00180d10
void FruitCamera::CreateCameraShake(const Vec3& impact, float intensity, float dirScale) {
    // Compute shake direction from impact position
    m_ShakeAngle = (uint16_t)(int)(atan2f(impact.y, impact.x) * 65536.0f / 6.2831853f);

    float angle_rad = (float)m_ShakeAngle * 6.2831853f / 65536.0f;
    m_ShakeDir_x = cosf(angle_rad) * 9.0f * dirScale;
    m_ShakeDir_y = sinf(angle_rad) * 9.0f * dirScale;

    m_ShakeIntensityInit = intensity;
    m_ShakeIntensity = intensity;
}

// Matches 0x00180ea0
void FruitCamera::UpdateShake(float dt) {
    static const float DAMP_FACTOR = 0.9f;
    static const float SNAP_THRESHOLD = 0.1f;

    if (m_ShakeIntensity <= 0.0f) {
        // No active shake: decay target back to zero
        if (fabsf(m_TargetX) <= SNAP_THRESHOLD)
            m_TargetX = 0.0f;
        else
            m_TargetX *= DAMP_FACTOR;

        if (fabsf(m_TargetY) <= SNAP_THRESHOLD)
            m_TargetY = 0.0f;
        else
            m_TargetY *= DAMP_FACTOR;
    } else {
        // Active shake
        m_ShakeIntensity -= dt;

        // Check if target reached shake direction
        float dx = m_TargetX - m_ShakeDir_x;
        float dy = m_TargetY - m_ShakeDir_y;
        if (dx * dx + dy * dy < 16.0f) {
            // Randomize shake angle
            m_ShakeAngle += 0x6388 + (uint16_t)(rand() % 0x38E0);

            float ratio = (m_ShakeIntensityInit > 0.0f) ?
                (m_ShakeIntensity / m_ShakeIntensityInit) : 0.0f;
            float scale = ratio * 9.0f;
            float angle_rad = (float)m_ShakeAngle * 6.2831853f / 65536.0f;
            m_ShakeDir_x = cosf(angle_rad) * scale;
            m_ShakeDir_y = sinf(angle_rad) * scale;
        }

        // Move target toward shake direction
        float ratio = (m_ShakeIntensityInit > 0.0f) ?
            (m_ShakeIntensity / m_ShakeIntensityInit) : 0.0f;
        float t = ratio + 1.0f;
        float lerp = 0.25f;  // DAT_lerp approximation
        m_TargetX += (m_ShakeDir_x - m_TargetX) * lerp * t;
        m_TargetY += (m_ShakeDir_y - m_TargetY) * lerp * t;

        m_bDirty = true;
    }
}
