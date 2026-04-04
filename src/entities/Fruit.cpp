#include "Fruit.h"
#include "Renderer.h"
#include "Game.h"
#include "math3d.h"
#include <cstdlib>
#include <cstdio>
#include <cmath>

// Random float in [-range, +range]
static float RandRange(float range) {
    return ((float)rand() / RAND_MAX * 2.0f - 1.0f) * range;
}

Fruit::Fruit()
    : m_FruitType(0), m_bSliced(false),
      m_ScaleAnim(0.0f), m_ChuckDelay(0.0f), m_ZPosition(0.0f),
      m_MeshTex(0) {
    entityType = 0;
}

Fruit::~Fruit() {
    m_Mesh.destroy();
}

void Fruit::Init(int param1, int fruitType, int param3) {
    (void)param1; (void)param3;
    m_FruitType = fruitType;
    m_bSliced = false;
    m_ScaleAnim = 0.0f;
    m_ChuckDelay = 0.0f;
    active = true;
    flags &= ~0x10;  // unhide

    // Random rotation velocities (matches original: [-5.5, 5.5] per component)
    m_RotVel1 = Vec3(RandRange(5.5f), RandRange(5.5f), RandRange(5.5f));
    m_RotVel2 = Vec3(RandRange(5.5f), RandRange(5.5f), RandRange(5.5f));

    // Random start rotation
    m_Rot1 = Quaternion::FromAxisAngle(Vec3(1, 0, 0), RandRange(3.14f));
    m_Rot2 = m_Rot1;

    // Default gravity (downward in game coords)
    m_Gravity = Vec3(0.0f, -400.0f, 0.0f);

    // Rotation axis offset
    m_RotAxis = Vec3(RandRange(10.0f), 0.0f, 0.0f);

    // DIFFERS: original computes visual scale in SetFruitType as:
    //   globalVec (BSS, unreadable) × configFloat (GOT_OFF_FRUIT_SCALE_CONFIG) × 0.01 (FRUIT_VISUAL_SCALE_MULT)
    // The 25.0 from FRUIT_INFO is m_CollisionScale — collision radius only, NOT visual.
    // Using 1.0 as placeholder until globalVec/configFloat are resolved at runtime.
    // SetFruitType would set this properly; for now approximate.
    scale = Vec3(1.0f, 1.0f, 1.0f);

    // Load mesh for this fruit type
    static const char* fruitNames[] = {
        "apple", "banana", "orange", "watermelon", "strawberry",
        "kiwifruit", "pineapple", "plum", "pear", "mango",
        "apple_red", "lime", "dragon", "coconut", "passionfruit", "lemon"
    };
    Game* game = Game::GetInstance();
    if (game && fruitType >= 0 && fruitType < 16) {
        std::string meshPath = game->data_dir + "/models/fruit/" + fruitNames[fruitType] + "_single.mmd";
        if (m_Mesh.load(meshPath)) {
            // Load fruit atlas texture (shared — TODO: cache this)
            if (!m_MeshTex) {
                TexImage img;
                std::string texPath = game->data_dir + "/models/fruit/textures/fruit_atlas.tex";
                if (tex_load(texPath, img)) {
                    m_MeshTex = game->renderer.upload_texture(img);
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

    // Kill if off-screen
    if (CheckOffscreen()) {
        Deactivate();
    }
}

void Fruit::Draw(Renderer& r) {
    if (!active || m_ChuckDelay > 0.0f) return;
    if (!m_Mesh.vbo || !m_MeshTex) return;

    float s = scale.x * m_ScaleAnim;
    if (s <= 0.0f) return;

    // Build model matrix: Scale * Rotation * Translation
    float scl[16], rotMat[16], sr[16], trans[16], model[16];
    mat4_scale(scl, s, s, s);

    // Convert quaternion to matrix
    Matrix44 qmat = m_Rot1.ToMatrix44();
    memcpy(rotMat, qmat.ptr(), sizeof(rotMat));

    mat4_multiply(sr, rotMat, scl);

    // Position in original centered coords
    // HUDControl3d::Draw adds Vec3(480,320,0) offset, so fruit pos matches button pos
    mat4_translate(trans, pos.x, pos.y, m_ZPosition);
    mat4_multiply(model, trans, sr);

    // Original centered ortho: SetupOrtho(160, -160, -240, 240, 2000, -6000)
    float proj[16];
    memset(proj, 0, sizeof(proj));
    float left = 160.0f, right = -160.0f;
    float bottom = -240.0f, top = 240.0f;
    float nearVal = 2000.0f, farVal = -6000.0f;
    proj[0]  = 2.0f / (right - left);
    proj[5]  = 2.0f / (top - bottom);
    proj[10] = -2.0f / (farVal - nearVal);
    proj[12] = -(right + left) / (right - left);
    proj[13] = -(top + bottom) / (top - bottom);
    proj[14] = -(farVal + nearVal) / (farVal - nearVal);
    proj[15] = 1.0f;

    float mvp[16];
    mat4_multiply(mvp, proj, model);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    r.draw_mesh(m_Mesh, m_MeshTex, mvp, model, m_ScaleAnim);
    glDisable(GL_DEPTH_TEST);
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
