#ifndef FN_FRUIT_CAMERA_H
#define FN_FRUIT_CAMERA_H

//
// FruitCamera : MortarCamera (size = 0x16C / 364 bytes)
//
// Only slot 3 (UpdateCamera) is overridden from MortarCamera vtable.
// FruitCamera::SetupPerspective(perspType, forceUpdate) at 0x001810ac is a
// SEPARATE non-virtual method called directly from GameDraw.
//

#include "render/MortarCamera.h"
#include "math/Vec2.h"
#include "math/Vec3.h"
#include <cstdint>

struct InputEvent;

// Analysed: 2026-05-04T00:00

// Binary enum from switch in SetupPerspective (0x001810ac).
// Typo in original binary symbol preserved intentionally.
enum PERSPECIVE_TYPE {
    PT_STANDARD    = 0,
    PT_ROTATED_CW  = 1,
    PT_ROTATED_CCW = 2,
    PT_GENERIC     = 3,
};

class FruitCamera : public Mortar::MortarCamera {
public:
    // +0x12C: entity pointer for follow mode (nullptr = none)
    void* m_pFollowEntity;         // Entity* in original

    // +0x130: 0 = idle, 1 = follow
    int m_CameraMode;

    // +0x134: orbit tilt yaw angle (uint16 fixed-point, full circle = 0x10000)
    // Binary @ 0x00181400 / 0x0018151c — DebugTiltLeft/Right mutate this field
    uint16_t m_TiltYaw;

    // +0x136: orbit tilt pitch angle (uint16 fixed-point)
    // Binary @ 0x00181638 / 0x00181754 — DebugTiltDown/Up mutate this field
    uint16_t m_TiltPitch;

    // +0x138: shake direction vector (from impact angle, decays with intensity)
    Vec2 m_ShakeDir;

    // +0x140: Atan2Idx result from CreateCameraShake
    uint16_t m_ShakeAngle;
    uint16_t _pad142;

    // +0x144: camera target position (shake lerps toward m_ShakeDir)
    // Initialized to (0.0, 0.0) from g_FruitCameraDefaultPos in BSS
    Vec2 m_Target;

    // +0x14C, +0x150: float copies of ushort angle fields (cast each UpdateCamera)
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
    void UpdateCamera(float dt);

    // Non-virtual (0x001810ac) — 4-type ortho dispatch
    // Called from GameDraw as SetupPerspective(PT_STANDARD, false)
    void SetupPerspective(PERSPECIVE_TYPE perspType = PT_STANDARD, bool forceUpdate = false);

    // Binary @ 0x00180d10 — shake angle from impact, dir = (cos,sin)*9*dirScale
    void CreateCameraShake(const Vec3& impact, float intensity, float dirScale);

    // 0x00180ea0
    void UpdateShake(float dt);

    // Binary @ 0x00180b2c — bind follow entity, reset tilt to (0,0), up=(0,1,0)
    void FollowEntity(void* entity);

    // Binary @ 0x00180a0c — return m_pFollowEntity iff mode==1
    void* GetFollowEntity() const;

    // --- Debug input handlers (binary @ addresses below) ---
    // All dead in retail binary (no caller registers them). Defunct: debug input.
    // Bodies preserved as working debug-fly for screenshot use.

    // Binary @ 0x00180a2c — debug pan +Y by 10
    bool DebugFlyUp(InputEvent* e);
    // Binary @ 0x00180a6c — debug pan -Y by 10
    bool DebugFlyDown(InputEvent* e);
    // Binary @ 0x00180aac — debug pan -X by 10
    bool DebugFlyLeft(InputEvent* e);
    // Binary @ 0x00180aec — debug pan +X by 10
    bool DebugFlyRight(InputEvent* e);
    // Binary @ 0x0018151c — orbit yaw += +0x96
    bool DebugTiltLeft(InputEvent* e);
    // Binary @ 0x00181400 — orbit yaw += -0x96
    bool DebugTiltRight(InputEvent* e);
    // Binary @ 0x00181638 — orbit pitch += -0x96
    bool DebugTiltDown(InputEvent* e);
    // Binary @ 0x00181754 — orbit pitch += +0x96
    bool DebugTiltUp(InputEvent* e);
    // Binary @ 0x00180b70 — debug zoom in: pos = lookAt + (pos-lookAt)*0.99
    bool DebugZoomDown(InputEvent* e);
    // Binary @ 0x00180be0 — debug zoom out: pos = lookAt + (pos-lookAt)*1.01
    bool DebugZoomUp(InputEvent* e);

private:
    void UpdateIdle(float dt);
    void UpdateFollow(float dt);
    void IdleCamera();
};

#endif
