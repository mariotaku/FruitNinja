//
// Jiblet : Mortar::Entity
// Binary: ctor 0x1d9580 · Init 0x1e50c0 · Update 0x1e5330 · PostUpdate(empty) 0x1e5c04
//

#include "Jiblet.h"
#include "SplatEntity.h"
#include "FruitInfo.h"
#include "math/Quaternion.h"
#include "engine/math/MathUtil.h"
#include "engine/math/Random.h"
#include "particle/PSPParticleManager.h"

struct Renderer;

// Binary literal constants from Jiblet functions
// (exact DAT addresses from v1.6.1 spec)

// Update @ 0x1e5330
// m_Age gate for emitter creation (DAT_001e5728 = 0x3d4ccccd = 0.05)
static const float JIB_EMITTER_AGE_GATE = 0.05f;

// Spin rate scale used in CreateFromAxisAngle (quaternion integrator).
// DAT_001e572c = 0x43360000 = 182.0 (= 65536/360).
// Spin rates are DEGREES-per-frame; 182 converts to 16-bit angle index.
static const float JIB_SPIN_TO_ANGLE16 = 182.0f;

// T_796 @ 0x1e508c: linear-random lerp, a + (b-a)*rand01, rand01 in [0,1).
// Used in Init for spin rates and splat timer, in Update for splat velocity magnitude.
static inline float JibT796(float a, float b) {
    if (a == b) return a;
    return a + (b - a) * Math::g_Random.RandF(1.0f);
}

// ctor @ 0x1d9580 (CreateEntity memsets 0xB0 first, then calls ctor)
Jiblet::Jiblet()
    : m_SplatTimer(0.0f)
    , m_pModel()
    , m_EmitterHash(0)
    , m_pEmitter(0)
    , m_Rotation()
    , m_DripRate(0.0f)
    , m_FruitType(0)
    , m_GravBase(0.0f, 0.0f, 0.0f)
    , m_SpinRateX(0.0f)
    , m_SpinRateY(0.0f)
    , m_SpinRateZ(0.0f)
    , m_Age(0.0f)
{
    entityType = 5;
}

Jiblet::~Jiblet() {}

// Init @ 0x1e50c0  (faithful arg-to-field map + resolved DATs)
// _ZN6Jiblet4InitEiR8_Vector3IfEfS1_N6Mortar8SmartPtrINS3_5ModelEEEmfS1_
//
// Binary signature:
//   Jiblet::Init(int   fruitType,            // r1  -> m_FruitType(+0x90)
//                Vec3& pos,                  // r2  -> pos(+0x10)
//                float scale,                // s0  -> m_Scale(+0x28) = (scale,scale,scale)
//                Vec3  vel,                  // r3  -> vel(+0x1c)
//                SmartPtr<Model> mdl,        // [sp] -> m_pModel(+0x40) operator=
//                unsigned long emitterHash,  // [sp] -> m_EmitterHash(+0x44)
//                float dripRate,             // s1  -> m_DripRate(+0x8c) + splat timer
//                Vec3  grav)                 // [sp] -> m_GravBase(+0x94)
//
// Resolved DAT constants (v1.6.1):
//   DAT_001e52cc = -0.04f   -> m_Age(+0xac) initial value
//   DAT_001e52d0 =  100.0f  -> m_SplatTimer when dripRate<=0 (drip loop disabled)
//   DAT_001e52d4 = -100.0f  -> spin-rate lower bound
//   DAT_001e52d8 =    0.0f  -> splat-timer lower bound
//   GOT[0x75d4]  -> global One (1,1,1): m_Scale = One*scale = (scale,scale,scale)
//   GOT[0x6ba4]  -> global identity Matrix44: m_Rotation seeded to identity
//   T_796(a,b) @ 0x1e508c = a + (b-a)*rand01  (linear-random lerp, rand01 in [0,1))
void Jiblet::Init(int fruitType, _Vector3<float>& pos_, float scale_, _Vector3<float> vel_,
                  Mortar::SmartPtr<Mortar::Model> mdl,
                  unsigned long emitterHash, float dripRate, _Vector3<float> grav_) {
    pos = pos_;
    m_pModel = mdl;                 // SmartPtr::operator= @ +0x40 (binary 0x114c10)
    vel = vel_;
    m_GravBase = grav_;             // +0x94

    // m_Scale(+0x28) = One * scale.
    // ASM-spec v1.6.1 Jiblet::Init @0x001e50c0: m_Scale = One*scale = (scale,scale,scale)
    scale.x = scale_;
    scale.y = scale_;
    scale.z = scale_;

    m_Age = -0.04f;                 // +0xac, DAT_001e52cc

    // m_Rotation: identity (binary copies global identity Matrix44), then three
    // random-angle rotations about X, Y, Z using 16-bit angle indices.
    m_Rotation.Identity();
    {
        uint16_t ax = (uint16_t)(Math::g_Random.Rand32(0x10000) & 0xffff);
        m_Rotation.RotX44(SinIdx(ax), CosIdx(ax));
        uint16_t ay = (uint16_t)(Math::g_Random.Rand32(0x10000) & 0xffff);
        m_Rotation.RotY44(SinIdx(ay), CosIdx(ay));
        uint16_t az = (uint16_t)(Math::g_Random.Rand32(0x10000) & 0xffff);
        m_Rotation.RotZ44(SinIdx(az), CosIdx(az));
    }

    // Clear ENT_INACTIVE | ENT_KILLED (binary: flags &= 0xee).
    flags &= static_cast<uint8_t>(0xee);

    // Spin rates: T_796(-100, 100) -> uniform in [-100, 100) per axis.
    m_SpinRateX = JibT796(-100.0f, 100.0f);
    m_SpinRateY = JibT796(-100.0f, 100.0f);
    m_SpinRateZ = JibT796(-100.0f, 100.0f);

    m_DripRate = dripRate;          // +0x8c
    m_FruitType = fruitType;        // +0x90

    if (dripRate <= 0.0f) {
        m_SplatTimer = 100.0f;      // +0x3c, DAT_001e52d0 (drip loop disabled)
    } else {
        m_SplatTimer = JibT796(0.0f, 1.0f / dripRate);  // +0x3c
    }

    m_EmitterHash = emitterHash;    // +0x44
    m_pEmitter = 0;                 // +0x48
}

// Update @ 0x1e5330
void Jiblet::Update(float dt) {
    PSPParticleManager& pm = PSPParticleManager::GetInstance();

    m_Age += dt;

    // Lazy emitter creation once age passes the gate (DAT_001e5728 = 0.05).
    if (!m_pEmitter && m_Age > JIB_EMITTER_AGE_GATE) {
        if (pm.EmitterExists(m_EmitterHash)) {
            // Binary passes (hash, 0, 0): no ref-ptr, not update-when-paused.
            m_pEmitter = pm.AddEmitter(m_EmitterHash, 0, false);
            if (m_pEmitter) {
                // Binary: *(byte*)(emitter+0x4d) = 1 (trail-started flag).
                m_pEmitter->m_bTrailStarted = 1;
                // dir = normalise(vel)
                _Vector3<float> dir = vel;
                dir.Normalise();
                // pos -> emitter.m_Pos (+0x08)
                m_pEmitter->m_Pos = pos;
                // emitter+0x30 (m_DirCos) = -dir.y ; emitter+0x34 (m_DirSin) = -dir.x
                m_pEmitter->m_DirCos = -dir.y;
                m_pEmitter->m_DirSin = -dir.x;
            }
        }
    }

    // Verlet integration (gate: m_Age >= 0; Init seeds m_Age = -0.04 so the
    // first ~2 frames are skipped). Acceleration source is m_GravBase (+0x94),
    // NOT Entity::scale (+0x28).
    if (m_Age >= 0.0f) {
        // accel * 0.5 * dt -> vel  (half-step)
        vel.x += m_GravBase.x * 0.5f * dt;
        vel.y += m_GravBase.y * 0.5f * dt;
        vel.z += m_GravBase.z * 0.5f * dt;
        // (pos += vel) * dt  -- binary: ((accel*0.5*dt + vel) * dt) += pos
        pos.x += vel.x * dt;
        pos.y += vel.y * dt;
        pos.z += vel.z * dt;
        // accel * dt -> vel  (full velocity step)
        vel.x += m_GravBase.x * dt;
        vel.y += m_GravBase.y * dt;
        vel.z += m_GravBase.z * dt;

        // Quaternion spin integrator. Binary angle scale DAT_001e572c = 182.0
        // (= 65536/360): spin rates are DEGREES-per-frame (Init seeds them with
        // T_796(-100,100)). angle16 = (int)(spinRate * dt * 182.0) & 0xffff.
        Quaternion qx, qy, qz;
        qx.CreateFromAxisAngle(1.0f, 0.0f, 0.0f,
            (uint32_t)((int)(m_SpinRateX * dt * JIB_SPIN_TO_ANGLE16) & 0xffff));
        qy.CreateFromAxisAngle(0.0f, 1.0f, 0.0f,
            (uint32_t)((int)(m_SpinRateY * dt * JIB_SPIN_TO_ANGLE16) & 0xffff));
        qz.CreateFromAxisAngle(0.0f, 0.0f, 1.0f,
            (uint32_t)((int)(m_SpinRateZ * dt * JIB_SPIN_TO_ANGLE16) & 0xffff));
        Quaternion combined = qx * qy * qz;
        Matrix44 quatMat = combined.ToMatrix44();
        // m_Rotation = m_Rotation * quatMat (post-multiply, column-major)
        Matrix44 newRot;
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                float s = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    s += m_Rotation.m[k * 4 + row] * quatMat.m[col * 4 + k];
                }
                newRot.m[col * 4 + row] = s;
            }
        }
        m_Rotation = newRot;

        // Drip loop: spawn SplatEntity on interval (1/m_DripRate seconds).
        // Binary: while (m_SplatTimer < 0 && m_DripRate > 0).
        m_SplatTimer -= dt;
        while (m_SplatTimer < 0.0f && m_DripRate > 0.0f) {
            // 16-bit random angle index -> SinIdx/CosIdx (Rand32 & 0xffff).
            uint16_t a16 = (uint16_t)(Math::g_Random.Rand32(0x10000) & 0xffff);
            // Magnitude T_796(1.0, 40.0): random in [1,40] (DAT_001e5734 = 40.0).
            float vmag = JibT796(1.0f, 40.0f);
            // GetFree() never returns null (v1.6.1 SplatEntity::GetFree @0x001eb318 --
            // flat round-robin pool steals the cursor slot when full).
            SplatEntity* s = SplatEntity::GetFree();
            _Vector3<float> sp = pos;
            // sv = (sin*vmag, cos*vmag, 0.0)  (DAT_001e5730 = 0.0)
            _Vector3<float> sv(SinIdx(a16) * vmag, CosIdx(a16) * vmag, 0.0f);
            // ASM-spec v1.6.1 Jiblet::Update @0x001e5618: mute arg is a constant 1
            // (no FruitInfo lookup) -- drip splats always land silent.
            s->MakeSplat(sp, sv, false, /*mute=*/true, (long)m_FruitType);
            m_SplatTimer += 1.0f / m_DripRate;
        }
    }

    // Per-frame emitter position sync (binary: emitter.m_Pos = pos).
    if (m_pEmitter) {
        m_pEmitter->m_Pos = pos;
    }

    // Bounds kill (DAT_001e5738..0x1e5744): X out of [-288,288] or Y out of
    // [-192,192] -> retire. NOTE: only pos.x/pos.y are tested in the binary.
    if (pos.x < -288.0f || pos.x > 288.0f ||
        pos.y < -192.0f || pos.y > 192.0f) {
        Kill();
    }
}

// ASM-spec v1.6.1 Jiblet::Kill @0x001e52ec: (1) if m_pEmitter, ClearEmitter
// and null it; (2) m_pModel.SetPtr(0) -- drops the model SmartPtr ref;
// (3) flags |= ENT_KILLED. Non-virtual member (Mortar::Entity has no Kill
// vtable slot). Sole real call site is the bounds-kill branch in Update
// above (0x1e5714); the other xrefs are structural (entry-point external
// ref, shared inter-TU GOT slot), not additional call sites.
void Jiblet::Kill() {
    if (m_pEmitter) {
        PSPParticleManager::GetInstance().ClearEmitter(m_pEmitter);
        m_pEmitter = 0;
    }
    m_pModel.SetPtr(nullptr);
    flags |= ENT_KILLED;
}

// Draw @ 0x1e5750 (vtable slot 5 / +0x14)
// Binary: if (m_pModel) { mat = m_Rotation; mat.Scale44(scale); mat.GlobalTranslate44(pos); m_pModel->Draw(mat); }
// The Renderer& param has no binary counterpart (port-only, to satisfy the
// Mortar::Entity pure-virtual signature); the binary Draw takes only `this`.
void Jiblet::Draw(Renderer& /*r*/) {
    // NOTE: genuine v1.6.1 gate -- Draw @0x001e5750 opens with
    // 'add r0,r0,#0x40; bl SmartPtr::IsValid; cmp r0,#0; beq'. Not a port addition.
    if (!m_pModel) {
        return;
    }

    // Local copy of the accumulated rotation matrix (m_Rotation @ +0x4c).
    Matrix44 mat = m_Rotation;

    // Binary instance _Matrix44<float>::Scale44(sx,sy,sz) @ 0x0015d06c scales
    // ROWS 0/1/2 by scale.x/y/z (offsets {0,0x10,0x20,0x30}=row0, etc.) -- this
    // is a row-scale, NOT the column-scale of Matrix44::ApplyScale nor the
    // static MakeScale factory, so it is inlined here to stay binary-faithful.
    {
        float sx = scale.x;
        float sy = scale.y;
        float sz = scale.z;
        mat.m[0]  *= sx; mat.m[4]  *= sx; mat.m[8]  *= sx; mat.m[12] *= sx; // row 0
        mat.m[1]  *= sy; mat.m[5]  *= sy; mat.m[9]  *= sy; mat.m[13] *= sy; // row 1
        mat.m[2]  *= sz; mat.m[6]  *= sz; mat.m[10] *= sz; mat.m[14] *= sz; // row 2
    }

    // World-space translate by entity position (pos @ +0x10).
    mat.GlobalTranslate44(pos);

    m_pModel->Draw(mat);
}

// PostUpdate @ 0x1e5c04 — empty
void Jiblet::PostUpdate(float /*dt*/) {}
