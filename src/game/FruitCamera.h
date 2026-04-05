#ifndef FN_FRUIT_CAMERA_H
#define FN_FRUIT_CAMERA_H

//
// FruitCamera : MortarCamera (size = 0x16C / 364 bytes)
// See docs/engine/camera.md for full layout, vtable, and method decompilation.
//
// Only slot 3 (UpdateCamera) is overridden from MortarCamera vtable.
// FruitCamera::SetupPerspective(perspType, forceUpdate) at 0x001810ac is a
// SEPARATE non-virtual method called directly from GameDraw.
//

#include "render/MortarCamera.h"
#include "math/Vec3.h"
#include <cstdint>

class FruitCamera : public Mortar::MortarCamera {
public:
    // +0x12c: entity pointer for follow mode (0 = none)
    int m_pFollowEntity;

    // +0x130: 0 = idle, 1 = follow
    int m_CameraMode;

    // +0x134, +0x136: angle ushorts cast to float each frame
    uint16_t m_field134;
    uint16_t m_field136;

    // +0x138: shake direction vector
    float m_ShakeDir_x;
    float m_ShakeDir_y;

    // +0x140: Atan2Idx result from CreateCameraShake
    uint16_t m_ShakeAngle;

    // +0x144, +0x148: camera target (shake modifies these)
    float m_TargetX;
    float m_TargetY;

    // +0x14c, +0x150: float copies of ushort angle fields
    float m_field14c;
    float m_field150;

    // +0x154: |pos - target| magnitude
    float m_DistanceMag;

    // +0x158: lookAt saved each UpdateCamera
    Vec3 m_LookAtSnapshot;

    // +0x164: shake amplitude (decays linearly by dt)
    float m_ShakeIntensity;

    // +0x168: initial shake intensity (for ratio calculations)
    float m_ShakeIntensityInit;

    FruitCamera();
    ~FruitCamera();

    // Vtable slot 3 override (0x00180c8c)
    void UpdateCamera(float dt) override;

    // Non-virtual (0x001810ac) — 4-type ortho dispatch
    // Called from GameDraw as SetupPerspective(0, false)
    void SetupPerspective(int perspType = 0, bool forceUpdate = false);

    // 0x00180d10
    void CreateCameraShake(const Vec3& impact, float intensity, float dirScale);

    // 0x00180ea0
    void UpdateShake(float dt);

private:
    void UpdateIdle(float dt);
    void UpdateFollow(float dt);
    void IdleCamera();
};

#endif
