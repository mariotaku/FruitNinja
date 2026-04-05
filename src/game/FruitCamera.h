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
#include "math/Vec2.h"
#include "math/Vec3.h"
#include <cstdint>

// Analysed: 2026-04-06T00:45

class FruitCamera : public Mortar::MortarCamera {
public:
    // +0x12C: entity pointer for follow mode (NULL = none)
    void* m_pFollowEntity;         // MortarEntity* in original

    // +0x130: 0 = idle, 1 = follow
    int m_CameraMode;

    // +0x134, +0x136: angle ushorts, cast to float each UpdateCamera frame
    uint16_t m_field134;
    uint16_t m_field136;

    // +0x138: shake direction vector (from impact angle, decays with intensity)
    Vec2 m_ShakeDir;

    // +0x140: Atan2Idx result from CreateCameraShake
    uint16_t m_ShakeAngle;
    uint16_t _pad142;

    // +0x144: camera target position (shake lerps toward m_ShakeDir)
    // Initialized to (0.0, 0.0) from g_FruitCameraDefaultPos in BSS
    Vec2 m_Target;

    // +0x14C, +0x150: float copies of ushort angle fields
    float m_field14c;
    float m_field150;

    // +0x154: |pos - target| magnitude, computed each frame
    float m_DistanceMag;

    // +0x158: lookAt saved each UpdateCamera frame
    Vec3 m_LookAtSnapshot;

    // +0x164: shake amplitude (decays linearly by dt each frame)
    float m_ShakeIntensity;

    // +0x168: initial shake intensity (for ratio calculations in UpdateShake)
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
