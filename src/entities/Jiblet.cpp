//
// Jiblet : Mortar::Entity
// Binary: ctor 0x1d9580 · Init 0x1e50c0 · Update 0x1e5330 · PostUpdate(empty) 0x1e5c04
//

#include "Jiblet.h"
#include "SplatEntity.h"
#include "math/Quaternion.h"
#include "math/MathUtil.h"
#include "particle/PSPParticleManager.h"
#include <cstdlib>
#include <cmath>
#include <cstring>

struct Renderer;

// Binary literal constants from Jiblet functions
// (exact DAT addresses from v1.6.1 spec)

// Init @ 0x1e50c0
// Gravity scale applied to param_1 (scale-speed) to seed accel
// TODO: 0x1e50c0 — resolve exact Init DAT gravity constant and arg-to-field mapping

// Update @ 0x1e5330
// m_Age gate for emitter creation
static const float JIB_EMITTER_AGE_GATE = 0.05f;
// TODO: 0x1e5330 — resolve exact DAT for emitter-age gate and bounds kill distances

// Spin rate scale used in CreateFromAxisAngle (quaternion integrator)
// Binary: spinRate * dt * DAT_k (angle scale).
// The Quaternion::CreateFromAxisAngle takes a 16-bit angle index (0..65535 = full circle)
// so DAT_k converts radians -> 16-bit: k = 65536 / (2*pi) ~= 10430.37
static const float JIB_SPIN_TO_ANGLE16 = 10430.37f;  // 65536 / (2*pi)

// Bounds kill thresholds (centred-ortho coordinate space: X=-240..+240, Y=-160..+160)
// TODO: 0x1e5330 — verify exact out-of-bounds constants from DAT

static inline float JibRand(float r) {
    return ((float)rand() / (float)RAND_MAX) * r;
}

// ctor @ 0x1d9580 (CreateEntity memsets 0xB0 first, then calls ctor)
Jiblet::Jiblet()
    : m_SplatTimer(0.0f)
    , m_pModel()
    , m_EmitterHash(0)
    , m_pEmitter(0)
    , m_Rotation()
    , m_FadeRate(0.0f)
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

// Init @ 0x1e50c0
// Field writes from spec (ground truth):
//   pos(+0x10) = *param_5
//   vel(+0x1c) = *param_6
//   accel(+0x28) = DAT_grav * param_1
//   clears base.p_pad+8 low nibble
//   m_SplatTimer(+0x3c): =DAT (param2<=0) or T_796(DAT, 1/param2)
//   m_pModel(+0x40): operator= from model param
//   m_EmitterHash(+0x44) = param_8
//   m_pEmitter(+0x48) = 0
//   m_Rotation(+0x4c): unit Mat44 then RotX/Y/Z44 with random angles
//   m_FadeRate(+0x8c) = param_2 (drip rate)
//   m_FruitType(+0x90) = param_4
//   m_GravBase(+0x94) = *param_9
//   spinRateX/Y/Z: = random T_796
//
// Arg layout is fuzzy per spec (HFA/VFP param passing), so accept what the
// call sites supply and map field-write ground truth.
void Jiblet::Init(void* /*payload*/, long /*unused*/, Vec3* scaleOrNull) {
    (void)scaleOrNull;
    // TODO: 0x1e50c0 — Init arg layout is HFA-fuzzy; field writes require
    // resolved caller (Fruit slice code / CreateEntity(5)) to wire correctly.
    // Known writes:
    //   pos, vel, scale, accel from caller (set before Init, or passed as params)
    //   m_pModel operator= from caller-supplied SmartPtr<Model>
    //   m_EmitterHash from caller uint32_t
    //   m_FruitType from caller int
    //   m_GravBase from caller Vec3*
    //   m_FadeRate (drip rate) from caller float
    //   m_SplatTimer = DAT or T_796(DAT, 1/fadeRate)
    //   m_Rotation: unit Mat44 then random-angle RotX/Y/Z44
    //   spinRateX/Y/Z = random T_796

    // Clear base flag low nibble (binary: clears bits 0+4 of flags byte)
    flags &= static_cast<uint8_t>(0xee);

    // Rotation: identity then random angles
    m_Rotation.Identity();
    // TODO: 0x1e50c0 — apply RotX44/RotY44/RotZ44 with random angles (T_796 range unknown)

    // Spin rates: random per axis
    // TODO: 0x1e50c0 — resolve T_796 range for spin rates (max value from binary DAT unknown)
    m_SpinRateX = JibRand(6.2831853f);
    m_SpinRateY = JibRand(6.2831853f);
    m_SpinRateZ = JibRand(6.2831853f);

    m_pEmitter = 0;
    m_Age = 0.0f;
}

// Update @ 0x1e5330
// Per the spec:
//   1. m_Age += dt. If no emitter and m_Age > DAT: if EmitterExists(hash) -> AddEmitter,
//      set [+0x4d]=1 (a byte inside m_Rotation area — see note), seed pos from this+0x10,
//      dir from -normalise(vel).
//   2. if m_Age >= 0: verlet: accel*0.5*dt -> vel; vel*dt -> pos; accel*dt -> vel.
//      Build 3 quats CreateFromAxisAngle(spinRateX/Y/Z*dt*k) -> multiply -> Matrix44 ->
//      m_Rotation *= quatMat.
//   3. drip loop: m_SplatTimer -= dt; while <0 && m_FadeRate>0:
//      SplatEntity::GetFree() -> MakeSplat(pos, randDir*T_796, 0, 1, m_FruitType);
//      m_SplatTimer += 1/m_FadeRate.
//   4. sync emitter pos; bounds kill.
void Jiblet::Update(float dt) {
    PSPParticleManager& pm = PSPParticleManager::GetInstance();

    m_Age += dt;

    // Lazy emitter creation
    if (!m_pEmitter && m_Age > JIB_EMITTER_AGE_GATE) {
        if (pm.EmitterExists(m_EmitterHash)) {
            // TODO: 0x1e5330 — seed emitter pos from pos, dir from -normalise(vel)
            m_pEmitter = pm.AddEmitter(m_EmitterHash, &m_pEmitter, false);
        }
    }

    // Verlet integration
    if (m_Age >= 0.0f) {
        // accel * 0.5 * dt -> vel
        vel.x += scale.x * 0.5f * dt;
        vel.y += scale.y * 0.5f * dt;
        vel.z += scale.z * 0.5f * dt;
        // vel * dt -> pos
        pos.x += vel.x * dt;
        pos.y += vel.y * dt;
        pos.z += vel.z * dt;
        // accel * dt -> vel (second half of verlet)
        vel.x += scale.x * 0.5f * dt;
        vel.y += scale.y * 0.5f * dt;
        vel.z += scale.z * 0.5f * dt;

        // Quaternion spin integrator: build 3 quats, multiply, convert to Matrix44,
        // post-multiply m_Rotation.
        // Binary: CreateFromAxisAngle(1,0,0, spinX*dt*k), (0,1,0, spinY*dt*k), (0,0,1, spinZ*dt*k).
        Quaternion qx, qy, qz;
        qx.CreateFromAxisAngle(1.0f, 0.0f, 0.0f,
            (uint32_t)(int)(m_SpinRateX * dt * JIB_SPIN_TO_ANGLE16));
        qy.CreateFromAxisAngle(0.0f, 1.0f, 0.0f,
            (uint32_t)(int)(m_SpinRateY * dt * JIB_SPIN_TO_ANGLE16));
        qz.CreateFromAxisAngle(0.0f, 0.0f, 1.0f,
            (uint32_t)(int)(m_SpinRateZ * dt * JIB_SPIN_TO_ANGLE16));
        Quaternion combined = qx * qy * qz;
        Matrix44 quatMat = combined.ToMatrix44();
        // m_Rotation *= quatMat
        // Binary: post-multiplies (m_Rotation = m_Rotation * quatMat)
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
    }

    // Drip loop: spawn SplatEntity on interval
    m_SplatTimer -= dt;
    while (m_SplatTimer < 0.0f && m_FadeRate > 0.0f) {
        SplatEntity* s = SplatEntity::GetFree();
        if (s) {
            // Random direction for splat velocity; magnitude from T_796
            // TODO: 0x1e5330 — resolve exact T_796 range for splat velocity magnitude
            float vmag = JibRand(30.0f) + 5.0f;
            float angle = JibRand(6.2831853f);
            Vec3 sv(cosf(angle) * vmag, sinf(angle) * vmag, -vmag * 0.5f);
            s->MakeSplat(pos, sv, false, m_FruitType);
        }
        m_SplatTimer += 1.0f / m_FadeRate;
    }

    // Sync emitter position
    if (m_pEmitter) {
        // TODO: 0x1e5330 — sync emitter pos (PSPParticleEmitter field layout needed)
    }

    // Bounds kill: pos out of [X=-240..240, Y=-160..160] -> set ENT_KILLED
    // TODO: 0x1e5330 — verify exact bounds constants from binary DAT
    if (pos.x < -300.0f || pos.x > 300.0f || pos.y < -220.0f || pos.y > 220.0f) {
        flags |= ENT_KILLED;
    }
}

// Draw @ slot5 (not pulled from binary; renders m_pModel with m_Rotation matrix at pos)
void Jiblet::Draw(Renderer& /*r*/) {
    // TODO: 0x1e5??? — Jiblet::Draw not yet ported (renders m_pModel with m_Rotation at pos)
}

// PostUpdate @ 0x1e5c04 — empty
void Jiblet::PostUpdate(float /*dt*/) {}
