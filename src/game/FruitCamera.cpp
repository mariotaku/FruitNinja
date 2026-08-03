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
// ASM-verified: 2026-05-17 v1.6.1 binary @ 0x00180c8c..0x00180d0e (re-analyst) [v1.5.1].
// v1.6.1 UpdateCamera at 0x1edf24 extends this with the zoom state machine.
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
        _Vector3<float> delta = m_lookAt - m_pFollowEntity->pos;
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
    m_up = _Vector3<float>(0.0f, 1.0f, 0.0f);
}

// Binary @ 0x00180a0c — return m_pFollowEntity iff mode==1
Mortar::Entity* FruitCamera::GetFollowEntity() {
    return (m_CameraMode == 1) ? m_pFollowEntity : 0;
}

// Non-virtual (0x001ee124) — 4-type ortho dispatch.
//
// ASM-spec v1.6.1 SetupPerspective @ 0x001ee124 (body 0x001ee124..0x001ee53f,
// 271 instructions; last function in FruitCamera.cpp before that file's
// static-ctor @ 0x001ee56c).
// The port compiles to 186 against the binary's ~274. Of the two candidate
// causes previously listed, a disassembly read settles both:
//   (1) REFUTED — the binary DOES have the m_bDirty early-out. At 0x001ee180:
//       `ldrb r3,[r5,#0x108]; cmp r3,#0; bne 0x001ee194; cmp r10,#0;
//        beq 0x001ee4f8`, i.e. exactly `if (!m_bDirty && !forceUpdate)`, with
//       0x001ee4f8 as the cheap re-upload tail. Not the source of the delta.
//   (2) CONFIRMED — this is the whole gap. The binary dispatches through a
//       jump table at 0x001ee1d4 (`cmp r7,#4; addls pc,pc,r7,lsl #2`) with five
//       entries resolving to THREE distinct bodies (0x001ee1f0, 0x001ee298,
//       0x001ee3a4) plus a default. The port branches only PT_GENERIC vs
//       everything-else, so it collapses two of the three arms.
// Port the two missing arms before re-stamping.
//
// CAUTION: the binary has a SECOND, different function actually named
// `FruitCamera::SetupPerspective` @ 0x00257aac. That one is a true perspective
// setup (reads fovy/aspect/near/far at +0x11c/+0x120/+0x124/+0x128 and calls
// 0x0010dd60); it is NOT this ortho dispatcher. Do not merge the two, and note
// that asm-verify pairs this port body against 0x001ee124 by instruction count.
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

    _Vector3<float> eye, at;
    if (perspType == PT_GENERIC) {
        eye = _Vector3<float>(0.0f, 0.0f, 1.0f);
        at  = _Vector3<float>(0.0f, 0.0f, 0.0f);
    } else {
        eye = _Vector3<float>(m_Target.x, m_Target.y, 1.0f);
        at  = _Vector3<float>(m_Target.x, m_Target.y, 0.0f);
    }
    _Vector3<float> up(0.0f, 1.0f, 0.0f);
    mm.SetupLookAt(eye, up, at);
    m_localToWorld = Matrix43::FromMatrix44(mm.GetViewStack().m_Current);

    // ASM-spec v1.6.1 SetupPerspective @ 0x001ee4ac..0x001ee4b8 -- the shared
    // ortho tail of the function above: two vldr.32 into s4/s5, r1=0, then
    // bl 0x001129ac (= MatrixManager::SetupOrtho). All three dispatch arms
    // converge here via `b 0x001ee4ac`.
    // DIFFERS: opt-in widescreen (Layout::HalfWidth); faithful 240 under __bada__ --
    // horizontal bounds widen with Layout::HalfWidth() when Layout::g_WideLayout is
    // on (== 240.0f, i.e. the original bounds, otherwise). Vertical stays +-160.
#ifdef __bada__
    mm.SetupOrtho(160.0f, -160.0f, -240.0f, 240.0f, 2000.0f, -6000.0f);
#else
    mm.SetupOrtho(160.0f, -160.0f, -Layout::HalfWidth(), Layout::HalfWidth(), 2000.0f, -6000.0f);
#endif

    m_projection = mm.GetProjectionStack().m_Current;
    m_bDirty = false;
    m_bInitialized = false;

    mm.GetWorldStack().Reset();
}

// ASM-spec v1.6.1 FruitCamera::ViewIsNormal @<addr TBD>
// Assumed predicate: m_ZoomT<=0.0f (view not zoomed/rotated).
// Consistent with TranslatePos no-op gate. TODO: verify exact predicate in binary.
bool FruitCamera::ViewIsNormal() {
    return m_ZoomT <= 0.0f;
}

// Binary @ 0x00180d10 — shake angle from impact, dir = (cos,sin)*9*dirScale
// ASM-verified: 2026-05-17 v1.6.1 binary @ 0x00180d10..0x00180d68 (re-analyst).
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
// (The old 0x00180ea0 / 0x00181068 citations were v1.5.1 residue — in v1.6.1
// 0x00180ea0 falls inside FruitFactZenPage::Init. The pool moved 0x00181068 ->
// 0x001edf08; the four floats kept their order and values.)
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
