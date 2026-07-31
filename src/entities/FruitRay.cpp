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

// ASM-spec v1.6.1 FruitRay::DrawRay @0x001e48b8
// (Re-read instruction-by-instruction 2026-07-30. The previous "ASM-verified"
// stamp is withdrawn: it only covered the 0x1e48d4-0x1e494c loop and missed
// the seven post-loop `vstr` stores at 0x1e498c-0x1e49b0 that land INSIDE the
// vertex array, so the port built a degenerate zero-area triangle and the
// super-fruit rays never rendered. Restamp to ASM-verified only after an
// asm-inspector cross-compile diff of this body.)
//
// Draws ONE ray: a 3-vertex QUADCUSTOMVERTEX tri-strip -- an apex at the local
// origin widening to a 0.5-unit base one unit out along +Z.
//
// Stack map (sub sp,#0x148): the vertex array is at sp+0x48, stride 0x24.
//   v0 = sp+0x48  v1 = sp+0x6c  v2 = sp+0x90   (DrawTriStrip gets sp+0x48, n=3)
//
// Loop @0x1e48d4-0x1e494c (r4 = sp+0x60 = &v[i].colour, post-inc 0x24) writes
// the fields common to all three vertices:
//   +0x00/04/08 x,y,z   = 0.0f   (pool 0x1e4aac = 0x00000000)
//   +0x0c/10    nx,ny   = 0.0f
//   +0x14       nz      = 1.0f
//   +0x18       colour  = Colour(255,255,255,alpha).PlatformColour()
//                         alpha = m_Life*255 via vcvt.u32.f32 (saturates <0 to 0)
//   +0x20       v       = 1.0f for i==0 (vmoveq @0x1e490c), else 0.05f
//                         (pool 0x1e4ab0 = 0x3d4ccccd)
//   +0x1c       u       -- NOT written in the loop
//
// Post-loop @0x1e498c-0x1e49b0 -- the stores the old marker missed. Their sp
// offsets fall inside the vertex array, so they are per-vertex overrides:
//   sp+0x64 = v0+0x1c  v0.u =  0.5f    (0x3f000000)
//   sp+0x6c = v1+0x00  v1.x = -0.25f   (0xbe800000)
//   sp+0x74 = v1+0x08  v1.z =  1.0f
//   sp+0x88 = v1+0x1c  v1.u =  0.0f    (pool 0x1e4aac)
//   sp+0x90 = v2+0x00  v2.x =  0.25f   (0x3e800000)
//   sp+0x98 = v2+0x08  v2.z =  1.0f
//   sp+0xac = v2+0x1c  v2.u =  1.0f
//
// Transform chain (ASM @0x1e4958-0x1e4a18):
//   lengthFactor = m_Life * -2.0f + 3.0f                    (vmla @0x1e497c)
//   scaledVec    = scale (Entity+0x28) * lengthFactor       (bl 0x0011139c)
//   m            = Scale44(scaledVec)                       (bl 0x00102ec4)
//   m            = m * m_StartMatrix (+0x7c)                (bl 0x0010d580)
//   m            = m * m_WorldMatrix (+0x3c)                (bl 0x0010d580)
//   m.GlobalTranslate44(pos) (Entity+0x10)                  (bl 0x00106a68)
//
// Texture gating (@0x1e4a1c and @0x1e4a7c): the binary calls the
// `RayTexture.ptr != 0` helper (T.788 @0x001e4890) TWICE and gates ONLY the
// Set()/UnSet() vtable calls with it. The verts, the matrix, SetCurrentMatrix,
// the modelview upload and DrawTriStrip all run unconditionally -- there is no
// early return. Do not reintroduce one.
//
// Residual asm-verify delta after this fix is codegen-only: the port's
// _Matrix44<T>::operator*, MakeScale, GlobalTranslate44 and
// _Vector3<T>::operator*(T) are template-header inlines, where the binary has
// them out-of-line behind the five `bl`s listed above. That accounts for the
// bulk of the port-is-bigger instruction count and is not fixable from this
// file.
void FruitRay::DrawRay() {
    QUADCUSTOMVERTEX verts[3];
    for (int i = 0; i < 3; ++i) {
        verts[i].x = 0.0f;
        verts[i].y = 0.0f;
        verts[i].z = 0.0f;
        verts[i].nx = 0.0f;
        verts[i].ny = 0.0f;
        verts[i].nz = 1.0f;
        verts[i].v = (i == 0) ? 1.0f : 0.05f;

        // Binary uses vcvt.u32.f32, which saturates a negative float to 0.
        // m_Life goes slightly negative on the frame the ray is killed, so the
        // clamp is load-bearing; spell it out because a plain cast is UB (and
        // wraps) on the host toolchain.
        float alpha = m_Life * 255.0f;
        Colour c(255, 255, 255, (alpha > 0.0f) ? (uint8_t)(int)alpha : 0);
        verts[i].colour = c.PlatformColour();
    }

    // Per-vertex overrides -- the ray wedge. Apex at the origin sampling the
    // middle of the texture; base edge one unit out along +Z spanning u 0..1.
    verts[0].u =  0.5f;
    verts[1].x = -0.25f;
    verts[1].z =  1.0f;
    verts[1].u =  0.0f;
    verts[2].x =  0.25f;
    verts[2].z =  1.0f;
    verts[2].u =  1.0f;

    float lengthFactor = m_Life * -2.0f + 3.0f;
    _Vector3<float> scaledVec = scale * lengthFactor;

    Matrix44 m = Matrix44::MakeScale(scaledVec);
    m = m * m_StartMatrix;
    m = m * m_WorldMatrix;
    m.GlobalTranslate44(pos);

    if (RayTexture.IsValid()) RayTexture->Set();

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().SetCurrentMatrix(m);
    mm.UploadModelViewOnly();

    Mortar::Mesh::DrawTriStrip(verts, 3, false, NULL);

    if (RayTexture.IsValid()) RayTexture->UnSet(true);
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
