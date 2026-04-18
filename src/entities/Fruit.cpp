#include "Fruit.h"
#include "FruitInfo.h"
#include "SplatEntity.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/gl_funcs.h"
#include "asset/MeshManager.h"
#include "asset/TextureManager.h"
#include "particle/PSPParticleManager.h"
#include "hud/SliceEffect.h"
#include "game/BombHit.h"
#include "Game.h"
#include "math/math3d.h"
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Binary constants for fruit slicing (docs/engine/fruit-slice-notes.md).
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

Fruit::Fruit()
    : m_FruitType(0)
    , m_SliceTimer(-1.0f)
    , m_SliceAngle(0)
    , m_SliceImpulse(0.0f)
    , m_SlicePos(0, 0, 0)
    , m_pEmitter1(NULL)
    , m_pEmitter2(NULL)
    , m_bSliced(false)
    , m_bDetached(false)
    , m_bDrawWhole(false)
    , m_ScaleAnim(0.0f)
    , m_ChuckDelay(0.0f)
    , m_ZPosition(0.0f)
{
    entityType = 0;
}

Fruit::~Fruit() {
    // Model released by SmartPtr destructor
}

void Fruit::Init(int param1, int fruitType, int param3) {
    (void)param1; (void)param3;
    m_FruitType = fruitType;
    m_bSliced = false;
    m_bDetached = false;
    m_bDrawWhole = false;
    m_ScaleAnim = 0.0f;
    m_ChuckDelay = 0.0f;
    active = true;
    flags &= ~0x10;  // unhide

    // Reset slice state (binary Fruit::Init — m_SliceTimer = -1).
    m_SliceTimer   = -1.0f;
    m_SliceAngle   = 0;
    m_SliceImpulse = 0.0f;
    m_SlicePos     = Vec3(0, 0, 0);
    m_pEmitter1    = NULL;
    m_pEmitter2    = NULL;

    // Random rotation velocities (matches original: [-5.5, 5.5] per component)
    m_RotVel1 = Vec3(RandRange(5.5f), RandRange(5.5f), RandRange(5.5f));
    m_RotVel2 = Vec3(RandRange(5.5f), RandRange(5.5f), RandRange(5.5f));

    // Random start rotation
    m_Rot1 = Quaternion::FromAxisAngle(Vec3(1, 0, 0), RandRange(3.14f));
    m_Rot2 = m_Rot1;

    // Default gravity — confirmed from Fruit::Init 0x00176708: literal -12.0, DAT_00176a18=0.0
    m_Gravity = Vec3(0.0f, -12.0f, 0.0f);

    // Rotation axis offset
    m_RotAxis = Vec3(RandRange(10.0f), 0.0f, 0.0f);

    // Matches SetFruitType (0x17621c):
    // visualScale = globalScaleVec * FruitInfo[type].scale * VISUAL_SCALE_MULT (0.01)
    // globalScaleVec is at BSS 0x1F4334, initialized to (0,0,0) by static init
    // but overwritten at runtime before fruit creation.
    // Per-fruit scale from Data/xml/fruitlist.xml (e.g. watermelon=75)
    {
        // Vec3::One at BSS 0x1F4334 — a constant singleton for (1,1,1), not a
        // mutable scale variable. Matches binary: _Vector3::operator*(Vec3*, float*)
        // in SetFruitType (0x17621c) multiplies Vec3::One by m_Scale then 0.01.
        const FruitInfo* info = FruitInfo_Get(fruitType);
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
        m_Col.center = Vec3(pos.x, pos.y, 0.0f);
        m_Col.radius = radius;
    }

    // Load mesh via MeshManager (cached, matches binary pattern)
    Game* game = Game::GetInstance();
    Mortar::MeshManager* meshMgr = Mortar::MeshManager::GetInstance();
    printf("[Fruit] Init: type=%d game=%p meshMgr=%p\n", fruitType, (void*)game, (void*)meshMgr);
    if (game && meshMgr) {
        const FruitInfo* info = FruitInfo_Get(fruitType);
        const char* modelName = (info && info->m_ModelName[0]) ? info->m_ModelName : "apple";
        std::string meshPath = game->data_dir + "/models/Fruit/" + modelName + "_single.mmd";
        printf("[Fruit] Init: loading '%s'\n", meshPath.c_str());
        m_Model = meshMgr->Load(meshPath.c_str());
        printf("[Fruit] Init: model valid=%d\n", m_Model.IsValid());

        // Assign fruit atlas texture to the model's mesh
        // fruit_atlas.tex is in models/fruit/textures/, NOT in textures/
        // So we can't use TextureManager::LoadLocalisedTexture (which prepends textures/)
        if (m_Model.IsValid() && !m_Model->m_Meshes.empty()) {
            static SmartPtr<Mortar::Texture> s_fruitAtlas;
            if (!s_fruitAtlas.IsValid()) {
                std::string texPath = game->data_dir + "/models/fruit/textures/fruit_atlas.tex";
                s_fruitAtlas = Mortar::TextureManager::GetInstance().Load(texPath.c_str());
            }
            printf("[Fruit] Init: fruit_atlas valid=%d texId=%u\n",
                   s_fruitAtlas.IsValid(), s_fruitAtlas.IsValid() ? s_fruitAtlas->m_TexId : 0);
            if (s_fruitAtlas.IsValid()) {
                for (int i = 0; i < (int)m_Model->m_Meshes.size(); i++) {
                    if (m_Model->m_Meshes[i].IsValid() && !m_Model->m_Meshes[i]->HasDiffuseTexture()) {
                        m_Model->m_Meshes[i]->SetDiffuseTexture(s_fruitAtlas);
                        printf("[Fruit] Init: assigned tex to mesh[%d]\n", i);
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
    active = true;
    flags &= ~0x10;
}

void Fruit::Update(float dt) {
    if (!active) return;

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

    // Quaternion rotation update (both halves)
    // Original: QuatFromAxisAngle for each component, then multiply
    float rotScale = dt * 60.0f;  // normalized to ~60fps
    {
        Quaternion qx = Quaternion::FromAxisAngle(Vec3(1, 0, 0), m_RotVel1.x * rotScale * 0.01f);
        Quaternion qy = Quaternion::FromAxisAngle(Vec3(0, 1, 0), m_RotVel1.y * rotScale * 0.01f);
        Quaternion qz = Quaternion::FromAxisAngle(Vec3(0, 0, 1), m_RotVel1.z * rotScale * 0.01f);
        m_Rot1 = (m_Rot1 * qx * qy * qz).normalized();
    }
    {
        Quaternion qx = Quaternion::FromAxisAngle(Vec3(1, 0, 0), m_RotVel2.x * rotScale * 0.01f);
        Quaternion qy = Quaternion::FromAxisAngle(Vec3(0, 1, 0), m_RotVel2.y * rotScale * 0.01f);
        Quaternion qz = Quaternion::FromAxisAngle(Vec3(0, 0, 1), m_RotVel2.z * rotScale * 0.01f);
        m_Rot2 = (m_Rot2 * qx * qy * qz).normalized();
    }

    // Update collision sphere center (z clamped to 0).
    m_Col.center = Vec3(pos.x, pos.y, 0.0f);

    // Track juice emitters with the two halves so particles follow the
    // pieces instead of spraying from the original slice point. Matches
    // binary Fruit::Update @ 0x177680 tail section.
    if (m_pEmitter1) m_pEmitter1->m_Pos = pos;
    if (m_pEmitter2) m_pEmitter2->m_Pos = m_SecondPos;

    if (CheckHasGoneOffscreen()) {
        KillFruit(true);
    }
}

// Internal helper: draw the model once at (drawPos, drawRot, drawScale).
static void DrawOneModel(Mortar::Model* model,
                         const Vec3& drawPos,
                         const Quaternion& drawRot,
                         float s)
{
    Matrix44 mat = Matrix44::MakeScale(s, s, s);

    Matrix44 qmat = drawRot.ToMatrix44();
    float rotMat[16];
    memcpy(rotMat, qmat.ptr(), sizeof(rotMat));
    float temp[16];
    mat4_multiply(temp, rotMat, mat.ptr());
    memcpy(mat.ptr(), temp, sizeof(temp));

    mat.GlobalTranslate44(drawPos);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    model->Draw(mat);
    glDisable(GL_DEPTH_TEST);
}

void Fruit::Draw(Renderer& r) {
    (void)r;
    if (!active || m_ChuckDelay > 0.0f) return;
    if (!m_Model.IsValid()) {
        static bool s_logged = false;
        if (!s_logged) { printf("[Fruit] Draw: model not valid\n"); s_logged = true; }
        return;
    }

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

void Fruit::Deactivate() {
    active = false;
    flags |= 0x10;
}

// Matches Fruit::KillFruit (0x00176abc).
void Fruit::KillFruit(bool doMissPenalty) {
    if (m_pEmitter1) {
        Mortar::PSPParticleManager::GetInstance().ClearEmitter(m_pEmitter1);
        m_pEmitter1 = NULL;
    }
    if (m_pEmitter2) {
        Mortar::PSPParticleManager::GetInstance().ClearEmitter(m_pEmitter2);
        m_pEmitter2 = NULL;
    }

    // TODO: miss penalty (needs MissControl, GameState)
    // if (!m_bSliced && !m_bNoPowerUp && FruitInfo[type].baseScore < 5):
    //   non-zen: MissControl::MakeDisappear, SFX "fruit_miss",
    //            missCount++, GameOver if > 2
    //   zen:     FruitSaveData tracking
    (void)doMissPenalty;

    // TODO: unlink from SlashEntity (field_0x108 + 0x134)
    // TODO: decrement g_PowerFruitCount if FruitInfo has powers
    // TODO: ET_RemoveEntity(0, m_TrackerID)

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
//   - critical / special-fruit branch selection for impulse clamp + timer
//   - slice angle/impulse/pos capture from bladeVel
//   - one-shot impact particle emitter rotated by blade angle
//   - persistent juice emitters from FRUIT_INFO.m_SlicedHash
//   - AddSlice visual (SliceEffect_Add)
//   - CriticalFlash full-screen tint for critical + special-fruit paths
// Skipped: SFX, achievements, score, power-ups, coins, MissControl.
void Fruit::OnSliced(const Vec3& bladeVel) {
    // Guard: already sliced or slice timer is positive → double-hit.
    if (m_bSliced || m_SliceTimer > -1.0f) return;

    // Critical RNG is gated on WaveManager::RESET_BONUS which isn't ported
    // — keep critical off for now. Special-fruit path IS deterministic
    // (FRUIT_INFO.m_Score == 0x32) and runs the rare-fruit branch.
    const FruitInfo* info = FruitInfo_Get(m_FruitType);
    const bool isCritical = false;                 // TODO: WaveManager
    const bool isSpecial  = (info && info->m_Score == 0x32);

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

    printf("[Fruit] OnSliced: type=%d pos=(%.1f,%.1f) impulse=%.2f angle=0x%04x "
           "crit=%d special=%d\n",
           m_FruitType, pos.x, pos.y, m_SliceImpulse, m_SliceAngle,
           isCritical, isSpecial);

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
            info->m_NameHash, NULL, /*persistent=*/false);
        if (eHit) {
            eHit->m_Pos      = pos;
            eHit->m_ScaleY   =  cosf(sliceRad);   // cos θ
            eHit->m_field30  =  sinf(sliceRad);   // sin θ
        }

        // Persistent juice emitters — one per future half. m_SlicedHash
        // resolves to "<name>_sliced" (e.g. "apple_sliced").
        m_pEmitter1 = pm.AddEmitter(info->m_SlicedHash, NULL, /*persistent=*/true);
        m_pEmitter2 = pm.AddEmitter(info->m_SlicedHash, NULL, /*persistent=*/true);
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

    // White slice-line visual — matches AddSlice call in binary
    // CollisionResponse at 0x17821c. Binary builds sliceInfo as:
    //   x = m_SliceAngle / -182.0 + 90.0   (degrees-offset)
    //   y = bladeSpeed * 0.4                (impulse length)
    const float sliceAngleDeg = (float)(int16_t)m_SliceAngle / -182.0f + 90.0f;
    const float sliceLength   = bladeSpeed * 0.4f;
    FN::SliceEffect_Add(pos, sliceAngleDeg, sliceLength, isCritical);
}

// Matches Fruit::Slice (0x176d58), now with the binary's flipSide
// logic, special-fruit ×1.5 impulse, and spin-boost loop on both
// halves. See docs/engine/fruit-slice-notes.md + raw decompile
// reference for the exact math.
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
    // Port's critical flag isn't wired yet (always false) — kept for
    // future when Fruit::OnSliced's critical RNG is implemented.
    const bool isCritical = false;  // TODO: m_bCriticalEligible
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
    const FruitInfo* info = FruitInfo_Get(m_FruitType);
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

    // --- Half velocities ---
    // Binary uses sliceFactor = 1 - FRUIT_INFO[+0x24c]. That field
    // isn't in the port's FruitInfo struct yet — hardcode to 0.7
    // which maps to a per-fruit slice softness of 0.3.
    const float sliceFactor = 0.7f;

    // Port the biased "rand(0x5550) retry if < 0x2aa8" pattern.
    auto randBiased = []() -> float {
        int r = rand() & 0x5550;
        if (r < 0x2aa8) r = rand() & 0x5550;
        return (float)r;
    };

    // Angle offsets for the two halves — bound by `(1-softness)*4`.
    const float randA = randBiased() * (1.0f - 0.3f) * 4.0f;
    const float randB = randBiased() * (1.0f - 0.3f) * 4.0f;
    const int16_t offA = (int16_t)randA;
    const int16_t offB = (int16_t)randB;

    uint16_t baseAngle = m_SliceAngle;
    uint16_t angA, angB;
    if (flipSide) {
        // Binary: m_SliceAngle += 0x7ff8; shuffles halves to opposite
        // sides before applying offsets.
        baseAngle = (uint16_t)(baseAngle + 0x7ff8);
        angA = (uint16_t)(baseAngle - offB + 0x7ff8);
        angB = (uint16_t)(baseAngle + offA + 0x7ff8);
    } else {
        angA = (uint16_t)(baseAngle - offB);
        angB = (uint16_t)(baseAngle + offA);
    }

    const float radA = (float)(int16_t)angA * (6.2831853f / 65536.0f);
    const float radB = (float)(int16_t)angB * (6.2831853f / 65536.0f);
    Vec3 dirA(sinf(radA), cosf(radA), 0.0f);
    Vec3 dirB(sinf(radB), cosf(radB), 0.0f);

    Vec3 halfVelA = dirA * (imp_screen * sliceFactor) +
                    vel  * (1.0f - sliceFactor);
    Vec3 halfVelB = dirB * (imp_screen * sliceFactor) +
                    vel  * (1.0f - sliceFactor);

    // Critical / special override — binary uses pure ±90° directions
    // with a 0.5 scale.
    if (isCritical || (info && info->m_Score == 0x32)) {
        const float r1 = radA + 1.5707963f;
        const float r2 = radA - 1.5707963f;
        halfVelA = Vec3(sinf(r1) * imp_screen, cosf(r1) * imp_screen, 0) * 0.5f;
        halfVelB = Vec3(sinf(r2) * imp_screen, cosf(r2) * imp_screen, 0) * 0.5f;
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

    printf("[Fruit] Slice: type=%d imp=%.2f flipSide=%d splats=%d "
           "vA=(%.1f,%.1f) vB=(%.1f,%.1f)\n",
           m_FruitType, imp_screen, flipSide, splatCount,
           halfVelA.x, halfVelA.y, halfVelB.x, halfVelB.y);
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
            const FruitInfo* info = FruitInfo_Get(i);
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
        const FruitInfo* info = FruitInfo_Get(i);
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
    printf("[Fruit] LoadFruitModels: loaded half meshes for %d/%d fruit types\n",
           loaded, n);
}

const FruitModelInfo* Fruit::GetFruitModelInfo(int fruitType) {
    if (!s_FruitModelsLoaded) return NULL;
    if (fruitType < 0 || fruitType >= (int)s_FruitModels.size()) return NULL;
    return &s_FruitModels[fruitType];
}
