//
// FruitCamera : MortarCamera (0x1a8 bytes, v1.6.1)
//

#include "FruitCamera.h"
#include "platform/InputEvent.h"
#include "render/MatrixManager.h"
#include "render/DisplayManager.h"
#include "render/Layout.h"
#include "math/MathUtil.h"
#include "math/Random.h"
#include <cmath>
#include <cstdlib>

// ctor @ 0x1edb48 (v1.6.1)
// DIFFERS: v1.5.1 port had m_field150, m_DistanceMag, m_LookAtSnapshot in +0x14c..+0x168 region.
//   v1.6.1 layout collapses those into m_LookAt(Vec3)@+0x150, m_Zoom@+0x15c, m_RollOut@+0x160,
//   m_ZoomT@+0x164, m_ZoomTarget(Vec3)@+0x168, m_ZoomScale@+0x174, m_RollScale@+0x178,
//   and shifts m_ShakeIntensity/m_ShakeIntensityInit to +0x17c/+0x180 before m_OnZoomDone@+0x184.
FruitCamera::FruitCamera()
    : m_pFollowEntity(0),                 // +0x12c str r3(=0)
      m_CameraMode(0),                    // +0x130 str r3(=0)
      m_TiltYaw(0), m_TiltPitch(0),       // +0x134/+0x136 strh r3(=0)
      m_ShakeDir(_Vector2<float>::Zero()),           // +0x138 <- ldmia of `Zero` global @ 0x002d92a0 (Vec2 = 0,0)
      m_ShakeAngle(0), _pad142(0),        // +0x140 NOT written by ctor; zeroed for port determinism
      m_Target(_Vector2<float>::Zero()),             // +0x144 <- same `Zero` global @ 0x002d92a0 (Vec2 = 0,0)
      m_reserved14c(0.0f),                // +0x14c vstr s15 ; s15 = DAT_001edbf0 = 0.0f
      m_LookAt(0.0f, 0.0f, 0.0f),         // +0x150 NOT written by ctor; zeroed for port determinism
      m_Zoom(1.0f),                       // +0x15c vstr s14 = 1.0f (0x3f800000)
      m_RollOut(0), _pad162(0),           // +0x160 strh r3(=0)
      m_ZoomT(0.0f),                      // +0x164 vstr s15 = 0.0f
      m_ZoomTarget(0.0f, 0.0f, 0.0f),     // +0x168 <- ldmia of `Zero` global @ 0x002d9288 (Vec3 = 0,0,0)
      m_ZoomScale(1.0f),                  // +0x174 vstr s14 = 1.0f
      m_RollScale(1.0f),                  // +0x178 NOT written by ctor; 1.0f keeps zoom roll neutral
      m_ShakeIntensity(0.0f),             // +0x17c vstr s15 = 0.0f
      m_ShakeIntensityInit(0.0f),         // +0x180 NOT written by ctor; zeroed for port determinism
      m_OnZoomDone()                      // +0x184 Delegate0<void>::Delegate0
{
    // ctor @ 0x1edb48 (v1.6.1) fully resolved from disassembly:
    //   m_ShakeDir/m_Target  <- `Zero` Vec2 global (GOT @ 0x002d882c -> 0x002d92a0), both (0,0)
    //   m_ZoomTarget         <- `Zero` Vec3 global (GOT @ 0x002d8248 -> 0x002d9288), (0,0,0)
    //   m_reserved14c, m_ZoomT, m_ShakeIntensity  <- s15 = DAT_001edbf0 = 0.0f
    //   m_Zoom, m_ZoomScale  <- s14 = 1.0f
    //   m_TiltYaw/Pitch, m_CameraMode, m_pFollowEntity, m_RollOut  <- 0
    // Fields the binary ctor leaves UNwritten (heap garbage in the binary, but the
    // port zero-inits them for deterministic behaviour): m_ShakeAngle(+0x140),
    // m_LookAt(+0x150), m_RollScale(+0x178), m_ShakeIntensityInit(+0x180).
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
// ASM-spec v1.6.1 FruitCamera::UpdateCamera @0x001edf24: the zoom state machine
// above. Spec marker, not a verify -- the v1.6.1 body has not been ASM-diffed.
void FruitCamera::UpdateCamera(float dt) {
    // RE-ported: 0x1edf24 — v1.6.1 UpdateCamera does NOT write m_reserved14c (+0x14c) at all.
    // The v1.5.1 (float)m_TiltPitch / (float)m_TiltYaw casts were dropped; m_reserved14c retains ctor value.

    UpdateShake(dt);

    // RE-ported: 0x1edf24 — m_LookAt(+0x150) = engine 'Zero' Vec3 global @ 0x002d9288 = (0,0,0).
    // Binary: pfVar5 = *(float**)GOT[0x7118]; ldmia pfVar5,{s?,s?,s?} -> stmia [r4+0x150].
    // Source field is engine Zero (0,0,0), NOT MortarCamera::m_lookAt.
    m_LookAt = _Vector3<float>(0.0f, 0.0f, 0.0f);

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
            m_OnZoomDone();
            m_CameraMode = 0;
        }
        break;
    case 3:
        // Zoom-out: ZoomT += -10*dt, clamp to 0; on done fire Delegate0
        m_ZoomT += -10.0f * dt;
        if (m_ZoomT <= 0.0f) {
            m_ZoomT = 0.0f;
            m_OnZoomDone();
            m_CameraMode = 0;
        }
        break;
    default:
        break;
    }

    // m_Zoom = LerpF(1.0, m_ZoomScale, InverseSquareTransition(ZoomT,0))
    // InverseSquareTransition(t,_) = 2t - t^2  (2nd arg unused, iOS body @ 0x14e8cc)
    {
        float t = m_ZoomT;
        float f = 2.0f * t - t * t;
        m_Zoom = 1.0f + (m_ZoomScale - 1.0f) * f;
    }

    // m_LookAt = LerpF<Vec3>(Zero(0,0,0), m_ZoomTarget, SinTransition(ZoomT,90))
    // RE-ported: 0x1edf24 — base = engine Zero (0,0,0), NOT m_lookAt.
    // SinTransition(t,90): idxMax=(uint16_t)(90*182)=16380; sin(t*16380*2pi/65536)/sin(16380*2pi/65536).
    // DAT_001ee0e4=90.0f (scale), DAT_001ee0e8=182.0f (65536/360 deg->index).
    // den=sin(16380*2pi/65536)~=1.0, so sin(t*pi/2) is faithful to float precision.
    {
        float t = m_ZoomT;
        float f = sinf(t * 1.5707963f);   // SinTransition(t,90): sin(t*pi/2), den~=1
        m_LookAt.x = 0.0f + (m_ZoomTarget.x - 0.0f) * f;
        m_LookAt.y = 0.0f + (m_ZoomTarget.y - 0.0f) * f;
        m_LookAt.z = 0.0f + (m_ZoomTarget.z - 0.0f) * f;
    }

    // m_RollOut = (uint16_t)(int)(LerpF(0, m_RollScale, InverseSquareTransition(ZoomT,0)) * 182.0f)
    // RE-ported: 0x1edf24 — DAT_001ee0e8=182.0f (65536/360, deg->angle-index); was missing in prior port.
    {
        float t = m_ZoomT;
        float roll = (2.0f * t - t * t) * m_RollScale;   // LerpF(0, m_RollScale, IST(t))
        m_RollOut = (uint16_t)(int)(roll * 182.0f);        // 182 = 65536/360
    }

    // Add shake offset to LookAt: Vec3(m_Target.x, m_Target.y, 0) += m_LookAt
    m_LookAt.x += m_Target.x;
    m_LookAt.y += m_Target.y;

    // RE-ported: 0x1edf24 — binary emits `mov r3,#1; strb r3,[r4,#0x108]` unconditionally
    // after the shake-offset block, setting the camera dirty flag every frame so
    // SetupPerspective always recomputes its matrix (not just on pos/lookAt changes).
    m_bDirty = true;
}

// ASM-spec v1.6.1 FruitCamera::TranslatePos @0x001ed840
// view<->world coordinate transform through the current zoom state.
// inverse=false: world->view (subtract lookAt offset, divide by zoom, rotate by +m_RollOut)
// inverse=true:  view->world (rotate by -m_RollOut, multiply by zoom, add lookAt offset)
// useZeroCenter=true: center = (0,0,0); false: center = (m_Target.x, m_Target.y, 0)
// Returns pos unchanged when m_ZoomT <= 0 (not zooming).
_Vector3<float> FruitCamera::TranslatePos(_Vector3<float> pos, bool inverse, bool useZeroCenter)
{
    if (m_ZoomT <= 0.0f) return pos;
    _Vector3<float> center = useZeroCenter
                                 ? _Vector3<float>(0.0f, 0.0f, 0.0f)
                                 : _Vector3<float>(m_Target.x, m_Target.y, 0.0f);
    if (inverse) {
        // view-space -> world-space: RotZ(-m_RollOut) * pos * m_Zoom + (m_LookAt - center)
        uint16_t negIdx = (uint16_t)(-(int)m_RollOut);
        float sinA = Math::SinIdx(negIdx);
        float cosA = Math::CosIdx(negIdx);
        Matrix44 rot;
        rot.RotZ44(sinA, cosA);
        // rot.m[0]=cosA, rot.m[1]=sinA, rot.m[4]=-sinA, rot.m[5]=cosA after RotZ44 on identity
        pos = _Vector3<float>(rot.m[0] * pos.x + rot.m[4] * pos.y,
                              rot.m[1] * pos.x + rot.m[5] * pos.y,
                              pos.z);
        pos = pos * m_Zoom;
        pos = pos + (m_LookAt - center);
    } else {
        // world-space -> view-space: (pos - (m_LookAt - center)) / m_Zoom, then RotZ(+m_RollOut)
        pos = pos - (m_LookAt - center);
        pos = pos / m_Zoom;
        float sinA = Math::SinIdx(m_RollOut);
        float cosA = Math::CosIdx(m_RollOut);
        Matrix44 rot;
        rot.RotZ44(sinA, cosA);
        pos = _Vector3<float>(rot.m[0] * pos.x + rot.m[4] * pos.y,
                              rot.m[1] * pos.x + rot.m[5] * pos.y,
                              pos.z);
    }
    return pos;
}

// ASM-spec v1.6.1 FruitCamera::IdleCamera @0x001ed77c — clears follow entity, mode 0
void FruitCamera::IdleCamera() {
    m_pFollowEntity = 0;
    m_CameraMode = 0;
}

// Empty in idle mode
void FruitCamera::UpdateIdle(float dt) {
    (void)dt;
}

// ASM-spec v1.6.1 FruitCamera::UpdateFollow @0x001ed7ec — delta-preserving follow.
// Semantic (re-analyst): delta = m_lookAt - entity->pos;
//   m_lookAt = entity->pos; m_pos -= delta;
// Net: m_pos += (entity->pos - oldLookAt), preserving (m_pos - m_lookAt).
void FruitCamera::UpdateFollow(float dt) {
    (void)dt;
    if (m_pFollowEntity == 0) {
        IdleCamera();
    } else {
        _Vector3<float> delta = m_lookAt - m_pFollowEntity->pos;
        m_lookAt = m_pFollowEntity->pos;
        m_pos -= delta;
    }
}

// ASM-spec v1.6.1 FruitCamera::FollowEntity @0x001ed78c — bind follow entity (entity
// and mode are stored only when entity != 0, `strne`), always reset tilt to (0,0)
// and up to (0,1,0).
void FruitCamera::FollowEntity(Mortar::Entity* entity) {
    if (entity) {
        m_pFollowEntity = entity;
        m_CameraMode = 1;
    }
    m_TiltYaw   = 0;
    m_TiltPitch = 0;
    m_up = _Vector3<float>(0.0f, 1.0f, 0.0f);
}

// ASM-spec v1.6.1 FruitCamera::GetFollowEntity @0x001ed768 — return m_pFollowEntity iff mode==1
Mortar::Entity* FruitCamera::GetFollowEntity() {
    return (m_CameraMode == 1) ? m_pFollowEntity : 0;
}

// ASM-spec v1.6.1 FruitCamera::SetupPerspective @0x001ee124
// (body 0x001ee124..0x001ee53f; last function in FruitCamera.cpp before that
// file's static-ctor @0x001ee56c). Non-virtual, 5 selector values / 3 arms.
//
// CAUTION: the binary has a SECOND, different function actually named
// `FruitCamera::SetupPerspective` @ 0x00257aac. That one is a true perspective
// setup (reads fovy/aspect/near/far at +0x11c/+0x120/+0x124/+0x128 and calls
// 0x0010dd60); it is NOT this ortho dispatcher. Do not merge the two, and note
// that asm-verify pairs this port body against 0x001ee124 by instruction count.
//
// Structure of the binary body:
//   0x001ee130..0x001ee17c  snapshot zoom(+0x15c) / lookAt(+0x150) / roll(+0x160)
//                           into locals, then `cmp r7,#4` overrides all three for
//                           PT_GENERIC (zoom=1, roll=0, lookAt = engine `Zero` Vec3).
//   0x001ee180              `if (!m_bDirty && !forceUpdate) goto 0x001ee4f8`.
//   0x001ee194..0x001ee1cc  halfH = zoom*160, halfW = zoom*240;
//                           up = Vec3(SinIdx(roll), CosIdx(roll), 0).
//   0x001ee1d4              5-entry jump table -> the three arms below.
//   0x001ee4ac..0x001ee4b8  shared ortho tail (MatrixManager::SetupOrtho, out=0).
//   0x001ee4bc..0x001ee4ec  m_projection <- projection stack m_Current.
//   0x001ee4f8..0x001ee51c  the !dirty early-out: SetCurrentMatrix on view then
//                           projection, NO upload.
//   0x001ee520              MatrixStack::Reset(world stack) -- every path ends here.
//
// The binary writes NEITHER m_bDirty nor m_bInitialized here (only the base
// MortarCamera::SetupPerspective @0x00257aac / ::SetupOrtho @0x00257758 do), and
// the early-out does NOT call UploadAll -- both were port-side additions and are
// removed. Nothing else clears m_bDirty on a FruitCamera, so the early-out is in
// practice unreachable, exactly as in the binary.
void FruitCamera::SetupPerspective(PERSPECIVE_TYPE perspType, bool forceUpdate) {
    MatrixManager& mm = MatrixManager::GetInstance();

    // 0x001ee130: s16 <- m_Zoom; sp+0x124 <- copy of m_LookAt; r6 <- m_RollOut.
    float zoom = m_Zoom;
    _Vector3<float> lookAt = m_LookAt;
    uint16_t rollOut = m_RollOut;

    // 0x001ee154: PT_GENERIC ignores the camera entirely -- screen-space setup.
    // lookAt <- the engine `Zero` Vec3 global (GOT 0x001ee564 -> 0x002d9288).
    if (perspType == PT_GENERIC) {
        zoom    = 1.0f;
        rollOut = 0;
        lookAt  = _Vector3<float>(0.0f, 0.0f, 0.0f);
    }

    // 0x001ee180
    if (!m_bDirty && !forceUpdate) {
        Matrix44 viewMat44;
        m_localToWorld.ToMatrix44(viewMat44);
        mm.GetViewStack().SetCurrentMatrix(viewMat44);
        mm.GetProjectionStack().SetCurrentMatrix(m_projection);
        mm.GetWorldStack().Reset();
        return;
    }

    // 0x001ee198: the ortho half-extents scale with m_Zoom -- this is what makes
    // the zoom state machine in UpdateCamera visible. Literals from the pool at
    // 0x001ee544 (160.0f) and 0x001ee548 (240.0f).
    // DIFFERS: opt-in widescreen (Layout::HalfWidth); faithful 240 under __bada__ --
    // horizontal bounds widen with Layout::HalfWidth() when Layout::g_WideLayout is
    // on (== 240.0f, i.e. the original bounds, otherwise). Vertical stays +-160.
    float halfH = zoom * 160.0f;
#ifdef __bada__
    float halfW = zoom * 240.0f;
#else
    float halfW = zoom * Layout::HalfWidth();
#endif

    // 0x001ee1a8: up = Vec3(SinIdx(m_RollOut), CosIdx(m_RollOut), 0). m_RollOut is
    // the zoom transition's roll in 16-bit angle-index units, so at rest (roll 0)
    // this is exactly (0,1,0).
    _Vector3<float> up(Math::SinIdx(rollOut), Math::CosIdx(rollOut), 0.0f);

    // SetupOrtho takes (top, bottom, left, right, near, far) -- NOT the GL order.
    float top, bottom, left, right;
    _Vector3<float> at, eye;

    switch (perspType) {
    case PT_STANDARD:      // 0x001ee1dc -> 0x001ee1f0
    case PT_STANDARD_2D:   // 0x001ee1e0 -> 0x001ee1f0
    case PT_GENERIC:       // 0x001ee1ec -> 0x001ee1f0
        // 0x001ee1f0: eye = Vec3(0,0,1) + lookAt; at = lookAt; up as computed above.
        at  = lookAt;
        eye = _Vector3<float>(0.0f, 0.0f, 1.0f) + at;
        mm.SetupLookAt(eye, up, at);
        m_localToWorld = Matrix43::FromMatrix44(mm.GetViewStack().m_Current);
        // 0x001ee4ac reached with s0=halfH, s1=-halfH, s2=-halfW, s3=halfW.
        top = halfH; bottom = -halfH; left = -halfW; right = halfW;
        break;

    case PT_ROTATED_CW:    // 0x001ee1e4 -> 0x001ee298
        // 0x001ee298: at = Vec3(-lookAt.y, lookAt.x, lookAt.z) / 1.0f. The divisor
        // is materialised on the stack (`vstr s16,[sp,#0x134]` after
        // `vmov.f32 s16,#1.0`) because operator/ takes a const float&, so the
        // source really does divide by a constant 1.0f here.
        at  = _Vector3<float>(-lookAt.y, lookAt.x, lookAt.z) / 1.0f;
        up  = _Vector3<float>(1.0f, 0.0f, 0.0f);
        eye = _Vector3<float>(0.0f, 0.0f, 1.0f) + at;
        mm.SetupLookAt(eye, up, at);
        m_localToWorld = Matrix43::FromMatrix44(mm.GetViewStack().m_Current);
        // 0x001ee374: s0=DAT_001ee550(-240), s1=DAT_001ee548(240),
        //             s2=DAT_001ee544(160), s3=DAT_001ee554(-480).
        // Both axes are inverted relative to PT_ROTATED_CCW, so with up=(1,0,0)
        // the scene lands rotated 90 deg clockwise. Ignores m_Zoom by design.
        top = -240.0f; bottom = 240.0f; left = 160.0f; right = -480.0f;
        break;

    case PT_ROTATED_CCW:   // 0x001ee1e8 -> 0x001ee3a4
        // 0x001ee3a4: up first, then at = Vec3(lookAt.y, -lookAt.x, lookAt.z) / 1.0f.
        up  = _Vector3<float>(1.0f, 0.0f, 0.0f);
        at  = _Vector3<float>(lookAt.y, -lookAt.x, lookAt.z) / 1.0f;
        eye = _Vector3<float>(0.0f, 0.0f, 1.0f) + at;
        mm.SetupLookAt(eye, up, at);
        m_localToWorld = Matrix43::FromMatrix44(mm.GetViewStack().m_Current);
        // 0x001ee480: s0=DAT_001ee548(240), s1=DAT_001ee550(-240),
        //             s2=DAT_001ee554(-480), s3=DAT_001ee544(160).
        top = 240.0f; bottom = -240.0f; left = -480.0f; right = 160.0f;
        break;

    default:
        // 0x001ee1d8: selector > 4 skips the whole camera setup and falls straight
        // through to the world-stack reset.
        mm.GetWorldStack().Reset();
        return;
    }

    // 0x001ee4ac: shared ortho tail -- s4/s5 from DAT_001ee558 (2000.0f) and
    // DAT_001ee55c (-6000.0f), r1 = 0 (no out-matrix). All three arms `b` here.
    mm.SetupOrtho(top, bottom, left, right, 2000.0f, -6000.0f);

    // 0x001ee4bc
    m_projection = mm.GetProjectionStack().m_Current;

    // 0x001ee520
    mm.GetWorldStack().Reset();
}

// ASM-spec v1.6.1 FruitCamera::ViewIsNormal @0x001d0098:
//   if (m_Target.x == 0 && m_Target.y == 0) return m_ZoomT <= 0; else return false;
// The shake offset counts too -- the prior port checked only m_ZoomT.
bool FruitCamera::ViewIsNormal() {
    if (m_Target.x == 0.0f && m_Target.y == 0.0f) {
        return m_ZoomT <= 0.0f;
    }
    return false;
}

// ASM-spec v1.6.1 FruitCamera::CreateCameraShake @0x001ed9e0 — shake angle from
// impact via Math::Atan2Idx, dir = (CosIdx,SinIdx)*9 then *= dirScale.
// DIFFERS: original = Math::Atan2Idx 16-bit-angle-index trig; port uses
//          atan2f/sinf/cosf with the (radians to 16-bit-index) conversion
//          factor 65536/2pi.
void FruitCamera::CreateCameraShake(_Vector3<float> impact, float intensity, float dirScale) {
    m_ShakeAngle = (uint16_t)(int)(atan2f(impact.y, impact.x) * 65536.0f / 6.2831853f);

    float angle_rad = (float)m_ShakeAngle * 6.2831853f / 65536.0f;
    m_ShakeDir.x = cosf(angle_rad) * 9.0f * dirScale;
    m_ShakeDir.y = sinf(angle_rad) * 9.0f * dirScale;

    m_ShakeIntensityInit = intensity;
    m_ShakeIntensity = intensity;
}

// v1.6.1 FruitCamera::UpdateShake @ 0x001edcc0 (body 0x001edcc0..0x001edf00).
// Constants verified by value from the v1.6.1 literal pool at 0x001edf08.
void FruitCamera::UpdateShake(float dt) {
    static const float LERP_FACTOR    = 0.2f;    // DAT_001edf08
    static const float SNAP_NEG       = -0.01f;  // DAT_001edf0c
    static const float SNAP_POS       = 0.01f;   // DAT_001edf10
    static const float DAMP_FACTOR    = 0.8f;    // DAT_001edf14

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
            // ASM-spec v1.6.1 FruitCamera::UpdateShake @0x001edcc0 (inlined draw
            // @0x001edd64): Math::g_random.Rand32(0x38E0) x1, only when distSq < 16.
            m_ShakeAngle += 0x6388 + (uint16_t)Math::g_Random.Rand32(0x38E0);

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

// StartZoomIn — binary symbol FruitCamera::Transition(_Vector3<float>, float, float, Delegate0<void>) @ 0x1bef54.
// ASM (0x1bef54): str #2 -> [this+0x130]; ldmia target,{x,y,z} -> [this+0x168..0x170];
//   vstr s0 -> [this+0x174] (zoomScale); vstr s1 -> [this+0x178] (rollScale);
//   Delegate0::operator=([this+0x184], onDone) (tail b 0x106068).
// DIFFERS: prior port stub also did `m_ZoomT = 0.0f` — the binary does NOT touch m_ZoomT
//   in Transition; zoom-in resumes from the current m_ZoomT (ctor inits it 0, zoom-out clamps it to 0).
void FruitCamera::StartZoomIn(const _Vector3<float>& target, float zoomScale, float rollScale,
                              Mortar::Delegate0<void> onDone) {
    m_CameraMode = 2;
    m_ZoomScale  = zoomScale;
    m_RollScale  = rollScale;
    m_ZoomTarget = target;
    m_OnZoomDone = onDone;
}

// StartZoomOut — binary symbol FruitCamera::TransitionOut() @ 0x1bede8.
// ASM (0x1bede8): mov r3,#3; str r3,[r0,#0x130]; bx lr.
// DIFFERS: prior port stub took a Delegate0 onDone and set m_OnZoomDone + m_ZoomT=1.0;
//   the binary TransitionOut takes NO arguments and writes ONLY m_CameraMode=3.
//   (m_ZoomT is left as-is — typically 1.0 from a completed zoom-in — and the existing
//    m_OnZoomDone callback is reused/fires when zoom-out reaches 0 in UpdateCamera.)
void FruitCamera::StartZoomOut() {
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
