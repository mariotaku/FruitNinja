#ifndef FN_FRUIT_CAMERA_H
#define FN_FRUIT_CAMERA_H

//
// FruitCamera : MortarCamera (size = 0x16C / 364 bytes)
// Reimplemented from docs/engine/camera.md
// Original: ctor 0x00180de0, UpdateCamera 0x00180c8c,
//           SetupPerspective 0x001810ac, CreateCameraShake 0x00180d10
//

#include "render/MortarCamera.h"
#include "math/Vec3.h"
#include <cstdint>

class FruitCamera : public Mortar::MortarCamera {
public:
    // +0x12c: pointer to followed entity (0 = idle)
    int m_pFollowEntity;

    // +0x130: 0 = idle, 1 = follow
    int m_CameraMode;

    // +0x134, +0x136: angle fields cast to float each frame
    uint16_t m_field134;
    uint16_t m_field136;

    // +0x138: shake direction vector
    float m_ShakeDir_x;
    float m_ShakeDir_y;

    // +0x140: Atan2Idx result from CreateCameraShake
    uint16_t m_ShakeAngle;

    // +0x144, +0x148: camera target (from global config, affected by shake)
    float m_TargetX;
    float m_TargetY;

    // +0x14c, +0x150: float copies of ushort angle fields
    float m_field14c;
    float m_field150;

    // +0x154: |pos - target| magnitude
    float m_DistanceMag;

    // +0x158: lookAt saved each UpdateCamera
    Vec3 m_LookAtSnapshot;

    // +0x164: shake amplitude (decays over time)
    float m_ShakeIntensity;

    // +0x168: initial shake intensity (set by CreateCameraShake)
    float m_ShakeIntensityInit;

    FruitCamera();
    ~FruitCamera();

    // Per-frame update — dispatches to UpdateIdle or UpdateFollow
    void UpdateCamera(float dt);

    // Setup the camera projection for rendering.
    // perspType: 0 = standard (used in GameDraw), 1 = multiplayer P1, 2 = multiplayer P2
    // In the original binary, this calls SetupLookAt + SetupOrtho with
    // hardcoded ortho bounds for the rotated Bada display.
    // Port: uses symmetric ortho centered at (480, 320) to match HUDControl3d offset.
    void SetupPerspective(int perspType = 0, bool forceUpdate = false);

    // Initiate camera shake from impact point
    void CreateCameraShake(const Vec3& impact, float intensity, float dirScale);

    // Per-frame shake update (called from UpdateCamera)
    void UpdateShake(float dt);

private:
    void UpdateIdle(float dt);
    void UpdateFollow(float dt);
    void IdleCamera();
};

#endif
