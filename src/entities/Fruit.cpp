#include "Fruit.h"
#include "FruitInfo.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "asset/tex_loader.h"
#include "Game.h"
#include "math/math3d.h"
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
        // DIFFERS: globalScaleVec is a runtime BSS value that cannot be read from
        // the static binary. Approximated as (2.75, 2.75, 2.75) from visual matching
        // against reference screenshots. Formula: target_size / (mesh_extent * xml_scale * 0.01 * 0.2)
        static const Vec3 globalScaleVec(2.75f, 2.75f, 2.75f);
        const FruitInfo* info = FruitInfo_Get(fruitType);
        float fruitScale = info ? info->scale * 0.01f : 1.0f;
        scale = globalScaleVec * fruitScale;
    }

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
    // Original uses MatrixManager world stack, same as HUD controls.
    // Position includes the (480, 320) offset from HUDControl3d space
    // since fruit entities are positioned at button.pos which is in HUD coords.
    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();

    Matrix44 mat = Matrix44::MakeScale(s, s, s);

    // Quaternion rotation
    Matrix44 qmat = m_Rot1.ToMatrix44();
    float rotMat[16];
    memcpy(rotMat, qmat.ptr(), sizeof(rotMat));
    // Multiply: mat = rotMat * mat (rotation applied to scaled)
    Matrix44 sr;
    float temp[16];
    mat4_multiply(temp, rotMat, mat.ptr());
    memcpy(sr.ptr(), temp, sizeof(temp));

    // Port specific: add (480, 320) offset to match HUDControl3d coordinate space.
    // In the original, the EGL pipeline handles this; in the port, the offset-centered
    // ortho requires all draw positions to include the (480, 320) base.
    Vec3 drawPos(pos.x + 480.0f, pos.y + 320.0f, m_ZPosition);
    sr.GlobalTranslate44(drawPos);

    mm.GetWorldStack().SetCurrentMatrix(sr);
    mm.UploadModelViewOnly();

    // Use MatrixManager's MVP (projection from FruitCamera, view, world)
    Matrix44 mvp = mm.GetMVP();
    float model[16];
    memcpy(model, sr.ptr(), sizeof(model));

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    r.setup_3d_shader(m_MeshTex, mvp.ptr(), model, m_ScaleAnim);
    m_Mesh.draw();
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
