#include "FruitRay.h"

#include "Fruit.h"
#include "ActorManager.h"
#include "asset/Texture.h"
#include "asset/Mesh.h"
#include "math/Colour.h"
#include "math/Random.h"
#include "math/MathUtil.h"
#include "game/GameWork.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "render/MatrixManager.h"
#include <list>

// Binary @ 0x001d954c -- base Entity ctor only; no field priming here (Init
// primes every field). entityType is set here to match the CreateEntity(6)
// factory path (mirrors Coin/Jiblet ctors, which set their own entityType).
FruitRay::FruitRay()
    : m_WorldMatrix()
    , m_StartMatrix()
    , m_pSourceFruit(0)
    , m_Phase(0.0f)
    , m_Life(0.0f)
    , m_ColourEnd(0.0f, 0.0f, 0.0f)
    , m_ColourStart(0.0f, 0.0f, 0.0f)
    , m_Expiring(0)
{
    entityType = 6;
}

FruitRay::~FruitRay() {}

// ASM-spec v1.6.1 FruitRay::Init @0x001e4740
void FruitRay::Init(Fruit* src, Quaternion /*rot*/) {
    m_pSourceFruit = src;
    flags &= 0xEE;   // clear ENT_INACTIVE(0x01) + ENT_KILLED(0x10)
    m_Expiring = 0;
    // m_ColourStart = Vec3::One * (rand01()*50 + 70) -- per-spawn random brightness.
    float brightness = Math::g_Random.RandF(1.0f) * 50.0f + 70.0f;
    m_ColourStart = _Vector3<float>::One() * brightness;
    m_Phase = 0.0f;
    m_ColourEnd = _Vector3<float>::One() * 40.0f;
    scale = m_ColourEnd;   // scale (Entity+0x28) doubles as m_ColourCurrent; see FruitRay.h note
    m_Life = 1.0f;
    pos = src->pos;
    // NOTE: the `rot` param is NOT used to build either matrix -- binary
    // leaves both at identity here; orientation comes from
    // m_pSourceFruit->m_Rot1 every Update.
    m_StartMatrix = Matrix44();   // identity
    m_WorldMatrix = Matrix44();   // identity
}

// ASM-spec v1.6.1 FruitRay::Update @0x001e45e0
void FruitRay::Update(float dt) {
    if (!m_Expiring) {
        Fruit* f = m_pSourceFruit;
        m_Phase += game_work.dt;             // fixed dt (NOT the param)
        pos = f->pos;
        m_WorldMatrix = f->m_Rot1.ToMatrix44();
        float t = m_Phase / 0.15f;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        scale.x = Lerp(m_ColourStart.x, m_ColourEnd.x, t);
        scale.y = Lerp(m_ColourStart.y, m_ColourEnd.y, t);
        scale.z = Lerp(m_ColourStart.z, m_ColourEnd.z, t);
    } else {
        m_Life += dt * -1.6f;
        m_pSourceFruit = 0;
        if (m_Life <= 0.0f) {
            flags |= ENT_KILLED;   // request removal
        }
    }
}

Mortar::SmartPtr<Mortar::Texture> FruitRay::RayTexture;

// ASM-verified: 2026-07-24T00:00Z v1.6.1 FruitRay::DrawRay @0x001e48b8 (asm-inspector)
// (scale-fold m_Life*-2+3, Scale*Start*World*translate order, alpha clamp all MATCH)
//
// Builds a 3-vertex QUADCUSTOMVERTEX strip (a thin ray "fan" -- one wide
// vertex + two narrow ones) and draws it with the ray's world matrix.
//
// Per-vertex fields (confirmed from ASM @0x1e48c4-0x1e4950):
//   pos = (0,0,0), normal = (0,0,1), colour = Colour(255,255,255,alpha).PlatformColour()
//   where alpha = clamp(m_Life*255, 0, 255).
//   v-coordinate (QUADCUSTOMVERTEX+0x20): vert[0] = 1.0f, vert[1]/vert[2] = 0.05f
//   (0x3d4ccccd) -- the "thin ray fan" shape. u-coordinate (+0x1c) is left
//   at whatever the (zero-initialised) stack held -- binary never writes it
//   in this loop, so it is 0.0f here for parity.
//
// Transform chain (confirmed from ASM @0x1e4958-0x1e4a18):
//   lengthFactor = m_Life * -2.0f + 3.0f
//   scaledVec    = scale (Entity+0x28) * lengthFactor      (_Vector3::operator*(T))
//   m            = Scale44(scaledVec)                      -- diag-scale matrix from scaledVec
//   m            = m * m_StartMatrix
//   m            = m * m_WorldMatrix
//   m.GlobalTranslate44(pos)                                (Entity+0x10)
// TODO: asm-inspect the Vec3-taking Scale44(Vec3*, Matrix44* out) overload
// (binary @0x0015f518 and siblings resolve to PLT thunks in the current
// Ghidra view, not an inline body) -- port uses Matrix44::MakeScale(scaledVec)
// as the byte-faithful equivalent (diag(sx,sy,sz,1) from a Vec3), matching
// every other MakeScale(Vec3) call site in the port, but the exact thunk
// target hasn't been ASM-diffed against this port body yet.
void FruitRay::DrawRay() {
    if (!RayTexture.IsValid()) return;

    QUADCUSTOMVERTEX verts[3];
    for (int i = 0; i < 3; ++i) {
        verts[i].x = 0.0f;
        verts[i].y = 0.0f;
        verts[i].z = 0.0f;
        verts[i].nx = 0.0f;
        verts[i].ny = 0.0f;
        verts[i].nz = 1.0f;
        verts[i].u = 0.0f;
        verts[i].v = (i == 0) ? 1.0f : 0.05f;

        float alpha = m_Life * 255.0f;
        Colour c(255, 255, 255, (alpha > 0.0f) ? (uint8_t)(int)alpha : 0);
        verts[i].colour = c.PlatformColour();
    }

    float lengthFactor = m_Life * -2.0f + 3.0f;
    _Vector3<float> scaledVec = scale * lengthFactor;

    Matrix44 m = Matrix44::MakeScale(scaledVec);
    m = m * m_StartMatrix;
    m = m * m_WorldMatrix;
    m.GlobalTranslate44(pos);

    RayTexture->Set();

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().SetCurrentMatrix(m);
    mm.UploadModelViewOnly();

    Mortar::Mesh::DrawTriStrip(verts, 3, false, NULL);

    RayTexture->UnSet(true);
}

// ASM-spec v1.6.1 FruitRay::DrawRays @0x001e4ac4
// Static batch: walks every ActorManager type-6 (FruitRay) entity and draws
// it. Called from GameDraw (v1.6.1 @0x001cd9d4), not from ActorManager::Draw's
// per-entity vtable dispatch.
void FruitRay::DrawRays() {
    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (!am) return;

    std::list<Mortar::Entity*>::iterator it;
    Mortar::Entity* e = am->GetEntityFirst(6, it);
    while (e != NULL) {
        static_cast<FruitRay*>(e)->DrawRay();
        e = am->GetEntityNext(6, it);
    }
}
