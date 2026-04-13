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
#include "Game.h"
#include "math/math3d.h"
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <string>

// Binary constants for fruit slicing (docs/engine/fruit-slice-notes.md).
// Resolved from DATs near CollisionResponse (0x1780b0) and Slice (0x176d58).
static const float SLICE_TIMER_BASE    = 0.03f;   // DAT_001784dc
static const float SLICE_BLADE_SCALE   = 0.1f;    // DAT_001784e0
static const float SLICE_CLAMP_MIN_NRM = 4.0f;    // normal fruit clamp
static const float SLICE_CLAMP_MAX     = 8.0f;
// Fruit::SetFruitType (0x17621c) collision radius formula:
//   radius = base + 0.52 * collisionScale
// with `base` read from FRUIT_INFO+0x248 (default 1.0) and collisionScale
// from +0x244 (parsed as "collision" attr, default 25.0).
static const float COL_RADIUS_FACTOR   = 0.52f;   // DAT_00176340
static const float COL_RADIUS_BASE     = 1.0f;    // FRUIT_INFO+0x248 default

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

        // Collision sphere (SetFruitType @ 0x17621c):
        //   radius = (base + COL_RADIUS_FACTOR * collisionScale) * fruitScale
        // where base = 1.0 and COL_RADIUS_FACTOR = 0.52.
        const float colScale = info ? info->m_CollisionScale : 25.0f;
        const float radius   = (COL_RADIUS_BASE + COL_RADIUS_FACTOR * colScale)
                             * fruitScale;
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
        // Ballistic flight: pos += vel*dt, vel += gravity*dt
        pos += vel * dt;
        vel += m_Gravity * dt;

        // Rotation axis drift
        pos += m_RotAxis * dt;

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
        // Gravity ramp-up (original: gravGrowth = DAT * (dt/FRAME_TIME) * 4.5)
        float gravLen = m_Gravity.length();
        float growRate = 4.5f;
        Vec3 gravDir = m_Gravity.normalized();
        m_Gravity = gravDir * (gravLen + growRate * dt * 60.0f);

        // Two-body physics
        vel += m_Gravity * dt;
        pos += vel * dt;
        m_SecondVel += m_Gravity * dt;
        m_SecondPos += m_SecondVel * dt;
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

    // Kill if off-screen
    if (CheckOffscreen()) {
        Deactivate();
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
    if (!m_bSliced) {
        // Whole fruit — single draw at pos with m_Rot1.
        Vec3 drawPos(pos.x, pos.y, m_ZPosition);
        DrawOneModel(m_Model.Get(), drawPos, m_Rot1, s);
    } else {
        // Sliced fruit — draw two halves. In the binary each half has
        // its own mesh (`_half_a.mmd` / `_half_b.mmd`) loaded via
        // Fruit::LoadFruitModels (0x1794e0). Those aren't ported yet,
        // so the port reuses the whole fruit mesh for both halves — the
        // two copies visibly split apart along m_Rot1/m_Rot2 and drift
        // with halfVelA/halfVelB, which still reads as "fruit was cut".
        // TODO: swap in real half meshes once LoadFruitModels is ported.
        Vec3 drawPosA(pos.x,        pos.y,        m_ZPosition);
        Vec3 drawPosB(m_SecondPos.x, m_SecondPos.y, m_ZPosition);
        DrawOneModel(m_Model.Get(), drawPosA, m_Rot1, s);
        DrawOneModel(m_Model.Get(), drawPosB, m_Rot2, s);
    }
}

void Fruit::Deactivate() {
    active = false;
    flags |= 0x10;
}

bool Fruit::CheckOffscreen() const {
    // Check if fruit has fallen below or gone far beyond screen
    if (pos.y < -200.0f) return true;   // below screen
    if (pos.y > 800.0f) return true;    // way above
    if (pos.x < -200.0f || pos.x > 680.0f) return true;  // sides
    return false;
}

// Matches Fruit::CollisionResponse (0x1780b0), minimal v1 port.
// Full pipeline in docs/engine/fruit-slice-notes.md. This subset covers:
//   - guard (already sliced / timer positive → ignore)
//   - slice angle/impulse/pos capture from bladeVel
//   - juice emitter spawn from FRUIT_INFO.m_SlicedHash
//   - timer set to SLICE_TIMER_BASE — Update counts down → Slice()
// Skipped for v1: critical hit chance, SFX, AddSlice visual, achievement,
// score, power-up spawn, coin spawn, MissControl, online multiplayer.
void Fruit::OnSliced(const Vec3& bladeVel) {
    // Guard: already sliced or slice timer is positive → double-hit.
    if (m_bSliced || m_SliceTimer > -1.0f) return;

    // Clamp blade impulse to [4, 8] (normal fruit). Binary also uses
    // [6, 8] for critical + special fruit — not ported here.
    float bladeSpeed = bladeVel.length() * SLICE_BLADE_SCALE;
    if (bladeSpeed < SLICE_CLAMP_MIN_NRM) bladeSpeed = SLICE_CLAMP_MIN_NRM;
    if (bladeSpeed > SLICE_CLAMP_MAX)     bladeSpeed = SLICE_CLAMP_MAX;

    m_SliceTimer   = SLICE_TIMER_BASE;    // 0.03f countdown to split
    m_SliceImpulse = bladeSpeed;
    m_SlicePos     = pos;
    // Atan2Idx: 16-bit angle index (65536 = 360°). Port uses std atan2
    // + the same scale factor that Atan2Idx produces.
    const float rad = atan2f(bladeVel.x, bladeVel.y);
    m_SliceAngle   = (uint16_t)((int)(rad * (65536.0f / 6.2831853f)) & 0xFFFF);

    printf("[Fruit] OnSliced: type=%d pos=(%.1f,%.1f) impulse=%.2f angle=0x%04x\n",
           m_FruitType, pos.x, pos.y, m_SliceImpulse, m_SliceAngle);

    // Spawn the juice particle emitters (one per future half).
    // FRUIT_INFO.m_SlicedHash is computed by FruitInfo_Load as
    // StringHash("%s_sliced") — the binary uses this to look up the
    // per-fruit juice particle template (e.g. "apple_sliced"). If no
    // matching template exists AddEmitter returns NULL, which is fine.
    const FruitInfo* info = FruitInfo_Get(m_FruitType);
    if (info) {
        Mortar::PSPParticleManager& pm = Mortar::PSPParticleManager::GetInstance();
        m_pEmitter1 = pm.AddEmitter(info->m_SlicedHash, NULL, /*persistent=*/true);
        m_pEmitter2 = pm.AddEmitter(info->m_SlicedHash, NULL, /*persistent=*/true);
        if (m_pEmitter1) m_pEmitter1->m_Pos = pos;
        if (m_pEmitter2) m_pEmitter2->m_Pos = pos;
    }

    // White slice-line visual — matches AddSlice call in binary
    // CollisionResponse at 0x17821c. Binary passes a (angle_deg, impulse_scaled)
    // pair; port keeps the raw 16-bit angle and flat impulse for simplicity.
    FN::SliceEffect_Add(pos, m_SliceAngle, m_SliceImpulse, /*critical=*/false);
}

// Matches Fruit::Slice (0x176d58), minimal v1 port. Splits the fruit into
// two halves moving perpendicular to the blade direction.
//   halfVelA = sin/cos(sliceAngle + 90°) * impulse * sliceFactor + vel * (1-sliceFactor)
//   halfVelB = sin/cos(sliceAngle - 90°) * impulse * sliceFactor + vel * (1-sliceFactor)
// sliceFactor = 1 - fruitInfo[+0x24c]; we don't have that field ported yet,
// so we use a fixed sliceFactor of 0.7 which gives a clean split visual.
//
// Skipped: splat spawning, crit dual-line AddSlice, "special fruit" path,
// spin boost, quaternion axis-angle composition.
void Fruit::Slice() {
    const float sliceFactor = 0.7f;

    // Convert 16-bit angle to radians. +0x3ffc = +90°, -0x3ffc = -90°.
    const float baseRad = (float)(int16_t)m_SliceAngle * (6.2831853f / 65536.0f);
    const float rad1 = baseRad + 1.5707963f;   // +90°
    const float rad2 = baseRad - 1.5707963f;   // -90°

    // Binary uses (SinIdx(angle), CosIdx(angle), 0) — note sin-first, not cos.
    // Atan2Idx also takes (y, x) order, so this gives a consistent rotation.
    Vec3 dir1(sinf(rad1), cosf(rad1), 0.0f);
    Vec3 dir2(sinf(rad2), cosf(rad2), 0.0f);

    const float imp = m_SliceImpulse * 50.0f;   // scale for screen-space velocities
    Vec3 halfVelA = dir1 * (imp * sliceFactor) + vel * (1.0f - sliceFactor);
    Vec3 halfVelB = dir2 * (imp * sliceFactor) + vel * (1.0f - sliceFactor);

    // First half = original body; second half = m_Second*.
    m_SecondPos = pos;
    m_SecondVel = halfVelA;
    vel         = halfVelB;

    m_bSliced = true;

    // Reset gravity so the ramp-up in Update starts fresh.
    m_Gravity = Vec3(0.0f, -12.0f, 0.0f);

    // Spawn 2–3 juice splat entities. Matches the for-loop at the tail
    // of binary Slice() (0x176d58) — count is `Rand(2) + 2`, each
    // splat has a random angle, randomly decayed speed, and is tinted
    // by the fruit colour. Simplified here: fixed 3 splats, angles
    // evenly spread around the slice direction.
    for (int i = 0; i < 3; ++i) {
        const float a = baseRad + ((float)i - 1.0f) * 0.9f;  // ±0.9 rad spread
        const float speed = m_SliceImpulse * 35.0f + (float)(rand() % 20) * 2.0f;
        Vec3 sv(sinf(a) * speed, cosf(a) * speed, 0.0f);
        SplatEntity* s = SplatEntity::GetFree();
        if (s) s->MakeSplat(pos, sv, m_FruitType);
    }

    printf("[Fruit] Slice: type=%d imp=%.2f vA=(%.1f,%.1f) vB=(%.1f,%.1f)\n",
           m_FruitType, imp, halfVelA.x, halfVelA.y, halfVelB.x, halfVelB.y);
}

// Matches Fruit::LoadInfo (0x17987c, 519 lines) — called once from GameInitialise step 24
void Fruit::LoadInfo() {
    Game* game = Game::GetInstance();
    if (!game) return;

    std::string xmlPath = game->data_dir + "/xml/fruitlist.xml";
    FruitInfo_Load(xmlPath.c_str());
}
