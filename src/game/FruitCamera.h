#ifndef FN_FRUIT_CAMERA_H
#define FN_FRUIT_CAMERA_H

//
// FruitCamera : MortarCamera (size = 0x1a8 / 424 bytes)
//
// v1.6.1 ground truth: GameInitialise @ 0x1d6c4: `operator_new(0x1a8)`.
// Only slot 3 (UpdateCamera) is overridden from MortarCamera vtable.
// FruitCamera::SetupPerspective(perspType, forceUpdate) at 0x001ee124 is a
// SEPARATE non-virtual method called directly from GameDraw.
//

#include "render/MortarCamera.h"
#include "math/_Vector2.h"
#include "math/_Vector3.h"
#include "util/Delegate.h"
#include "entities/Entity.h"
#include <cstdint>

struct InputEvent;

class FruitCamera : public Mortar::MortarCamera {
public:
    // Binary enum from the switch in SetupPerspective @ 0x001ee124 (v1.6.1); nested inside
    // FruitCamera in the binary (mangles N11FruitCamera15PERSPECIVE_TYPEE) -- must stay
    // nested here, not global, for SetupPerspective's mangled symbol to pair.
    // Typo in original binary symbol preserved intentionally.
    //
    // ASM-spec v1.6.1 FruitCamera::SetupPerspective @0x001ee124: the jump table at
    // 0x001ee1d4 has FIVE entries (`cmp r7,#4; addls pc,pc,r7,lsl #2`) resolving to
    // THREE distinct arms plus a default:
    //   0 -> 0x001ee1f0 | 1 -> 0x001ee1f0 | 2 -> 0x001ee298 | 3 -> 0x001ee3a4
    //   4 -> 0x001ee1f0 | >4 -> 0x001ee520 (no camera setup at all)
    // 0, 1 and 4 share one arm; 4 additionally zeroes lookAt/roll and forces zoom=1
    // in the pre-switch block at 0x001ee164.
    //
    // 0 and 1 are byte-for-byte the same code path in v1.6.1 -- nothing distinguishes
    // them but caller intent. Names below come from the v1.6.1 GameDraw @0x001cd7a0
    // call sites (r1 = the value, r2 = forceUpdate, always 1):
    //   0 : depth-on 3D passes  (ActorManager::Draw, FruitRay, DrawSlices, particles)
    //   1 : depth-off 2D passes (DrawBackground, HUD 0x40 + splats/shadows/blasts)
    //   4 : screen-space passes (DrawStartFade, the 16 blade draws, HUD 0x01/0x08)
    // 2 and 3 have NO v1.6.1 call site (GameDraw and DrawStartFade are the only
    // callers and pass only 0/1/4) -- their arms are ported for shape, not reachability.
    enum PERSPECIVE_TYPE {
        PT_STANDARD    = 0,
        PT_STANDARD_2D = 1,
        PT_ROTATED_CW  = 2,
        PT_ROTATED_CCW = 3,
        PT_GENERIC     = 4,
    };

    // +0x12C: entity pointer for follow mode (nullptr = none).
    // v1.6.1 @ +0x12c confirmed.
    Mortar::Entity* m_pFollowEntity;

    // +0x130: 0 = idle, 1 = follow, 2 = zoom-in, 3 = zoom-out.
    // v1.6.1: modes 2/3 are the new zoom states.
    int m_CameraMode;

    // +0x134: orbit tilt yaw angle (uint16 fixed-point, full circle = 0x10000)
    uint16_t m_TiltYaw;

    // +0x136: orbit tilt pitch angle (uint16 fixed-point)
    uint16_t m_TiltPitch;

    // +0x138: shake direction vector (from impact angle, decays with intensity)
    _Vector2<float> m_ShakeDir;

    // +0x140: Atan2Idx result from CreateCameraShake
    uint16_t m_ShakeAngle;
    uint16_t _pad142;

    // +0x144: camera target position (shake lerps toward m_ShakeDir)
    _Vector2<float> m_Target;

    // +0x14C: written 0.0f by ctor; v1.6.1 UpdateCamera @ 0x1edf24 does NOT write
    // or read it (the v1.5.1 (float)m_TiltPitch/(float)m_TiltYaw casts were dropped).
    // Semantic unresolved in v1.6.1. Note: v1.5.1 had m_field14c + m_field150 as a
    // pair; v1.6.1 only lists one field here at +0x14C. Reserved.
    float m_reserved14c;  // purpose unknown

    // +0x150: lookAt saved each UpdateCamera frame.
    // v1.6.1 @ +0x150 (3 floats = Vec3, 12B).
    // DIFFERS: v1.5.1 port had m_field150 (float) + m_DistanceMag (float) + m_LookAtSnapshot (Vec3)
    //   in this region. v1.6.1 collapses to m_LookAt (Vec3) at +0x150 per spec.
    _Vector3<float> m_LookAt;

    // +0x15C: zoom scale applied to perspective. Lerped by zoom state machine. = 1.0 in ctor.
    float m_Zoom;

    // +0x160: roll-out value (short, converted via Lerp from m_RollScale). = 0 in ctor.
    uint16_t m_RollOut;
    uint16_t _pad162;

    // +0x164: zoom transition timer. Increments at 3*dt (zoom-in) or -10*dt (zoom-out).
    float m_ZoomT;

    // +0x168: zoom-target lookAt position (Vec3). = DAT vec in ctor.
    _Vector3<float> m_ZoomTarget;

    // +0x174: zoom depth scale. = 1.0 in ctor.
    float m_ZoomScale;

    // +0x178: roll scale. = 1.0 in ctor.
    float m_RollScale;

    // +0x17C: m_ShakeIntensity / m_ShakeIntensityInit — confirmed from ctor @ 0x1edb48:
    //   m_ShakeIntensity <- s15 = 0.0f; m_ShakeIntensityInit NOT written by ctor (heap garbage
    //   in binary, zeroed here for determinism). Delegate0<void> follows at +0x184.
    float m_ShakeIntensity;       // +0x17C (was +0x164 in v1.5.1 port; relocated in v1.6.1)
    float m_ShakeIntensityInit;   // +0x180 (was +0x168 in v1.5.1 port; relocated in v1.6.1)

    // +0x184: callback fired when zoom transition completes (zoom-in at ZoomT==1, zoom-out at ZoomT==0).
    // Binary: Delegate0::Delegate0 ctor'd at offset +0x184, sizeof Delegate0<void> = 36 bytes -> +0x1A7.
    // Total = 0x1A8. (36B Delegate0<void> = 0x24B on 32-bit ARM = matches 0x184+0x24=0x1A8)
    Mortar::Delegate0<void> m_OnZoomDone;

    FruitCamera();
    ~FruitCamera();

    // Vtable slot 3 override (0x1edf24, v1.6.1)
    // 4-state: 0=idle, 1=follow, 2=zoom-in, 3=zoom-out + zoom lerp + shake.
    void UpdateCamera(float dt);

    // Non-virtual; v1.6.1 FruitCamera::SetupPerspective @0x001ee124 — 5-value / 3-arm ortho dispatch
    void SetupPerspective(PERSPECIVE_TYPE perspType = PT_STANDARD, bool forceUpdate = false);

    // v1.6.1 FruitCamera::CreateCameraShake @0x001ed9e0 — shake angle from impact,
    // dir = (CosIdx,SinIdx)*9 then *= dirScale
    void CreateCameraShake(_Vector3<float> impact, float intensity, float dirScale);

    // v1.6.1 FruitCamera::UpdateShake @0x001edcc0
    void UpdateShake(float dt);

    // v1.6.1 FruitCamera::FollowEntity @0x001ed78c — bind follow entity, reset tilt to (0,0), up=(0,1,0)
    void FollowEntity(Mortar::Entity* entity);

    // v1.6.1 FruitCamera::GetFollowEntity @0x001ed768 — return m_pFollowEntity iff mode==1
    Mortar::Entity* GetFollowEntity();

    // v1.6.1 FruitCamera::IdleCamera @0x001ed77c (clears follow entity, sets mode 0)
    void IdleCamera();

    // ASM-spec v1.6.1 FruitCamera::TranslatePos @0x001ed840: view<->world;
    // inverse=view->world = RotZ(-m_RollOut)*pos*m_Zoom + (m_LookAt-center);
    // no-op when m_ZoomT<=0.
    _Vector3<float> TranslatePos(_Vector3<float> pos, bool inverse, bool useZeroCenter);

    // ASM-spec v1.6.1 FruitCamera::ViewIsNormal @0x001d0098:
    //   m_Target.x == 0 && m_Target.y == 0 && m_ZoomT <= 0
    // i.e. neither shaking nor zooming. Gates DrawBackground's 3x3 UV-seam path.
    bool ViewIsNormal();

    // Zoom API. Binary symbols: FruitCamera::Transition @ 0x1bef54 (zoom-in),
    // FruitCamera::TransitionOut @ 0x1bede8 (zoom-out). UpdateCamera @ 0x1edf24 runs the lerp.
    // StartZoomIn = Transition(target, zoomScale, rollScale, onDone): mode=2, sets ZoomScale/
    //   RollScale/ZoomTarget/OnZoomDone. Does NOT reset m_ZoomT.
    void StartZoomIn(const _Vector3<float>& target, float zoomScale, float rollScale,
                     Mortar::Delegate0<void> onDone);
    // StartZoomOut = TransitionOut(): no args, sets ONLY m_CameraMode=3.
    void StartZoomOut();

    // ASM-spec v1.6.1 FruitCamera::IsTransitionIn @0x001bedf4: m_CameraMode==2.
    bool IsTransitionIn() const { return m_CameraMode == 2; }

    // --- Debug input handlers (binary @ addresses below) ---
    // All dead in retail binary. Bodies preserved as working debug-fly.

    bool DebugFlyUp(InputEvent* e);
    bool DebugFlyDown(InputEvent* e);
    bool DebugFlyLeft(InputEvent* e);
    bool DebugFlyRight(InputEvent* e);
    bool DebugTiltLeft(InputEvent* e);
    bool DebugTiltRight(InputEvent* e);
    bool DebugTiltDown(InputEvent* e);
    bool DebugTiltUp(InputEvent* e);
    bool DebugZoomDown(InputEvent* e);
    bool DebugZoomUp(InputEvent* e);

private:
    void UpdateIdle(float dt);
    void UpdateFollow(float dt);
};

#ifdef __bada__
// v1.6.1 binary layout asserts (cross-build only, 32-bit ARM).
static_assert(__builtin_offsetof(FruitCamera, m_pFollowEntity)  == 0x12C, "m_pFollowEntity wrong");
static_assert(__builtin_offsetof(FruitCamera, m_CameraMode)     == 0x130, "m_CameraMode wrong");
static_assert(__builtin_offsetof(FruitCamera, m_TiltYaw)        == 0x134, "m_TiltYaw wrong");
static_assert(__builtin_offsetof(FruitCamera, m_TiltPitch)      == 0x136, "m_TiltPitch wrong");
static_assert(__builtin_offsetof(FruitCamera, m_ShakeDir)       == 0x138, "m_ShakeDir wrong");
static_assert(__builtin_offsetof(FruitCamera, m_ShakeAngle)     == 0x140, "m_ShakeAngle wrong");
static_assert(__builtin_offsetof(FruitCamera, m_Target)         == 0x144, "m_Target wrong");
static_assert(__builtin_offsetof(FruitCamera, m_reserved14c)    == 0x14C, "m_reserved14c wrong");
static_assert(__builtin_offsetof(FruitCamera, m_LookAt)         == 0x150, "m_LookAt wrong");
static_assert(__builtin_offsetof(FruitCamera, m_Zoom)           == 0x15C, "m_Zoom wrong");
static_assert(__builtin_offsetof(FruitCamera, m_RollOut)        == 0x160, "m_RollOut wrong");
static_assert(__builtin_offsetof(FruitCamera, m_ZoomT)          == 0x164, "m_ZoomT wrong");
static_assert(__builtin_offsetof(FruitCamera, m_ZoomTarget)     == 0x168, "m_ZoomTarget wrong");
static_assert(__builtin_offsetof(FruitCamera, m_ZoomScale)      == 0x174, "m_ZoomScale wrong");
static_assert(__builtin_offsetof(FruitCamera, m_RollScale)      == 0x178, "m_RollScale wrong");
static_assert(__builtin_offsetof(FruitCamera, m_OnZoomDone)     == 0x184, "m_OnZoomDone wrong");
static_assert(sizeof(FruitCamera)                               == 0x1A8, "sizeof(FruitCamera) wrong");
#endif

#endif
