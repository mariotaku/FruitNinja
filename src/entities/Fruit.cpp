#include "Fruit.h"
#include "ActorManager.h"
#include "FruitInfo.h"
#include "Bomb.h"
#include "SlashEntity.h"
#include "SplatEntity.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/gl_funcs.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "asset/MeshManager.h"
#include "asset/TextureManager.h"
#include "particle/PSPParticleManager.h"
#include "hud/SliceEffect.h"
#include "hud/MissControl.h"
#include "game/BombHit.h"
#include "game/ScoreState.h"
#include "game/WaveManager.h"
#include "game/GameOver.h"
#include "engine/network/NetworkManager.h"
#include "game/FruitSaveData.h"
#include "util/StringHash.h"
#include "Game.h"
#include "audio/GameSound.h"
#include "math/math3d.h"
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Analysed: 2026-04-29T00:00

// Fruit.cpp uses FruitInfo as both a struct name (global) and a static
// method name (Fruit::FruitInfo). Alias the struct at file scope so all
// local declarations can use the unambiguous name FruitInfoData.
typedef ::FruitInfo FruitInfoData;

// Count of active power-fruits (FruitInfo::m_pPowers != nullptr) on screen.
// Decremented by KillFruit on natural-expiry path (flag 0x10 not yet set).
// Binary @ 0x00176cc8..0x00176cd4: unconditional store, clamped to >= 0 (not >= 1).
static int g_PowerFruitCount = 0;

// Static Colour constants from _GLOBAL__I_Fruit.cpp @ 0x0017a354.
// *DAT_0017a678: Colour(0x80, 0x80, 0xff, 0x80) = RGBA(128, 128, 255, 128)
// Likely g_FruitOutlineTint or g_FruitGlowTint. Exact consumer not yet RE'd -- TODO.
static Colour g_FruitTint1(128, 128, 255, 128);  // DAT_0017a678
// *DAT_0017a670: copy-ctor from DAT_0017a674. Source value not yet RE'd -- TODO.
static Colour g_FruitTint2(0, 0, 0, 255);        // DAT_0017a670 (placeholder, copy from DAT_0017a674)

// Binary constants for fruit slicing.
// Resolved from DATs near CollisionResponse (0x1780b0) and Slice (0x176d58).
static const float SLICE_TIMER_BASE    = 0.03f;   // DAT_001784dc
static const float SLICE_BLADE_SCALE   = 0.1f;    // DAT_001784e0
static const float SLICE_CLAMP_MIN_NRM = 4.0f;    // normal fruit clamp
static const float SLICE_CLAMP_MAX     = 8.0f;
// Fruit::SetFruitType (0x0017621c) collision radius formula — verified
// 2026-04-15 from binary disassembly at 0x0017630e..0x0017631e:
//   vldr s14, [r3, #0x244]   ; s14 = m_Scale            (XML "scale")
//   vldr s15, [r3, #0x248]   ; s15 = m_CollisionScale   (XML "collision")
//   vmla s15, s13, s14       ; s15 += 0.52 * s14
//   vmul s15, s15, scale     ; s15 *= scaleParam (1.0 typical)
// →  radius = (m_CollisionScale + 0.52 * m_Scale) * scaleParam
// Defaults from LoadInfo: m_Scale = 25.0 @ 0x41c80000,
//                         m_CollisionScale = 1.0 @ 0x3f800000.
static const float COL_RADIUS_FACTOR = 0.52f;   // DAT_00176340

// Random float in [-range, +range]
static float RandRange(float range) {
    return ((float)rand() / RAND_MAX * 2.0f - 1.0f) * range;
}

// Matches RandomStartAngle(Quat&, false) @ 0x00175740 — gives the fruit a
// uniformly random orientation on the sphere by picking a random axis in
// the unit cube, normalising, and combining with a random ~16-bit angle.
// The old port used FromAxisAngle(Vec3(1,0,0), RandRange(pi)) which locked
// every fruit's initial spin to the X axis — visible as all fruits starting
// level/upright instead of at varied tilts.
static Quaternion RandomStartAngle() {
    float ax = (float)rand() / RAND_MAX * 2.0f - 1.0f;   // [-1, 1]
    float ay = (float)rand() / RAND_MAX * 2.0f - 1.0f;
    float az = (float)rand() / RAND_MAX * 2.0f - 1.0f;
    float len = sqrtf(ax*ax + ay*ay + az*az);
    if (len < 1e-6f) { ax = 1.0f; ay = 0.0f; az = 0.0f; len = 1.0f; }
    ax /= len; ay /= len; az /= len;
    // Binary: Rand32(0xff3a) — ~full turn in 16-bit angle units.
    uint32_t angle16 = (uint32_t)(((uint32_t)rand()) % 0xff3aU);
    Quaternion q;
    q.CreateFromAxisAngle(ax, ay, az, angle16);
    return q;
}

Fruit::Fruit()
    : m_FruitType(0)
    , m_SliceTimer(-1.0f)
    , m_SliceAngle(0)
    , m_SliceImpulse(0.0f)
    , m_SlicePos(0, 0, 0)
    , m_pEmitter1(nullptr)
    , m_pEmitter2(nullptr)
    , m_bSliced(false)
    , m_bDetached(false)
    , m_bDrawWhole(false)
    , m_bCriticalEligible(false)
    , m_bNoPowerUp(0)
    , m_pSlasher(nullptr)
    , m_TrackerID(0)
    , m_ScaleAnim(0.0f)
    , m_ChuckDelay(0.0f)
    , m_PlayerIdx(0)
    , m_TimeScale(1.0f)
    , m_ZPosition(0.0f)
{
    entityType = 0;
}

Fruit::~Fruit() {
    delete m_Col;
    m_Col = nullptr;
    // Model released by SmartPtr destructor
}

// ASM-verified: 2026-04-28T00:00 binary @ 0x00176708 (asm-inspector)
void Fruit::Init(int param1, int fruitType, int param3) {
    (void)param1; (void)param3;
    m_FruitType = fruitType;
    m_bSliced = false;
    m_bDetached = false;
    m_bDrawWhole = false;
    m_bCriticalEligible = false;
    m_bNoPowerUp = 0;
    m_pSlasher = nullptr;
    m_TrackerID = 0;
    m_ScaleAnim = 0.0f;
    m_ChuckDelay = 0.0f;
    m_PlayerIdx = 0;
    m_TimeScale = 1.0f;
    flags &= ~ENT_SKIP_MASK;  // activate + unhide

    // Reset slice state (binary Fruit::Init — m_SliceTimer = -1).
    m_SliceTimer   = -1.0f;
    m_SliceAngle   = 0;
    m_SliceImpulse = 0.0f;
    m_SlicePos     = Vec3(0, 0, 0);
    m_pEmitter1    = nullptr;
    m_pEmitter2    = nullptr;

    // Random rotation velocity (matches binary Fruit::Init @ 0x00176708):
    // one triple of random values, stored IDENTICALLY into both m_RotVel1
    // and m_RotVel2 — the two halves tumble in sync.
    m_RotVel1 = Vec3(RandRange(5.5f), RandRange(5.5f), RandRange(5.5f));
    m_RotVel2 = m_RotVel1;

    // Random start rotation — random axis + random angle (binary
    // RandomStartAngle @ 0x00175740, called with false from Fruit::Init).
    m_Rot1 = RandomStartAngle();
    m_Rot2 = m_Rot1;

    // Default gravity — confirmed from Fruit::Init 0x00176708: literal -12.0, DAT_00176a18=0.0
    m_Gravity = Vec3(0.0f, -12.0f, 0.0f);

    // Rotation axis offset.
    // Binary Fruit::Init @ 0x00176708 reads *globalConfigVec3 (GOT 0x001f4328);
    // BSS Vec3 initialised by _GLOBAL__I_Fruit.cpp to (0,0,0).
    m_RotAxis = Vec3(0.0f, 0.0f, 0.0f);

    // Matches SetFruitType (0x17621c):
    // visualScale = globalScaleVec * FruitInfo[type].scale * VISUAL_SCALE_MULT (0.01)
    // globalScaleVec is at BSS 0x1F4334, initialized to (0,0,0) by static init
    // but overwritten at runtime before fruit creation.
    // Per-fruit scale from Data/xml/fruitlist.xml (e.g. watermelon=75)
    {
        // Vec3::One at BSS 0x1F4334 — a constant singleton for (1,1,1), not a
        // mutable scale variable. Matches binary: _Vector3::operator*(Vec3*, float*)
        // in SetFruitType (0x17621c) multiplies Vec3::One by m_Scale then 0.01.
        const FruitInfoData* info = FruitInfo_Get(fruitType);
        float fruitScale = info ? info->m_Scale * 0.01f : 1.0f;
        scale = Vec3::One() * fruitScale;

        // Collision sphere (SetFruitType @ 0x0017621c, verified
        // 2026-04-15 from disassembly).
        //   radius = (m_CollisionScale + 0.52 * m_Scale) * scaleParam
        // where scaleParam is the SetFruitType arg (1.0 at the common
        // call site). m_Scale is the XML "scale" attr (e.g. watermelon
        // = 75); m_CollisionScale is the XML "collision" attr (5 for
        // every fruit in fruitlist.xml). Defaults if FRUIT_INFO is
        // missing: m_Scale = 25.0, m_CollisionScale = 1.0.
        const float fScale  = info ? info->m_Scale          : 25.0f;
        const float fColBase = info ? info->m_CollisionScale : 1.0f;
        const float radius   = fColBase + COL_RADIUS_FACTOR * fScale;
        if (!m_Col) m_Col = new Mortar::ColSphere();
        Mortar::ColSphere* cs = static_cast<Mortar::ColSphere*>(m_Col);
        cs->center = Vec3(pos.x, pos.y, 0.0f);
        cs->radius = radius;
    }

    // Load mesh via MeshManager (cached, matches binary pattern)
    Game* game = Game::GetInstance();
    Mortar::MeshManager* meshMgr = Mortar::MeshManager::GetInstance();
    if (game && meshMgr) {
        const FruitInfoData* info = FruitInfo_Get(fruitType);
        const char* modelName = (info && info->m_ModelName[0]) ? info->m_ModelName : "apple";
        std::string meshPath = game->data_dir + "/models/Fruit/" + modelName + "_single.mmd";
        m_Model = meshMgr->Load(meshPath.c_str());

        // Assign fruit atlas texture to the model's mesh
        if (m_Model.IsValid() && !m_Model->m_Meshes.empty()) {
            static SmartPtr<Mortar::Texture> s_fruitAtlas;
            if (!s_fruitAtlas.IsValid()) {
                std::string texPath = game->data_dir + "/models/fruit/textures/fruit_atlas.tex";
                s_fruitAtlas = Mortar::TextureManager::GetInstance().Load(texPath.c_str());
            }
            if (s_fruitAtlas.IsValid()) {
                for (int i = 0; i < (int)m_Model->m_Meshes.size(); i++) {
                    if (m_Model->m_Meshes[i].IsValid() && !m_Model->m_Meshes[i]->HasDiffuseTexture()) {
                        m_Model->m_Meshes[i]->SetDiffuseTexture(s_fruitAtlas);
                    }
                }
            }
        }
    }
}

void Fruit::Chuck(const Vec3& velocity, float delay) {
    vel = velocity;
    m_ChuckDelay = delay;
    m_ScaleAnim = 0.0f;
    flags &= ~ENT_SKIP_MASK;
}

void Fruit::Update(float dt) {
    if (!IsActive()) return;

    // Launch delay
    if (m_ChuckDelay > 0.0f) {
        m_ChuckDelay -= dt;
        if (m_ChuckDelay > 0.0f) return;
        m_ChuckDelay = 0.0f;
    }

    // Scale animation (0 → 1 over ~0.3s)
    if (m_ScaleAnim < 1.0f) {
        m_ScaleAnim += dt * 3.0f;
        if (m_ScaleAnim > 1.0f) m_ScaleAnim = 1.0f;
    }

    if (!m_bSliced) {
        // === UNSLICED FRUIT ===
        // Binary integration (Fruit::Update 0x00177680):
        //   pos += (vel*dt + 0.5*g*dt²) * 60.0    (DAT_00177d00 = 60.0)
        //   vel += gravity * dt
        // The ×60 multiplier on the position step is the binary's
        // tuning fudge — at fixed dt=1/60 it makes effective
        // gravity strong enough for a Fruit-Ninja-feel arc. Without
        // it the port's fruit drift like they're underwater.
        const float POS_INTEGRATION_SCALE = 60.0f;
        Vec3 step = (vel * dt + m_Gravity * (0.5f * dt * dt)) * POS_INTEGRATION_SCALE;
        pos += step;
        vel += m_Gravity * dt;

        // Rotation axis drift — also scaled by the same fudge so
        // it stays consistent with the velocity-driven motion.
        pos += m_RotAxis * dt * POS_INTEGRATION_SCALE;

        // Backup for future split
        m_SecondPos = pos;
        m_SecondVel = vel;

        // Slice-timer countdown — set positive by OnSliced, triggers
        // the actual split when it hits 0. Matches binary Fruit::Update
        // @ 0x177680 phase 4.
        if (m_SliceTimer > 0.0f) {
            m_SliceTimer -= dt;
            if (m_SliceTimer <= 0.0f) {
                m_SliceTimer = 0.0f;
                Slice();
            }
        }
    } else {
        // ASM-verified: 2026-04-28T00:00 binary @ 0x00177680..0x001777bc (asm-inspector)
        // === SLICED (two halves) ===
        // Binary (Fruit::Update sliced branch, 0x00177680):
        //   normalize(m_Gravity); gravLen += DAT_00177950(=0.2) * dtNorm * 4.5
        //   = gravLen += 0.9 * dtNorm   (per-frame growth at 60fps)
        //   m_Gravity *= new_gravLen / old_gravLen (rescale unit vec)
        //
        // Then plain Euler integration on both halves, NO ×60 fudge.
        // This is intentional in the binary — sliced halves drift
        // gently while the gravity ramps up.
        //
        // Port keeps the same growth formula but uses the same ×60
        // position scaling as the unsliced branch so the halves
        // visibly drift instead of being frozen.
        const float gravLen0 = m_Gravity.length();
        const float dtNorm   = dt * 60.0f;
        const float growRate = 0.2f * dtNorm * 4.5f;   // = 0.9 per frame
        const float gravLen1 = gravLen0 + growRate;
        if (gravLen0 > 0.0001f) {
            m_Gravity = m_Gravity * (gravLen1 / gravLen0);
        }

        // Two-body physics — same ×60 position scale as unsliced.
        const float POS_INTEGRATION_SCALE = 60.0f;
        vel        += m_Gravity * dt;
        m_SecondVel += m_Gravity * dt;
        pos        += vel        * dt * POS_INTEGRATION_SCALE;
        m_SecondPos += m_SecondVel * dt * POS_INTEGRATION_SCALE;
    }

    // Quaternion rotation update (both halves). Matches binary Fruit::Update
    // @ 0x00177680: each axis is a CreateFromAxisAngle with 16-bit angle
    //   idx = (ushort)(int)(rotVel * dtNorm * 182.0)   // DAT_00177ff0 = 182
    // then m_Rot = m_Rot * qx * qy * qz.
    // In radians that is rotVel * dtNorm * (182 * 2pi / 65536) ≈
    // rotVel * dtNorm * (pi / 180) — i.e. one degree per unit of rotVel per
    // 60fps frame. The old port used 0.01 here, rotating fruits ~57% as
    // fast as the binary.
    const float ANGLE_PER_UNIT = 182.0f * 6.2831853f / 65536.0f;  // ~pi/180
    const float rotScale = dt * 60.0f;  // dtNorm (DAT_0017794c = 1/60)
    {
        Quaternion qx = Quaternion::FromAxisAngle(Vec3(1, 0, 0),
            m_RotVel1.x * rotScale * ANGLE_PER_UNIT);
        Quaternion qy = Quaternion::FromAxisAngle(Vec3(0, 1, 0),
            m_RotVel1.y * rotScale * ANGLE_PER_UNIT);
        Quaternion qz = Quaternion::FromAxisAngle(Vec3(0, 0, 1),
            m_RotVel1.z * rotScale * ANGLE_PER_UNIT);
        m_Rot1 = (m_Rot1 * qx * qy * qz).normalized();
    }
    {
        Quaternion qx = Quaternion::FromAxisAngle(Vec3(1, 0, 0),
            m_RotVel2.x * rotScale * ANGLE_PER_UNIT);
        Quaternion qy = Quaternion::FromAxisAngle(Vec3(0, 1, 0),
            m_RotVel2.y * rotScale * ANGLE_PER_UNIT);
        Quaternion qz = Quaternion::FromAxisAngle(Vec3(0, 0, 1),
            m_RotVel2.z * rotScale * ANGLE_PER_UNIT);
        m_Rot2 = (m_Rot2 * qx * qy * qz).normalized();
    }

    // Update collision sphere center (z clamped to 0).
    if (m_Col) static_cast<Mortar::ColSphere*>(m_Col)->center = Vec3(pos.x, pos.y, 0.0f);

    // Track juice emitters with the two halves so particles follow the
    // pieces instead of spraying from the original slice point. Matches
    // binary Fruit::Update @ 0x177680 tail section.
    if (m_pEmitter1) m_pEmitter1->m_Pos = pos;
    if (m_pEmitter2) m_pEmitter2->m_Pos = m_SecondPos;

    if (CheckHasGoneOffscreen()) {
        KillFruit(true);
    }
}

// Zen-mode "mirror bounce at X limits" flag. Reads bit 0x20 of
// SlashEntity::s_ModPowerMask (binary BSS 0x0024d8cc) — a uint bitmask
// that active SlashModifier instances OR their bits into each frame.
// Bit 0x20 is set by a SlashModifier whose XML declares
// <power colour_type="..."/> hashing to the zen-bounce power.
//
// Port: mask + SlashModifier::UpdateSpecific are wired, but
// PowerUpManager isn't ported yet so no modifier runs and the mask
// stays 0. The flag reads false in practice; the soft-nudge branch
// always runs. Once PowerUpManager ticks SlashModifier each frame the
// mirror-bounce lights up automatically.
static bool IsZenStrictBounceActive() {
    return (SlashEntity::s_ModPowerMask & 0x20u) != 0;
}

// Matches Fruit::DrawUpdate (0x0017501c) — called from ActorManager::Update
// immediately after Update (vtable slot 6, +0x18). Also known as
// "DrawUpdate" in per-subclass docs; same slot as Bomb::PostUpdate.
//
// Binary behaviour:
//   m_RotAxis *= 0.9                                    // DAT_0017519c damping
//   if (!m_bSliced && m_ChuckDelay <= 0) {
//     if (m_Gravity.x == 0) {                            // vertical-gravity fruit
//       if (zen && (zenFlag & 0x20)) { hard bounce x on ±192 }
//       else                         { soft nudge x toward centre }
//     } else if (m_Gravity.y == 0) {                     // horizontal-gravity fruit
//       soft nudge y toward centre on ±128
//     }
//   }
//
// Bounds resolved from binary: X = ±192 (DAT_001751a0 / 751a4),
// Y = ±128 (DAT_001751a8 / 751ac). Push / rotAxis magnitudes from
// the disassembly: vel += ±16*dt, rotAxis += ±20.
void Fruit::PostUpdate(float dt) {
    static const float ROT_AXIS_DAMPING = 0.9f;    // DAT_0017519c
    static const float BOUND_X_LO = -192.0f;       // DAT_001751a0
    static const float BOUND_X_HI =  192.0f;       // DAT_001751a4
    static const float BOUND_Y_LO = -128.0f;       // DAT_001751a8
    static const float BOUND_Y_HI =  128.0f;       // DAT_001751ac
    static const float PUSH_VEL   = 16.0f;
    static const float PUSH_ROT   = 20.0f;

    m_RotAxis *= ROT_AXIS_DAMPING;

    if (m_bSliced) return;
    if (m_ChuckDelay > 0.0f) return;

    Game* game = Game::GetInstance();
    if (!game) return;

    if (m_Gravity.x == 0.0f) {
        // Vertical-gravity fruit — nudge or hard-bounce on X bounds.
        const bool zen = (game->gameMode == 2);
        const bool zenStrict = zen && IsZenStrictBounceActive();
        if (zenStrict) {
            if (pos.x < BOUND_X_LO) { pos.x = BOUND_X_LO; vel.x = -vel.x; }
            if (pos.x > BOUND_X_HI) { pos.x = BOUND_X_HI; vel.x = -vel.x; }
        } else {
            if (pos.x < BOUND_X_LO) {
                vel.x       += dt * PUSH_VEL;
                m_RotAxis.x += PUSH_ROT;
            }
            if (pos.x > BOUND_X_HI) {
                vel.x       += dt * -PUSH_VEL;
                m_RotAxis.x -= PUSH_ROT;
            }
        }
    } else if (m_Gravity.y == 0.0f) {
        // Horizontal-gravity fruit — soft nudge on Y bounds.
        if (pos.y < BOUND_Y_LO) {
            vel.y       += dt * PUSH_VEL;
            m_RotAxis.y += PUSH_ROT;
        }
        if (pos.y > BOUND_Y_HI) {
            vel.y       += dt * -PUSH_VEL;
            m_RotAxis.y -= PUSH_ROT;
        }
    }
}

// Internal helper: draw the model once at (drawPos, drawRot, drawScale).
static void DrawOneModel(Mortar::Model* model,
                         const Vec3& drawPos,
                         const Quaternion& drawRot,
                         float s)
{
    Matrix44 mat = Matrix44::MakeScale(s, s, s);

    // No pre-quat mesh alignment. Per RE of iOS + Bada Fruit::Draw, neither
    // binary applies a coordinate fixup between Scale and Quat. The raw
    // mesh orientation (.mmd: +Z = long-axis / up) is intentional.
    Matrix44 qmat = drawRot.ToMatrix44();
    float rotMat[16];
    memcpy(rotMat, qmat.ptr(), sizeof(rotMat));
    float temp[16];
    mat4_multiply(temp, rotMat, mat.ptr());
    memcpy(mat.ptr(), temp, sizeof(temp));

    mat.GlobalTranslate44(drawPos);

    // Depth-test state is owned by the 3D actor pass in GameDraw
    // (SetDepthBuffer(1) before ActorManager::Draw, off after) -- binary
    // @ 0x0016ba10. Fruit::Draw in the binary does NOT touch GL state.
    model->Draw(mat);
}

void Fruit::Draw(Renderer& r) {
    (void)r;
    if (!IsActive() || m_ChuckDelay > 0.0f) return;
    if (!m_Model.IsValid()) return;

    float s = scale.x * m_ScaleAnim;
    if (s <= 0.0f) return;

    // Position in binary-centred ortho space.
    // See docs/engine/coordinate-system.md and FruitCamera::SetupPerspective.
    // m_bDrawWhole forces the whole-fruit branch even when m_bSliced is
    // set — used by ClearMenuItems @ 0x0016ac7c when releasing menu
    // fruits during the dojo transition (the fruit flies off as a
    // single object rather than splitting in two).
    if (!m_bSliced || m_bDrawWhole) {
        // Whole fruit — single draw at pos with m_Rot1.
        Vec3 drawPos(pos.x, pos.y, m_ZPosition);
        DrawOneModel(m_Model.Get(), drawPos, m_Rot1, s);
    } else {
        // Sliced fruit — draw two halves. Matches Fruit::Draw
        // (0x1791f4) sliced branch which loops over
        // m_pFruitModels[type]->m_HalfA / m_HalfB.
        //
        // If LoadFruitModels hasn't run or a half mesh is missing,
        // fall back to the whole-fruit mesh so something still shows.
        const FruitModelInfo* fmi = GetFruitModelInfo(m_FruitType);
        Mortar::Model* halfA = (fmi && fmi->m_HalfA.IsValid())
                             ? fmi->m_HalfA.Get() : m_Model.Get();
        Mortar::Model* halfB = (fmi && fmi->m_HalfB.IsValid())
                             ? fmi->m_HalfB.Get() : m_Model.Get();

        Vec3 drawPosA(pos.x,         pos.y,         m_ZPosition);
        Vec3 drawPosB(m_SecondPos.x, m_SecondPos.y, m_ZPosition);
        DrawOneModel(halfA, drawPosA, m_Rot1, s);
        DrawOneModel(halfB, drawPosB, m_Rot2, s);
    }
}

// Non-virtual cleanup helper called by ActorManager::Deactivate.
void Fruit::Deactivate() {
    // No Fruit-specific emitter cleanup needed here; emitters are cleared
    // by KillFruit before the entity is deactivated.
}

// Matches Fruit::KillFruit (0x00176abc).
void Fruit::KillFruit(bool doMissPenalty) {
    if (m_pEmitter1) {
        Mortar::PSPParticleManager::GetInstance().ClearEmitter(m_pEmitter1);
        m_pEmitter1 = nullptr;
    }
    if (m_pEmitter2) {
        Mortar::PSPParticleManager::GetInstance().ClearEmitter(m_pEmitter2);
        m_pEmitter2 = nullptr;
    }

    if (doMissPenalty) {
        const FruitInfoData* info = FruitInfo_Get(m_FruitType);
        if (!m_bNoPowerUp && !m_bSliced && info && info->m_Score < 5) {
            Game* g = Game::GetInstance();
            if (g) {
                if (g->gameMode == 2) {
                    // Zen mode: tracking only, no life loss.
                    // TODO: FruitSaveData::AddToTotal("zen_miss", 1) when save system is ported
                } else {
                    // Classic / Arcade miss penalty.
                    if (MissControl* mc = MissControl::GetFree()) {
                        SmartPtr<Mortar::Texture> defTex;
                        mc->MakeDisappear(pos, 0, defTex);
                    }
                    if (g->pGameSound) g->pGameSound->SFXPlay("fruit_miss", 1.0f, 1.0f);
                    g->missCount++;
                    if (g->missCount > 2) {
                        // ASM-verified: 2026-05-02 binary @ 0x00176c84 -- combo reset only inside game-over branch
                        g_ComboCount  = 0;
                        g_LastSlasher = -1;  // binary writes 0xFFFFFFFF @ 0x00176c8c
                        FN::GameOver(-1, -1.0f, -1);
                    }
                }
            }
        }
    }

    // Matches Fruit::KillFruit cleanup tail (binary @ 0x00176c8e..0x00176cea).
    // 1. Clear slasher's back-pointer if it still points at us.
    // ASM-verified: 2026-05-03 binary @ 0x00176c8e..0x00176cea (asm-inspector)
    if (m_pSlasher && m_pSlasher->m_pCurrentTarget == this) {
        m_pSlasher->m_pCurrentTarget = nullptr;
    }
    // 2. Decrement g_PowerFruitCount on natural-expiry path (flag 0x10 not yet set)
    //    AND for power-fruits (info->m_pPowers != nullptr).
    //    Binary @ 0x00176cc8..0x00176cd4: unconditional store of 0 when count<=1
    //    else (count-1). Port previously used conditional decrement which pinned
    //    the counter at 1 across multiple natural expirations.
    if (!(flags & 0x10)) {
        const FruitInfoData* killInfo = FruitInfo_Get(m_FruitType);
        if (killInfo && killInfo->m_pPowers) {
            int v = g_PowerFruitCount;
            int newv = 0;
            if (v > 1) newv = v - 1;
            g_PowerFruitCount = newv;
        }
    }
    // 3. Untrack from EntityTracker tree 0.
    if (m_TrackerID != 0) {
        ET_RemoveEntity(0, m_TrackerID);
    }

    flags |= 0x10;
}

// Matches Fruit::CheckHasGoneOffscreen (0x00175218).
// Returns true when BOTH halves are confirmed offscreen.
// Exact constants resolved from binary via read_memory.
//
// Coordinate system: X ∈ [-240, +240] (horizontal),
//                    Y ∈ [-160, +160] (vertical, +up).
//
// The "clamp" values are NOT bounces — they TELEPORT the half to the
// far side of the screen so it counts as "gone" for the kill check.
static const float OFFSCREEN_BASE      =  160.0f; // DAT_00175548
static const float WARP_CLAMP_TOP      = -320.0f; // DAT_0017554c
static const float WARP_THRESH_BOT     = -240.0f; // DAT_00175550
static const float WARP_CLAMP_BOT      =  320.0f; // DAT_00175554
static const float WARP_CLAMP_RIGHT    = -480.0f; // DAT_00175558
static const float WARP_CLAMP_LEFT     =  480.0f; // DAT_0017555c
static const float WARP_THRESH_TOP     =  240.0f; // DAT_00175560
static const float SCALE_MARGIN_MULT   =   50.0f; // DAT_00175564
static const float WARP_THRESH_RIGHT   =  360.0f; // DAT_00175568
static const float WARP_THRESH_LEFT    = -360.0f; // DAT_0017556c

// ASM-verified: 2026-04-28T00:00 binary @ 0x00175218 (asm-inspector)
bool Fruit::CheckHasGoneOffscreen() {
    const float margin = SCALE_MARGIN_MULT * scale.y;

    // === Horizontal gravity early exit (sliced + |m_Gravity.x| > 0) ===
    if (m_bSliced && fabsf(m_Gravity.x) > 0.0f) {
        float yBound = OFFSCREEN_BASE + margin;
        if (pos.y <= -yBound || pos.y >= yBound) {
            if (m_SecondPos.y <= -yBound || m_SecondPos.y >= yBound)
                return true;
        }
    }

    // === Downward gravity (m_Gravity.y < 0) ===
    bool halfA_gone = false;
    if (m_Gravity.y < 0.0f) {
        // Warp: sliced half that drifts above +240 gets teleported to
        // -320 (far below screen) so it counts as "gone" immediately.
        if (m_bSliced && pos.y > WARP_THRESH_TOP) {
            pos.y = WARP_CLAMP_TOP;
            vel.y = -1.0f;
        }
        if (m_bSliced && m_SecondPos.y > WARP_THRESH_TOP) {
            m_SecondPos.y = WARP_CLAMP_TOP;
            m_SecondVel.y = -1.0f;
        }

        float bottomBound = -(margin + OFFSCREEN_BASE);
        if (pos.y <= bottomBound && vel.y < 0.0f) {
            if (m_SliceTimer <= 0.0f &&
                m_SecondPos.y <= bottomBound && m_SecondVel.y < 0.0f)
                return true;
            halfA_gone = true;
        }

        if (m_bSliced) {
            float xBound = margin + WARP_THRESH_TOP;
            if (pos.x <= -xBound || pos.x >= xBound) {
                if (m_SecondPos.x <= -xBound || m_SecondPos.x >= xBound)
                    return true;
            }
        }
    }

    // === Upward gravity (m_Gravity.y > 0) ===
    if (m_Gravity.y > 0.0f) {
        if (m_bSliced && pos.y < WARP_THRESH_BOT) {
            pos.y = WARP_CLAMP_BOT;
            vel.y = 1.0f;
        }
        if (m_bSliced && m_SecondPos.y < WARP_THRESH_BOT) {
            m_SecondPos.y = WARP_CLAMP_BOT;
            m_SecondVel.y = 1.0f;
        }

        float topBound = margin + OFFSCREEN_BASE;
        if ((pos.y >= topBound && vel.y > 0.0f) || halfA_gone) {
            if (m_SliceTimer <= 0.0f &&
                m_SecondPos.y >= topBound && m_SecondVel.y > 0.0f)
                return true;
        }

        if (m_bSliced) {
            float xBound = margin + WARP_THRESH_TOP;
            if (pos.x <= -xBound || pos.x >= xBound) {
                if (m_SecondPos.x <= -xBound || m_SecondPos.x >= xBound)
                    return true;
            }
        }
    }

    // === Negative horizontal gravity (m_Gravity.x < 0) ===
    if (m_Gravity.x < 0.0f) {
        if (m_bSliced) {
            if (pos.x > WARP_THRESH_RIGHT) {
                pos.x = WARP_CLAMP_RIGHT;
                vel.x = -1.0f;
            }
            if (m_SecondPos.x > WARP_THRESH_RIGHT) {
                m_SecondPos.x = WARP_CLAMP_RIGHT;
                m_SecondVel.x = -1.0f;
            }
        }
        float leftBound = -(WARP_THRESH_TOP + margin);
        if ((pos.x <= leftBound && vel.x < 0.0f) || halfA_gone) {
            if (m_SliceTimer <= 0.0f &&
                m_SecondPos.x <= leftBound && m_SecondVel.x < 0.0f)
                return true;
        }
    }

    // === Positive horizontal gravity (m_Gravity.x > 0) ===
    if (m_Gravity.x > 0.0f && m_bSliced) {
        if (pos.x < WARP_THRESH_LEFT) {
            pos.x = WARP_CLAMP_LEFT;
            vel.x = 1.0f;
        }
        if (m_SecondPos.x < WARP_THRESH_LEFT) {
            m_SecondPos.x = WARP_CLAMP_LEFT;
            m_SecondVel.x = 1.0f;
        }
    }

    return false;
}

// Matches Fruit::CollisionResponse (0x1780b0). Visual-only pipeline:
//   - guard (already sliced / timer positive → ignore)
//   - critical-hit eligibility ladder (binary @ 0x001780f0..0x001781e8)
//   - critical / special-fruit branch selection for impulse clamp + timer
//   - slice angle/impulse/pos capture from bladeVel
//   - one-shot impact particle emitter rotated by blade angle
//   - persistent juice emitters from FRUIT_INFO.m_SlicedHash
//   - AddSlice visual (SliceEffect_Add)
//   - CriticalFlash full-screen tint for critical + special-fruit paths
// Skipped: SFX, achievements, score, power-ups, coins, MissControl.
void Fruit::CollisionResponse(const Vec3& bladeVel) {
    // Guard: already sliced or slice timer is positive → double-hit.
    if (m_bSliced || m_SliceTimer > -1.0f) return;

    const FruitInfoData* info = FruitInfo_Get(m_FruitType);
    const bool isSpecial  = (info && info->m_Score == 0x32);

    // ASM-verified: 2026-04-29T00:00Z binary @ 0x001780f0 (asm-inspector)
    // Critical-hit eligibility ladder (binary @ 0x001780f0..0x001781e8).
    // All gates must pass; on success roll Rand32(reroll) -- 0 == hit.
    m_bCriticalEligible = false;

    // DIFFERS: DAT_001784fc (kCritScoreBound) and DAT_00178504 (kCritResetBase)
    // are GOT globals not yet resolved by re-analyst. Using binary-plausible
    // defaults until the next re-analyst pass fills them in.
    static const int kCritScoreBound = 8;  // DIFFERS: DAT_001784fc unresolved
    static const int kCritResetBase  = 0;  // DIFFERS: DAT_00178504 unresolved

    // FruitInfo +0x318 is m_bScorable: 1 = can receive critical hit.
    const bool canCritFruit = info && info->m_bScorable;

    // DIFFERS: FruitNinjaApp gating fields (+0x05 frenzy flag, +0x10 frenzy
    // timer, +0x30 score threshold) not yet ported. m_ScoreThreshold from
    // Game::currentScore ladder is simulated with a local static counter.
    const int score = Game::GetInstance()->currentScore;

    // s_CritThreshold simulates FruitNinjaApp::m_ScoreThreshold (+0x30).
    // DIFFERS: real counter lives in FruitNinjaApp, not here.
    static int s_CritThreshold = kCritScoreBound;

    if (score >= 2 && canCritFruit /* && !frenzyFlag && frenzyTimer <= 0 */) {
        s_CritThreshold = (s_CritThreshold < 3) ? 2 : (s_CritThreshold - 1);

        const float chance = WaveManager::GetInstance()->GetCriticalChance(0);
        if (chance > 0.0f) {
            const int   bound  = (s_CritThreshold < kCritScoreBound)
                                 ? s_CritThreshold : kCritScoreBound;
            const float ratio  = (float)bound / chance;
            const uint32_t reroll = (ratio <= 1.0f) ? 1u : (uint32_t)ratio;

            const uint32_t roll = WaveManager::GetInstance()->GetRandom().Rand32(reroll);
            if (roll == 0) {
                m_bCriticalEligible = true;
                s_CritThreshold = kCritResetBase + kCritScoreBound;
            }
        }
    }

    const bool isCritical = m_bCriticalEligible;

    // Blade speed clamp. Critical / special → [6, 8]; normal → [4, 8].
    float bladeSpeed = bladeVel.length() * SLICE_BLADE_SCALE;
    const float clampMin = (isCritical || isSpecial)
                           ? 6.0f : SLICE_CLAMP_MIN_NRM;
    if (bladeSpeed < clampMin)          bladeSpeed = clampMin;
    if (bladeSpeed > SLICE_CLAMP_MAX)   bladeSpeed = SLICE_CLAMP_MAX;

    // Slice timer — base 0.03, critical × 2.5 (slow), special × 0.5 (fast).
    float sliceTimer = SLICE_TIMER_BASE;
    if (isCritical)      sliceTimer *= 2.5f;
    else if (isSpecial)  sliceTimer *= 0.5f;

    m_SliceTimer   = sliceTimer;
    m_SliceImpulse = bladeSpeed;
    m_SlicePos     = pos;
    // Atan2Idx: 16-bit angle index (65536 = 360°). Port uses std atan2
    // + the same scale factor that Atan2Idx produces.
    const float rad = atan2f(bladeVel.x, bladeVel.y);
    m_SliceAngle   = (uint16_t)((int)(rad * (65536.0f / 6.2831853f)) & 0xFFFF);

    // Impact particle emitter — one-shot, rotated by the blade direction.
    // Uses FRUIT_INFO.m_NameHash (e.g. "apple") as the template lookup. The
    // emitter's m_ScaleY / m_field30 pair encodes (cos, sin) of the rotation
    // applied to each spawned particle's initial velocity — matches binary
    // AddParticle 0x00115644. Negative-angle sign flip mirrors the binary:
    //   e->m_CosAngle =  CosIdx(-sliceAngle);
    //   e->m_SinAngle = -SinIdx(-sliceAngle);  = SinIdx(sliceAngle)
    if (info) {
        Mortar::PSPParticleManager& pm = Mortar::PSPParticleManager::GetInstance();
        const float sliceRad = (float)(int16_t)m_SliceAngle *
                               (6.2831853f / 65536.0f);
        Mortar::PSPParticleEmitter* eHit = pm.AddEmitter(
            info->m_NameHash, nullptr, /*persistent=*/false);
        if (eHit) {
            eHit->m_Pos      = pos;
            eHit->m_ScaleY   =  cosf(sliceRad);   // cos θ
            eHit->m_field30  =  sinf(sliceRad);   // sin θ
        }

        // Persistent juice emitters — one per future half. m_SlicedHash
        // resolves to "<name>_sliced" (e.g. "apple_sliced").
        m_pEmitter1 = pm.AddEmitter(info->m_SlicedHash, nullptr, /*persistent=*/true);
        m_pEmitter2 = pm.AddEmitter(info->m_SlicedHash, nullptr, /*persistent=*/true);
        if (m_pEmitter1) m_pEmitter1->m_Pos = pos;
        if (m_pEmitter2) m_pEmitter2->m_Pos = pos;
    }

    // Full-screen tint flash. Critical uses the configured crit colour
    // (gold/yellow); special-fruit uses half-alpha white. Matches
    // CriticalFlash @ 0x0016a9a4.
    if (isCritical) {
        FN::CriticalFlash(pos, Colour(255, 215, 0, 192));
    } else if (isSpecial) {
        FN::CriticalFlash(pos, Colour(255, 255, 255, 128));
    }

    // Overlay label on critical / rare slices. Pool is stubbed (GetFree
    // returns nullptr until the 9-slot MissControl pool lands in
    // GameInitialise), so this is currently a no-op — the call is wired
    // so it'll light up for free once the pool exists. See
    // docs/entities/miss-control.md and src/hud/MissControl.h.
    if (isCritical) {
        if (MissControl* mc = MissControl::GetFree())
            mc->MakeCritical(pos, 0 /* playerIdx */);
    } else if (isSpecial) {
        if (MissControl* mc = MissControl::GetFree())
            mc->MakeRare(pos);
    }

    // White slice-line visual — matches AddSlice call in binary
    // CollisionResponse at 0x17821c. Binary builds sliceInfo as:
    //   x = m_SliceAngle / -182.0 + 90.0   (degrees-offset)
    //   y = bladeSpeed * 0.4                (impulse length)
    const float sliceAngleDeg = (float)(int16_t)m_SliceAngle / -182.0f + 90.0f;
    const float sliceLength   = bladeSpeed * 0.4f;
    FN::SliceEffect_Add(pos, sliceAngleDeg, sliceLength, isCritical);

    // Matches CollisionResponse score+save dispatch (binary @ 0x00178e90..0x00178f04).
    if (info) {
        Game* g = Game::GetInstance();
        if (g) {
            const int multiplier = m_bCriticalEligible ? 2 : 1;
            // Score: multiplier and crit x2 are applied inside AddToCurrentScore
            // via GetScoreMultiplyer + Game::scoreMultDelegate.
            FN::AddToCurrentScore(info->m_Score * multiplier, (int)m_PlayerIdx,
                                  /*trackFruit=*/true, /*sendNetPacket=*/false);

            // Per-fruit-name save totals.
            if (g->pSaveData) {
                g->pSaveData->AddToTotal(info->m_TotalStatKey, info->m_TotalStatHash, 1,
                                         /*trackSession=*/false, false);
                g->pSaveData->AddToTotal(info->m_DropsKey, info->m_DropsHash, 1,
                                         /*trackSession=*/true, false);

                // On critical hit, record crit totals.
                if (isCritical) {
                    static const uint32_t hCrit      = StringHash("crit");
                    static const uint32_t hCritTotal = StringHash("crits_total");
                    g->pSaveData->AddToTotal("crit",        hCrit,      1, false, false);
                    g->pSaveData->AddToTotal("crits_total", hCritTotal, 1, true,  false);
                    char critBuf[128];
                    snprintf(critBuf, 128, "%scrit", info->m_Name);
                    g->pSaveData->AddToTotal(critBuf, StringHash(critBuf), 1, false, false);
                }
            }
        }
    }
    // TODO: Zen-mode-only save totals (first_fruit / last_fruit)

    // Combo counter increment — binary @ 0x001787a8..0x001787b0.
    // ASM-verified: 2026-05-02 binary @ 0x00178708 reads m_PlayerIdx (+0x90).
    int slasher = (int)m_PlayerIdx;
    if (g_LastSlasher != slasher) {
        g_ComboCount  = 0;   // binary @ 0x0017873a: different-player branch
        g_LastSlasher = slasher;
    }
    g_ComboCount += 1;       // binary @ 0x001787b0
}

// Matches Fruit::Slice (0x176d58), now with the binary's flipSide
// logic, special-fruit ×1.5 impulse, and spin-boost loop on both
// halves.
void Fruit::Slice() {
    m_SliceTimer = 0.0f;

    // --- flipSide determination ---
    // Binary: rotate (0,0,1) by current m_Rot1, compare XY direction
    // against m_SliceAngle via GetSmallestDelta. If the rotated Z axis
    // points away from the slice direction, flip the halves' angles.
    Vec3 slicePlane(0, 0, 1);
    // Approximate: m_Rot1.ToMatrix44() * (0,0,1) — just extract the
    // third column of the rotation matrix.
    Matrix44 rotMat = m_Rot1.ToMatrix44();
    // Third column of a column-major 4x4 is mat.m[8..10].
    slicePlane.x = rotMat.m[8];
    slicePlane.y = rotMat.m[9];
    slicePlane.z = rotMat.m[10];

    bool flipSide = false;
    if (fabsf(slicePlane.x) + fabsf(slicePlane.y) > 0.0f) {
        // 16-bit angle of the rotated-Z XY projection.
        float rotAngleRad = atan2f(slicePlane.y, slicePlane.x);
        float sliceAngleRad = (float)(int16_t)m_SliceAngle *
                              (6.2831853f / 65536.0f);
        // Wrap both into [-pi, pi] and take signed delta.
        float delta = rotAngleRad - sliceAngleRad;
        while (delta >  3.1415926f) delta -= 6.2831853f;
        while (delta < -3.1415926f) delta += 6.2831853f;
        if (delta < 0.0f) flipSide = true;
    }

    // --- Impulse ---
    float impulse = m_SliceImpulse;
    int   splatCount = (rand() % 2) + 2;   // Rand(2)+2 → 2 or 3

    // Critical hit gets 1.5× impulse + crit dual-line AddSlice.
    const bool isCritical = m_bCriticalEligible;
    if (isCritical) {
        // Binary: two slice lines at ±60° offset from the base angle.
        //   infoA.x = m_SliceAngle / -182.0 + 60.0
        //   infoB.x = m_SliceAngle / -182.0 - 60.0
        //   infoA/B.y = impulse * 0.4 * 0.7
        const float critBase = (float)(int16_t)m_SliceAngle / -182.0f;
        const float critLen  = impulse * 0.4f * 0.7f;
        FN::SliceEffect_Add(pos, critBase + 60.0f, critLen, true);
        FN::SliceEffect_Add(pos, critBase - 60.0f, critLen, true);
        impulse *= 1.5f;
        splatCount += 2;
    }

    // Special-fruit (baseScore == 0x32 = 50) also gets 1.5× impulse.
    const FruitInfoData* info = FruitInfo_Get(m_FruitType);
    if (info && info->m_Score == 0x32) {
        impulse *= 1.5f;
        splatCount += 2;
    }

    // --- Splat spawn ---
    // Per-splat speed = (impulse + rand(0.5)*impulse) * (i*0.2 + 5).
    // Per-splat angle = Rand16(0xFFF0).
    //
    // Binary uses raw impulse values directly (4..8 range from
    // CollisionResponse clamp). The port's Update integrates pos
    // with a ×60 fudge factor (matching binary 0x00177d00), which
    // means velocities should also stay in the binary's per-frame
    // scale — no extra ×50 multiplier needed here.
    const float imp_screen = impulse;
    for (int i = 0; i < splatCount; ++i) {
        const uint16_t angle16 = (uint16_t)(rand() & 0xFFF0);
        const float r          = ((float)rand() / (float)RAND_MAX) * 0.5f;
        const float speed      = (impulse + r * impulse) *
                                 ((float)i * 0.2f + 5.0f);
        const float a          = (float)angle16 * (6.2831853f / 65536.0f);
        Vec3 sv(sinf(a) * speed, cosf(a) * speed, 0.0f);

        SplatEntity* s = SplatEntity::GetFree();
        // Binary passes param3 = isCritical for crit splats (biases
        // MakeSplat's landing-type RNG toward types 4/5, the larger
        // variants).
        if (s) s->MakeSplat(pos, sv, isCritical, m_FruitType);
    }

    // Clean-Slice SFX is played by SliceEffect_Add (via AddSlice call below),
    // not directly here. Binary @ 0x0016b480 gates the SFX inside AddSlice.

    // --- Half velocities ---
    // Binary uses sliceFactor = 1 - FRUIT_INFO[+0x24c]. That field
    // isn't in the port's FruitInfo struct yet — hardcode to 0.7
    // which maps to a per-fruit slice softness of 0.3.
    const float sliceFactor = 0.7f;

    // Port the biased "rand(0x5550) retry if < 0x2aa8" pattern.
    int _ra = rand() & 0x5550; if (_ra < 0x2aa8) _ra = rand() & 0x5550;
    int _rb = rand() & 0x5550; if (_rb < 0x2aa8) _rb = rand() & 0x5550;

    // Angle offsets for the two halves — bound by `(1-softness)*4`.
    const float randA = (float)_ra * (1.0f - 0.3f) * 4.0f;
    const float randB = (float)_rb * (1.0f - 0.3f) * 4.0f;
    const int16_t offA = (int16_t)randA;
    const int16_t offB = (int16_t)randB;

    // Binary @ 0x00177236 also writes back into m_SliceAngle when flipSide is set.
    if (flipSide) {
        m_SliceAngle = (uint16_t)(m_SliceAngle + 0x7ff8);
    }
    uint16_t base = m_SliceAngle;
    uint16_t angA = (uint16_t)(base - offB + 0x7ff8);  // halfA direction always +0x7ff8 from base
    uint16_t angB = (uint16_t)(base + offA);            // halfB direction == base + offA

    const float radA = (float)(int16_t)angA * (6.2831853f / 65536.0f);
    const float radB = (float)(int16_t)angB * (6.2831853f / 65536.0f);
    Vec3 dirA(sinf(radA), cosf(radA), 0.0f);
    Vec3 dirB(sinf(radB), cosf(radB), 0.0f);

    Vec3 halfVelA = dirA * (imp_screen * sliceFactor) +
                    vel  * (1.0f - sliceFactor);
    Vec3 halfVelB = dirB * (imp_screen * sliceFactor) +
                    vel  * (1.0f - sliceFactor);

    // Critical / special override — binary @ 0x0017737a..0x00177442 uses raw
    // m_SliceAngle (NOT the offset-baked radA) with ±0x3ffc, plus int32
    // truncation on each velocity component.
    if (isCritical || (info && info->m_Score == 0x32)) {
        const float critRadA = (float)(int16_t)(uint16_t)(m_SliceAngle + 0x3ffc) * (6.2831853f / 65536.0f);
        const float critRadB = (float)(int16_t)(uint16_t)(m_SliceAngle + 0xc004) * (6.2831853f / 65536.0f);
        halfVelA = Vec3((float)(int)(sinf(critRadA) * imp_screen),
                        (float)(int)(cosf(critRadA) * imp_screen), 0.0f) * 0.5f;
        halfVelB = Vec3((float)(int)(sinf(critRadB) * imp_screen),
                        (float)(int)(cosf(critRadB) * imp_screen), 0.0f) * 0.5f;
    }

    m_SecondPos = pos;
    m_SecondVel = halfVelA;
    vel         = halfVelB;

    m_bSliced = true;

    // Reset gravity so the ramp-up in Update starts fresh.
    m_Gravity = Vec3(0.0f, -12.0f, 0.0f);

    // --- Spin boost loop (matches Fruit::Slice 0x176d58 tail) ---
    //
    // For each half i in {0, 1}:
    //   sum = |rv[i].x| + |rv[i].y| + |rv[i].z|
    //   sum *= isCritical ? 2.0 : 0.5
    //   compA = sum * (rand(0.5) + 0.75)
    //   compB = sum * (rand(0.5) + 0.75)
    //   1/4 chance: oneBig = ±compA * 1.5
    //   else:       mix    = sum * (rand(0.3) - 0.1)
    //   sign-flip compA / compB by flipSide + iteration index
    //   new m_RotVel[i] = (picked x, picked y, -compB)
    //   m_Rot[i] reset to axis-angle composition aligned with slice angle
    for (int i = 0; i < 2; ++i) {
        Vec3* rv    = (i == 0) ? &m_RotVel1 : &m_RotVel2;
        Quaternion* q = (i == 0) ? &m_Rot1 : &m_Rot2;

        float mag = fabsf(rv->x) + fabsf(rv->y) + fabsf(rv->z);
        mag *= isCritical ? 2.0f : 0.5f;

        const float r1 = ((float)rand() / (float)RAND_MAX) * 0.5f + 0.75f;
        const float r2 = ((float)rand() / (float)RAND_MAX) * 0.5f + 0.75f;
        float compA = mag * r1;
        float compB = mag * r2;

        // Sign flip — binary uses two different dice rolls per branch.
        if (flipSide) {
            if ((rand() % 5) > 1) compA = -compA;
            if (i == 1)           compA = -compA;
            if (i == 0)           compB = -compB;
        } else {
            if ((rand() % 5) < 2) compA = -compA;
            if (i != 0)           compB = -compB;
            if (i == 0)           compA = -compA;
        }

        Vec3 newRotVel;
        if ((rand() % 3) == 0) {
            // 1/4 chance (rand() % 3 == 0 is actually 1/3): oneBig
            float big = compA * 1.5f;
            if (big < 0.0f) big = -big;
            newRotVel = Vec3(big, 0.0f, -compB);
        } else {
            const float mix = ((float)rand() / (float)RAND_MAX) * 0.3f - 0.1f;
            newRotVel = Vec3(mag * mix, compA, -compB);
        }
        *rv = newRotVel;

        // Reset m_Rot[i] to a composition:
        //   Qx(axis=(1,0,0), 90°) * Qy(axis=(0,1,0), 90°) * Qz(axis=(0,0,1), sliceAngle)
        // (Binary also passes impulse=0 literals; the port uses fixed
        // 90° for the first two and m_SliceAngle in radians for Z.)
        const float half = 1.5707963f * 0.5f;
        const float sliceRad = (float)(int16_t)m_SliceAngle *
                               (6.2831853f / 65536.0f) * 0.5f;
        Quaternion qx(sinf(half), 0.0f, 0.0f, cosf(half));
        Quaternion qy(0.0f, sinf(half), 0.0f, cosf(half));
        Quaternion qz(0.0f, 0.0f, sinf(sliceRad), cosf(sliceRad));
        *q = (qx * qy * qz).normalized();
    }

}

// Matches Fruit::RotateFacingUp (0x001757f4).
// Sets m_Rot1/m_Rot2 to a fixed starting orientation (facing up) then
// optionally applies an alignment rotation. Sets m_RotVel1/m_RotVel2
// to spinVelAxis * random magnitude.
//
// RandomStartAngle (0x00175740): sets rot to axis=(-1,0,0),
//   angle16=0xce2c via CreateFromAxisAngle (0x0017ac68), then resets to
//   Identity if w==0.
//
// Spin magnitude: +(2 + RandF(2.0)) or -(2 + RandF(2.0)). Binary uses
//   WaveManager's Random instance; port substitutes rand() since this
//   only affects display orientation, not gameplay.
void Fruit::RotateFacingUp(bool alignToFacing, const Vec3& spinVelAxis) {
    // Random spin magnitude: +(2 + rand[0,2)) or -(2 + rand[0,2))
    // Matches: r = RandF(2.0); sign = (Rand32(2)==0) ? 1 : -1
    float r    = (float)rand() / (float)RAND_MAX * 2.0f;   // RandF(2.0)
    float sign = (rand() % 2 == 0) ? 1.0f : -1.0f;         // Rand32(2) == 0
    float magnitude = sign * (2.0f + r);

    for (int i = 0; i < 2; i++) {
        Quaternion* rot    = (i == 0) ? &m_Rot1 : &m_Rot2;
        Vec3*       rotVel = (i == 0) ? &m_RotVel1 : &m_RotVel2;

        // Step 1: RandomStartAngle(rot, fixedAxis=true)
        // Binary: Identity, then CreateFromAxisAngle(-1, 0, 0, 0xce2c).
        // halfAngle = (0xce2c >> 1) / 65536.0 * 2pi
        {
            float halfAngle = (float)(0xce2c >> 1) / 65536.0f * 6.2831853f;
            float c = cosf(halfAngle), s = sinf(halfAngle);
            *rot = Quaternion(-1.0f * s, 0.0f, 0.0f, c);
            if (rot->w == 0.0f) *rot = Quaternion::Identity();
        }

        // Step 2: optional facing-up alignment (alignToFacing=false at MP call site)
        if (alignToFacing) {
            // qA: axis=spinVelAxis, angle=0 --> Identity (sin(0)=0)
            Quaternion qA = Quaternion::Identity();

            // qB: axis=(0, 0, zSign), angle = 0x4e34
            // ARM idiom: zSign = -1 when (spinVelAxis.x + spinVelAxis.y >= 0), else +1
            float zSign = (spinVelAxis.x + spinVelAxis.y >= 0.0f) ? -1.0f : 1.0f;
            {
                float halfAngle = (float)(0x4e34 >> 1) / 65536.0f * 6.2831853f;
                float c = cosf(halfAngle), s = sinf(halfAngle);
                Quaternion qB(0.0f, 0.0f, zSign * s, c);
                *rot = (*rot * qA) * qB;
            }
        }

        // Step 3: rotation velocity = spinVelAxis * random magnitude
        *rotVel = spinVelAxis * magnitude;
    }
}

// Matches Fruit::FruitType (0x00175b10).
// Searches FRUIT_INFO array by hash of name, matching m_NameHash or
// m_NameHashUpper. Returns index on match. If not found and
// fallbackRandom=true returns a random valid index, else -1.
int Fruit::FruitType(const char* name, bool fallbackRandom) {
    const int count = FruitInfo_GetCount();
    if (name && *name) {
        const uint32_t hash = StringHash(name);
        for (int i = 0; i < count; i++) {
            const FruitInfoData* info = FruitInfo_Get(i);
            if (info && (info->m_NameHash == hash || info->m_NameHashUpper == hash)) {
                return i;
            }
        }
    }
    if (fallbackRandom && count > 0) {
        // Binary uses WaveManager's RNG; port uses rand() — behaviorally
        // equivalent since the result is just a fallback fruit index.
        return rand() % count;
    }
    return -1;
}


// Matches Fruit::LoadInfo (0x17987c, 519 lines) — called once from GameInitialise step 24
void Fruit::LoadInfo() {
    Game* game = Game::GetInstance();
    if (!game) return;

    std::string xmlPath = game->data_dir + "/xml/fruitlist.xml";
    FruitInfo_Load(xmlPath.c_str());
}

// --- FruitModelInfo global array ---------------------------------------
//
// Binary allocates a flat FruitModelInfo[fruitCount] array at
// LoadFruitModels time and stores the pointer in the FRUIT_INFO
// header block at +0xc8. Port keeps it as a module-local vector
// sized once by LoadFruitModels.

static std::vector<FruitModelInfo> s_FruitModels;
static bool s_FruitModelsLoaded = false;

// Matches Fruit::LoadFruitModels (0x1794e0). Walks every FRUIT_INFO
// entry and loads `<name>_<c>_piece_1.mmd` + `_piece_2.mmd` via
// MeshManager. The format string was resolved from DAT_0017986c at
// 0x001bcd43: "models/Fruit/%s_%c_piece_%d.mmd" where %c is the first
// letter of the fruit name.
void Fruit::LoadFruitModels() {
    if (s_FruitModelsLoaded) return;

    Game* game = Game::GetInstance();
    if (!game) return;

    Mortar::MeshManager* meshMgr = Mortar::MeshManager::GetInstance();
    if (!meshMgr) return;

    const int n = FruitInfo_GetCount();
    s_FruitModels.clear();
    s_FruitModels.resize((size_t)n);

    int loaded = 0;
    for (int i = 0; i < n; ++i) {
        const FruitInfoData* info = FruitInfo_Get(i);
        if (!info || !info->m_ModelName[0]) continue;

        const char* name = info->m_ModelName;
        const char  c    = name[0];
        char path[256];

        for (int piece = 1; piece <= 2; ++piece) {
            snprintf(path, sizeof(path),
                     "%s/models/Fruit/%s_%c_piece_%d.mmd",
                     game->data_dir.c_str(), name, c, piece);
            SmartPtr<Mortar::Model> m = meshMgr->Load(path);
            if (piece == 1) s_FruitModels[i].m_HalfA = m;
            else            s_FruitModels[i].m_HalfB = m;
        }

        if (s_FruitModels[i].m_HalfA.IsValid()) {
            ++loaded;
            // Assign fruit_atlas texture to the half meshes — same
            // pattern as whole-fruit mesh in Fruit::Init.
            static SmartPtr<Mortar::Texture> s_fruitAtlas;
            if (!s_fruitAtlas.IsValid()) {
                std::string texPath = game->data_dir
                                    + "/models/fruit/textures/fruit_atlas.tex";
                s_fruitAtlas = Mortar::TextureManager::GetInstance().Load(texPath.c_str());
            }
            if (s_fruitAtlas.IsValid()) {
                for (int h = 0; h < 2; ++h) {
                    Mortar::Model* mod = (h == 0 ? s_FruitModels[i].m_HalfA.Get()
                                                  : s_FruitModels[i].m_HalfB.Get());
                    if (!mod) continue;
                    for (size_t m = 0; m < mod->m_Meshes.size(); ++m) {
                        if (mod->m_Meshes[m].IsValid() &&
                            !mod->m_Meshes[m]->HasDiffuseTexture())
                        {
                            mod->m_Meshes[m]->SetDiffuseTexture(s_fruitAtlas);
                        }
                    }
                }
            }
        }
    }

    s_FruitModelsLoaded = true;
    (void)0; // (verbose load log removed)
    if (false) printf("[Fruit] LoadFruitModels: loaded half meshes for %d/%d fruit types\n",
           loaded, n);
}

const FruitModelInfo* Fruit::GetFruitModelInfo(int fruitType) {
    if (!s_FruitModelsLoaded) return nullptr;
    if (fruitType < 0 || fruitType >= (int)s_FruitModels.size()) return nullptr;
    return &s_FruitModels[fruitType];
}

// Binary @ 0x00176564
// 4-path weighted selector: {crit,normal} x {includeOnSide,avail-only}.
// Lazy-init cumulative tables on first call via m_CumWeight / m_CumCritWeight cache fields.
// Field usage (asm-inspector verified offsets):
//   "available" gate  -> m_CoinsMax < 1  (+0x328, int)
//   "critical"  gate  -> m_bScorable     (+0x318, byte)
//   m_bSpecial (+0x319) is NOT read by this function.
static int s_TotalWeight     = 0;
static int s_TotalAvail      = 0;
static int s_TotalCrit       = 0;
static int s_TotalCritAvail  = 0;

// ASM-verified: 2026-05-03T09:42 binary @ 0x00176564..0x001766f4 (asm-inspector)
int Fruit::RandomFruit(bool includeOnSide) {
    if (s_TotalWeight < 1) {
        int wT = 0, wA = 0, wC = 0, wCA = 0;
        const int count = FruitInfo_GetCount();
        FruitInfoData* arr = FruitInfo_GetArray();
        for (int i = 0; i < count; ++i) {
            FruitInfoData* fi = &arr[i];
            wT += fi->m_Chance;
            fi->m_CumWeight = wT;
            if (fi->m_CoinsMax < 1) wA += fi->m_Chance;       // binary +0x328
            if (fi->m_bScorable) {                             // binary +0x318
                wC += fi->m_Chance;
                if (fi->m_CoinsMax < 1) wCA += fi->m_Chance;  // binary +0x328
            }
            fi->m_CumCritWeight = wC;
        }
        s_TotalWeight    = wT;
        s_TotalAvail     = wA;
        s_TotalCrit      = wC;
        s_TotalCritAvail = wCA;
    }

    bool isCrit = WaveManager::GetInstance()->CriticalMode(0);
    Math::Random* rng = &WaveManager::GetInstance()->m_Random;
    const int count = FruitInfo_GetCount();

    if (!isCrit) {
        if (includeOnSide) {
            uint32_t r = rng->Rand32((uint32_t)s_TotalWeight);
            for (int i = 0; i < count; ++i)
                if (r < (uint32_t)FruitInfo_Get(i)->m_CumWeight) return i;
        } else {
            uint32_t r = rng->Rand32((uint32_t)s_TotalAvail);
            int acc = 0;
            for (int i = 0; i < count; ++i) {
                const FruitInfoData* fi = FruitInfo_Get(i);
                if (fi->m_CoinsMax < 1) {                      // binary +0x328
                    acc += fi->m_Chance;
                    if (r < (uint32_t)acc) return i;
                }
            }
        }
    } else {
        if (includeOnSide) {
            uint32_t r = rng->Rand32((uint32_t)s_TotalCrit);
            for (int i = 0; i < count; ++i)
                if (r < (uint32_t)FruitInfo_Get(i)->m_CumCritWeight) return i;
        } else {
            uint32_t r = rng->Rand32((uint32_t)s_TotalCritAvail);
            int acc = 0;
            for (int i = 0; i < count; ++i) {
                const FruitInfoData* fi = FruitInfo_Get(i);
                if (fi->m_CoinsMax < 1 && fi->m_bScorable) {   // binary +0x328, +0x318
                    acc += fi->m_Chance;
                    if (r < (uint32_t)acc) return i;
                }
            }
        }
    }
    return (int)rng->Rand32((uint32_t)(count - 1));
}

// Matches Fruit::GetNumActiveForPlayer (0x00122a00).
// TODO: playerIdx filtering not ported; counts all active fruits.
int Fruit::GetNumActiveForPlayer(int /*playerIdx*/, bool /*checkBombs*/) {
    ActorManager* am = ActorManager::GetInstance();
    if (!am) return 0;
    return am->GetNumEntities(0);
}

// Matches Fruit::ClearUnspawned (0x001762a0).
void Fruit::ClearUnspawned(bool deactivateVisible) {
    ActorManager* am = ActorManager::GetInstance();
    if (!am) return;
    std::list<Entity*>::iterator it;
    Entity* e = am->GetEntityFirst(0, it);
    while (e) {
        Fruit* f = static_cast<Fruit*>(e);
        Entity* next_e = am->GetEntityNext(0, it);
        if (f->m_ChuckDelay > 0.0f || deactivateVisible)
            am->Deactivate(f);
        e = next_e;
    }
}

// Matches Fruit::Disable (binary @ 0x00126374): one-byte store of 1 to +0x3D.
// ASM-verified: 2026-05-03 binary @ 0x00126374 (asm-inspector)
void Fruit::Disable(Fruit* f) {
    f->m_bNoPowerUp = 1;
}

// Matches Fruit::DrawShadows (0x00178f28) + AddShadow (0x00175ea0).
// Texture: fruit_shadow.tex (loaded by FruitInfo_Load step 0).
// Geometry: 1 fade-out quad while spawning, 2 half-quads when active.
// Buffer: 64 fruits * 3 quads max * 6 verts = 1152 verts (binary uses 18432 stack).
static const int SHADOW_MAX_FRUITS = 64;
static QUADCUSTOMVERTEX s_ShadowVerts[SHADOW_MAX_FRUITS * 3 * 6];

void Fruit::DrawShadows() {
    Mortar::Texture* shadowTex = FruitInfo_GetShadowTex();
    if (!shadowTex) return;

    QUADCUSTOMVERTEX* w = s_ShadowVerts;
    int count = 0;

    ActorManager* am = ActorManager::GetInstance();
    if (!am) return;
    std::list<Entity*>::iterator it;
    Entity* e = am->GetEntityFirst(0, it);
    while (e && count + 3 <= SHADOW_MAX_FRUITS * 3) {
        Fruit* f = static_cast<Fruit*>(e);
        if (f->scale.x > 0.0f) f->AddShadow(&w, &count);
        e = am->GetEntityNext(0, it);
    }
    if (count == 0) return;

    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();

    shadowTex->Set();
    if (Renderer* r = Renderer::GetInstance())
        r->DrawTriList(s_ShadowVerts, count * 6);
    shadowTex->UnSet();
}

// Writes up to 3 quads (shadow geometry) for one fruit into the shared buffer.
// Matches Fruit::AddShadow @ 0x00175ea0.
//
// Binary DAT constants:
//   DAT_00176164 = 230.0f  (spawn-fade alpha mult)
//   DAT_00176168 = 82.0f   (whole-fruit shadow half-size base)
//   DAT_0017616c = -0.65f  (whole-shadow offset mult)
//   DAT_00176170 = 100.0f  (post-spawn alpha mult)
//   DAT_00176174 = 50.0f   (sliced-half shadow half-size base)
//   DAT_00176178 = -0.45f  (sliced-half offset mult)
//   DAT_00176180 = Vec3(0,0,1)  (slice-plane axis BSS singleton, never reassigned)
//   DAT_00175e9c = -5000.0f    (shadow Z, written by AddQuad)
static void AddQuad(QUADCUSTOMVERTEX** out, float cx, float cy, float w, float h, Colour col) {
    // Corner layout:
    //   v0: (cx-w, cy-h)    v3: (cx+w, cy-h)
    //   v1: (cx-w, cy+h)    v4: (cx-w, cy+h)
    //   v2: (cx+w, cy-h)    v5: (cx+w, cy+h)
    // All verts: z = -5000 (DAT_00175e9c), normal (0,0,1), u in {0,1}, v in {0,1}.
    const float z    = -5000.0f;   // DAT_00175e9c
    const uint32_t c = ((uint32_t)col.a << 24)
                     | ((uint32_t)col.b << 16)
                     | ((uint32_t)col.g <<  8)
                     | ((uint32_t)col.r);

    QUADCUSTOMVERTEX* v = *out;

    v[0] = { cx - w, cy - h, z,   0.0f, 0.0f, 1.0f,   c,   0.0f, 0.0f };
    v[1] = { cx - w, cy + h, z,   0.0f, 0.0f, 1.0f,   c,   0.0f, 1.0f };
    v[2] = { cx + w, cy - h, z,   0.0f, 0.0f, 1.0f,   c,   1.0f, 0.0f };
    v[3] = { cx + w, cy - h, z,   0.0f, 0.0f, 1.0f,   c,   1.0f, 0.0f };
    v[4] = { cx - w, cy + h, z,   0.0f, 0.0f, 1.0f,   c,   0.0f, 1.0f };
    v[5] = { cx + w, cy + h, z,   0.0f, 0.0f, 1.0f,   c,   1.0f, 1.0f };

    *out += 6;
}

// ============================================================
// Chunk A: pure-data accessors
// ============================================================

// Binary @ 0x00174f18
const char* Fruit::FruitTypeName(long type) {
    const FruitInfoData* info = FruitInfo_Get((int)type);
    return info ? info->m_Name : nullptr;
}

// Binary @ 0x00174f38
unsigned long Fruit::FruitTypeHash(long type) {
    const FruitInfoData* info = FruitInfo_Get((int)type);
    return info ? (unsigned long)info->m_NameHash : 0UL;
}

// Binary @ 0x00174f5c
const char* Fruit::FruitFactTexture(long type) {
    const FruitInfoData* info = FruitInfo_Get((int)type);
    return info ? info->m_FactTexture : nullptr;
}

// Binary @ 0x00174f80
Colour Fruit::FruitTypeColour(long type) {
    // TODO: 0x00174fbc/0x00174fc0 — special-fruit colour override (StarFruit etc.); default specialIdx=-1 means no override
    const FruitInfoData* info = FruitInfo_Get((int)type);
    if (!info) return Colour(255, 255, 255, 255);
    return Colour(info->m_FruitColour[0], info->m_FruitColour[1],
                  info->m_FruitColour[2], info->m_FruitColour[3]);
}

// Binary @ 0x00174fc8
Colour Fruit::FruitFactColour(long type) {
    const FruitInfoData* info = FruitInfo_Get((int)type);
    if (!info) return Colour(255, 255, 255, 255);
    return Colour(info->m_FactColour[0], info->m_FactColour[1],
                  info->m_FactColour[2], info->m_FactColour[3]);
}

// Binary @ 0x00174ff8
const ::FruitInfo* Fruit::FruitInfo(long type) {
    return FruitInfo_Get((int)type);
}

// ============================================================
// Chunk B: small helpers
// ============================================================

// Binary @ 0x00176184 — local-MP "did a player drop their last life" check.
// Port: FN::GameOver is wired; the multi-player player-count gate is not yet
// ported (GetNumActiveForPlayer doesn't filter by player yet). Stub the
// per-player gating and call GameOver when count hits zero for player 0.
void Fruit::CheckFruitDropped() {
    // TODO: 0x00176184 — per-player live-count gate (local-MP); needs
    // GetNumActiveForPlayer player filtering + per-player life tracking.
    // For now: single-player path only.
    if (GetNumActiveForPlayer(0, false) == 0) {
        FN::GameOver(-1, -1.0f, -1);
    }
}

// Binary @ 0x00175624 — "is either half outside the play field" predicate.
// Play-field bounds match PostUpdate (±192 X, ±128 Y) plus 50*scale margin.
bool Fruit::IsOffscreen() const {
    const float margin = 50.0f * scale.y;
    const float xBound = 192.0f + margin;
    const float yBound = 128.0f + margin;

    if (pos.x < -xBound || pos.x > xBound) return true;
    if (pos.y < -yBound || pos.y > yBound) return true;
    if (m_bSliced) {
        if (m_SecondPos.x < -xBound || m_SecondPos.x > xBound) return true;
        if (m_SecondPos.y < -yBound || m_SecondPos.y > yBound) return true;
    }
    return false;
}

// Binary @ 0x00176354
void Fruit::EnableCollision(bool enable) {
    if (enable) {
        const FruitInfoData* info = FruitInfo_Get(m_FruitType);
        const float fScale   = info ? info->m_Scale          : 25.0f;
        const float fColBase = info ? info->m_CollisionScale : 1.0f;
        const float radius   = fColBase + COL_RADIUS_FACTOR * fScale;
        if (!m_Col) m_Col = new Mortar::ColSphere();
        Mortar::ColSphere* cs = static_cast<Mortar::ColSphere*>(m_Col);
        cs->center = Vec3(pos.x, pos.y, 0.0f);
        cs->radius = radius;
    } else {
        delete m_Col;
        m_Col = nullptr;
    }
}

// Binary @ 0x00175b78
void Fruit::SetForPlayer(int playerIdx) {
    m_PlayerIdx = (uint32_t)playerIdx;
    // Defunct: online-mp — P2 collision radius *= 0.66; binary @ 0x00175b78
    // Mortar::NetworkManager::GetInstance().IsOnlineMultiplayer() is always false in port.
    if (Mortar::NetworkManager::GetInstance()->IsOnlineMultiplayer()) {
        if (playerIdx == 1 && m_Col) {
            static_cast<Mortar::ColSphere*>(m_Col)->radius *= 0.66f;
        }
    }
}

// Binary @ 0x001761d8 — virtual Entity::Release override.
// Called by ActorManager teardown before the destructor.
void Fruit::Release() {
    if (m_pEmitter1) {
        Mortar::PSPParticleManager::GetInstance().ClearEmitter(m_pEmitter1);
        m_pEmitter1 = nullptr;
    }
    if (m_pEmitter2) {
        Mortar::PSPParticleManager::GetInstance().ClearEmitter(m_pEmitter2);
        m_pEmitter2 = nullptr;
    }
    if (m_pSlasher && m_pSlasher->m_pCurrentTarget == this) {
        m_pSlasher->m_pCurrentTarget = nullptr;
    }
    m_pSlasher = nullptr;
    Entity::Release();
}

// ============================================================
// Chunk C: GetFact + SetTrailParticles
// ============================================================

// Binary @ 0x00175ba4 — fact-of-the-day picker with save-data round-robin
// and exclude-special-fruits remap.
const char* Fruit::GetFact(int* outType, int* outFactIdx, int fruitType, int factIdx) {
    const int count = FruitInfo_GetCount();
    if (count <= 0) return nullptr;

    // Remap fruitType: skip special-fruit entries (m_bSpecial != 0).
    // Binary walks the array and builds a non-special subset for indexing.
    int ft = fruitType;
    if (ft < 0 || ft >= count) ft = 0;

    // Advance to a fruit that has facts.
    int attempts = 0;
    while (attempts < count) {
        const FruitInfoData* info = FruitInfo_Get(ft);
        if (info && !info->m_bSpecial && info->m_FactCount > 0) break;
        ft = (ft + 1) % count;
        ++attempts;
    }

    const FruitInfoData* chosen = FruitInfo_Get(ft);
    if (!chosen || chosen->m_FactCount <= 0) return nullptr;

    int fi = factIdx % chosen->m_FactCount;

    if (outType)    *outType    = ft;
    if (outFactIdx) *outFactIdx = fi;

    // TODO: FruitSaveData::AddToTotal — defer save-tracking; pick fact via raw modulo.
    // Binary calls FruitSaveData::AddToTotal("facts", 1) and
    // FruitSaveData::AddToTotal("<fruit>_fact", 1) here.

    return chosen->m_pFacts ? chosen->m_pFacts[fi] : nullptr;
}

// Binary @ 0x001756dc — replace m_pEmitter1 with a custom trail emitter.
bool Fruit::SetTrailParticles(unsigned long emitterHash) {
    Mortar::PSPParticleManager& pm = Mortar::PSPParticleManager::GetInstance();

    // Binary: EmitterExists check before replace. Port uses FindTemplate
    // as the equivalent gate (AddEmitter returns nullptr for unknown hashes).
    if (!pm.FindTemplate((uint32_t)emitterHash)) return false;

    if (m_pEmitter1) {
        pm.ClearEmitter(m_pEmitter1);
        m_pEmitter1 = nullptr;
    }
    m_pEmitter1 = pm.AddEmitter((uint32_t)emitterHash, nullptr, /*persistent=*/true);
    if (m_pEmitter1) m_pEmitter1->m_Pos = pos;
    return m_pEmitter1 != nullptr;
}

// ============================================================
// Chunk D: UpdateBombAvoidance + DestroyFruitModels
// ============================================================

// Binary @ 0x00175988 — push bombs away from this fruit on the X axis
// when they are within 70px and have matching velocity direction.
void Fruit::UpdateBombAvoidance(float dt) {
    static const float AVOIDANCE_RADIUS = 70.0f;   // from binary @ 0x00175988

    ActorManager* am = ActorManager::GetInstance();
    if (!am) return;

    std::list<Entity*>::iterator it;
    Entity* e = am->GetEntityFirst(1, it);   // entity type 1 = bomb
    while (e) {
        if (e->IsActive()) {
            Vec3 diff = e->pos - pos;
            float dist = fabsf(diff.x);
            if (dist < AVOIDANCE_RADIUS) {
                // Push bomb away on X axis when velocities agree in direction.
                float pushDir = (diff.x >= 0.0f) ? 1.0f : -1.0f;
                if ((e->vel.x * pushDir) >= 0.0f) {
                    e->vel.x += pushDir * dt * 16.0f;
                }
            }
        }
        e = am->GetEntityNext(1, it);
    }
}

// Binary @ 0x0017911c — releases the FruitModelInfo[] array.
void Fruit::DestroyFruitModels() {
    s_FruitModels.clear();
    s_FruitModelsLoaded = false;
    // TODO: 0x0017911c — per-MP-player SmartPtr cleanup pending FruitModelInfo struct expansion
    // (m.slot[p+4] for p=0..3; requires FruitModelInfo extended to 0x24 bytes)
}

// ============================================================

void Fruit::AddShadow(QUADCUSTOMVERTEX** out, int* outCount) {
    // DIFFERS: m_PlayerIdx not ported; same-screen MP shadow mirror skipped.
    const float mirrorX = 1.0f;
    const float mirrorY = 0.0f;   // DAT_00176160

    // Quad 1: spawn-fade whole-fruit shadow (active while m_ScaleAnim < 1).
    if (m_ScaleAnim < 1.0f) {
        int a = (int)((1.0f - m_ScaleAnim) * 230.0f);   // DAT_00176164
        uint8_t al = (a < 1) ? 0 : (a > 254 ? 255 : (uint8_t)a);
        float hs = 82.0f * scale.x;                      // DAT_00176168
        float ox = mirrorY * hs * -0.65f;                // DAT_0017616c
        float oy = mirrorX * hs * -0.65f;
        AddQuad(out, pos.x + ox, pos.y + oy, hs, hs, Colour(255, 255, 255, al));
        ++(*outCount);
    }

    // Quads 2+3: per-half shadows (active while m_ScaleAnim > 0).
    if (m_ScaleAnim > 0.0f) {
        int a = (int)(m_ScaleAnim * 100.0f);             // DAT_00176170
        uint8_t al = (a < 1) ? 0 : (a > 254 ? 255 : (uint8_t)a);
        float hs = scale.x * 50.0f;                      // DAT_00176174
        Vec3 axis(0.0f, 0.0f, 1.0f);                    // DAT_00176180 BSS singleton (0,0,1)

        float ox = mirrorY * hs * -0.45f;                // DAT_00176178
        float oy = mirrorX * hs * -0.45f;

        // Binary calls Quaternion::Matrix33Unit on each rot, then multiplies axis.
        // Port uses ToMatrix44() and extracts the 3x3 rotation applied to axis.
        // For axis=(0,0,1): rotated = col2 of the rotation matrix = (m[8], m[9], m[10]).
        {
            Matrix44 mat1 = m_Rot1.ToMatrix44();
            Vec3 anchorA = pos + Vec3(
                mat1.m[0]*axis.x + mat1.m[4]*axis.y + mat1.m[8]*axis.z,
                mat1.m[1]*axis.x + mat1.m[5]*axis.y + mat1.m[9]*axis.z,
                mat1.m[2]*axis.x + mat1.m[6]*axis.y + mat1.m[10]*axis.z
            ) * 0.5f;
            AddQuad(out, anchorA.x + ox, anchorA.y + oy, hs, hs, Colour(255, 255, 255, al));
            ++(*outCount);

            Matrix44 mat2 = m_Rot2.ToMatrix44();
            Vec3 anchorB = m_SecondPos + Vec3(
                mat2.m[0]*axis.x + mat2.m[4]*axis.y + mat2.m[8]*axis.z,
                mat2.m[1]*axis.x + mat2.m[5]*axis.y + mat2.m[9]*axis.z,
                mat2.m[2]*axis.x + mat2.m[6]*axis.y + mat2.m[10]*axis.z
            ) * 0.5f;
            AddQuad(out, anchorB.x + ox, anchorB.y + oy, hs, hs, Colour(255, 255, 255, al));
            ++(*outCount);
        }
    }
}
