//
// BombBlast — shockwave ring. Ported from binary 0x171170..0x171aa0.
// See docs/entities/bomb-blast.md and the RE findings reconciled here.
//
// Analysed: 2026-04-13T23:45
//
// Key RE facts (resolved via re-analyst 2026-04-13):
//   - DrawBlast writes a 6-vertex parallelogram per blast: two
//     triangles forming a kite whose wide end points along m_Vel2
//     (perpendicular to the blast's random angle) and tapers near
//     the bomb centre along m_Vel1.
//   - DrawActiveBlasts iterates every live type-4 entity, builds ONE
//     shared vertex buffer, then issues a single DrawTriList for all
//     blasts on that frame.
//   - Texture is `bomb_explode.tex`, loaded by Bomb::Init into the
//     extern'd g_BombTexture (same as g_bombData->tex_02 at +0x04 in
//     the binary). There is NO separate "blast ring" texture.
//   - Init sets m_Scale = (5.0, 50.0, 1.0) as a Vec3 — unused in
//     rendering but kept for struct fidelity.
//

#include "BombBlast.h"
#include "ActorManager.h"
#include "Game.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/gl_funcs.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "asset/Texture.h"
#include "util/SmartPtr.h"
#include "math/Matrix44.h"
#include <cstdlib>
#include <cmath>
#include <cstdio>

// Binary constants (resolved from memory at agent-reported DAT addrs).
static const float RADIUS_GROWTH = 100.0f;   // DAT_0017120c
static const float SCALE_GROWTH  = 2500.0f;  // DAT_00171210
static const float BLAST_LIFE    = 3.0f;
static const float BLAST_Z       = 0.0f;     // field_0x6c initial

// Shared texture — loaded by Bomb::Init, not re-loaded here.
namespace { extern "C" {} }
extern Mortar::SmartPtr<Mortar::Texture> g_BombTexture;

// Static scratch buffer for the batched tri-list. Binary uses a global
// at 0x00232618 sized for ~512 blasts per frame (0x1B000 / 36 / 6).
// Port: keep it small. MAX_POOL ~= 64 blasts is comfortable since
// Bomb::Update spawns at 0.05s intervals over a 2s window → max 40.
static const int  MAX_BLASTS = 64;
static const int  VERTS_PER_BLAST = 6;
static QUADCUSTOMVERTEX s_BlastVerts[MAX_BLASTS * VERTS_PER_BLAST];

// --------------------------------------------------------------------------

BombBlast::BombBlast()
    : m_BlastRadius(0.0f)
    , m_Scale(0.0f)
    , m_PosA(0, 0, 0)
    , m_PosB(0, 0, 0)
    , m_Vel1(0, 0, 0)
    , m_Vel2(0, 0, 0)
    , m_Lifetime(0.0f)
{
    entityType = 4;
    m_Angle = 0;  // inherited from Mortar::Entity base at +0x36
    // Binary ctor clears 0x11 (collision + kill); we start without both.
    flags &= ~0x11u;
}

BombBlast::~BombBlast() {}

// Binary loads the blast texture in Bomb::Init, not BombBlast::LoadContent.
// Keeping these as no-ops so the header declarations still link.
void BombBlast::LoadContent()    {}
void BombBlast::ReleaseContent() {}

// Binary @ 0x001718ac — vtable slot 2.
// ASM-verified: 2026-05-04T08:23Z binary @ 0x001718ac (asm-inspector)
// Confirmed: Ghidra's void* p1 was a mis-decompile artifact -- the binary
// writes through r0 which is `this`; runtime caller passes (this, 0, 0, 0).
// Body operates exclusively on `this` and ignores all three explicit params.
void BombBlast::Init(void* /*p1*/, long /*p2*/, Vec3* /*p3*/) {

    // Activate: clear ENT_INACTIVE | ENT_KILLED. Mortar::ActorManager::Add already
    // cleared these on the recycle path; redundant on the factory path
    // but harmless and matches the binary's explicit Init sequence.
    flags &= ~ENT_SKIP_MASK;

    pos.z = BLAST_Z;

    // Random 16-bit angle. Binary uses Rand32(0x7FFFF) / 524288.0f * 360 * 182,
    // which compresses the 19-bit random into the 0..65535 angle range.
    m_Angle = (uint16_t)(rand() & 0xFFFF);

    const float rad = (float)m_Angle * 6.2831853f / 65536.0f;
    const float c = cosf(rad);
    const float s = sinf(rad);

    // Vel1: randomly-angled direction scaled by 0.5 (the "narrow" axis).
    m_Vel1 = Vec3(c, s, 0.0f) * 0.5f;

    // Vel2: perpendicular (angle + 0x3FFC = +90° in 16-bit), full magnitude.
    //       Binary uses CosIdx/SinIdx on (angle + 0x3FFC) which is literally
    //       +90° — i.e. a rotated copy of Vel1 without the 0.5 scale.
    const float rad2 = rad + 1.5707963f;
    m_Vel2 = Vec3(cosf(rad2), sinf(rad2), 0.0f);

    // Initial m_PosA/m_PosB = copies of the velocities (frame-zero positions).
    m_PosA = m_Vel1;
    m_PosB = m_Vel2;

    m_BlastRadius = 0.0f;
    // Binary: m_Scale = (5.0, 50.0, 1.0) Vec3. Unused in render, kept scalar
    // in the port for struct simplicity — track the dominant .y component.
    m_Scale = 50.0f;
    m_Lifetime = 0.0f;

    // m_Col stays null (inherited from Mortar::Entity ctor) — BombBlast doesn't collide.
}

// Matches BombBlast::Update (0x171170).
void BombBlast::Update(float dt) {
    if (!IsActive()) return;

    m_BlastRadius += dt * RADIUS_GROWTH;
    m_Lifetime    += dt;

    // Binary re-multiplies m_PosA/m_PosB by a growing factor each frame.
    // Use the lifetime-scaled blast radius so the quad expands outward.
    m_PosA = m_Vel1 * m_BlastRadius;
    m_PosB = m_Vel2 * m_BlastRadius;

    m_Scale += SCALE_GROWTH * dt;

    if (m_Lifetime >= BLAST_LIFE) {
        flags |= 0x10;   // kill
    }
}

// Binary @ 0x00171034 — vtable Draw: no-op. Rendering via DrawActiveBlasts.
void BombBlast::Draw(Renderer& r) { (void)r; }

// Binary @ 0x00171030 — vtable PostUpdate (DrawUpdate): no-op.
void BombBlast::PostUpdate(float /*dt*/) {}

// Matches DrawActiveBlasts (0x171aa0) + DrawBlast (0x171354).
//
// Per-blast geometry (binary DrawBlast):
//   v0 = pos + A + B          // far corner, +A side
//   v1 = pos - A + B          // far corner, -A side
//   v2 = pos + A * 0.25       // near-centre, +A side
//   v5 = pos - A * 0.25       // near-centre, -A side
//   v3 = v2    (degenerate-free triangle pairing)
//   v4 = v1
//
// Where A = m_PosA (m_Vel1 * m_BlastRadius, the "narrow" axis) and
// B = m_PosB (m_Vel2 * m_BlastRadius, the "long" axis perpendicular to A).
// The quad is a kite pointing outward in direction B with width 2·A at
// the far end and 0.5·A at the near end. Multiple blasts at random
// angles produce a starburst of expanding strips.
//
// Colour is a solid tint (white) with age-based alpha fade. UVs are all
// (0,0) — binary samples a single texel of `bomb_explode.tex` for a
// flat tinted fill.
void BombBlast::DrawActiveBlasts() {
    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (!am) return;

    // Share the texture Bomb::Init already loaded (g_bombData->tex_02
    // in the binary). Skip the pass if no bomb has spawned yet.
    if (!g_BombTexture.IsValid()) return;

    // Build the per-frame vertex buffer in one pass. BombBlast is type 4.
    int blastCount = 0;
    const std::list<Mortar::Entity*>& blastList = am->GetTypeList(4);
    for (auto it = blastList.begin(); it != blastList.end() && blastCount < MAX_BLASTS; ++it) {
        Mortar::Entity* e = *it;
        if (!e || !e->IsActive()) continue;

        BombBlast* b = static_cast<BombBlast*>(e);
        if (b->m_BlastRadius <= 0.0f) continue;

        // Alpha fades linearly over the 3s lifetime.
        float t = b->m_Lifetime / BLAST_LIFE;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        const uint8_t alpha = (uint8_t)((1.0f - t) * 255.0f);
        const uint32_t col  = (uint32_t)alpha << 24 | 0x00FFFFFF; // AABBGGRR → ABGR

        const float px = b->pos.x;
        const float py = b->pos.y;
        const float pz = b->pos.z;
        const float ax = b->m_PosA.x;
        const float ay = b->m_PosA.y;
        const float bx = b->m_PosB.x;
        const float by = b->m_PosB.y;

        QUADCUSTOMVERTEX* v = &s_BlastVerts[blastCount * VERTS_PER_BLAST];

        // Parallelogram corner positions.
        //   v0 = pos + A + B   (outer-far on +A side)
        //   v1 = pos - A + B   (outer-far on -A side)
        //   v2 = pos + A * 0.25 (near-centre on +A side)
        //   v5 = pos - A * 0.25 (near-centre on -A side)
        // Triangle list: (v0,v1,v2), (v3=v2, v4=v1, v5).
        //
        // UV mapping: binary-accurate single-texel sample. DrawBlast
        // (0x171354) writes u=1.0, v=0.0 to all 6 verts, making the
        // quad a flat colour equal to the (1,0) texel of the source
        // texture. bomb_explode.tex is a 32x128 gold→white gradient,
        // so (1.0, 0.0) samples the right edge of row 0 which is
        // the gold start colour. The port reproduces that exactly
        // so the blast tint matches the binary's starburst look.
        v[0].x = px + ax + bx;  v[0].y = py + ay + by;  v[0].z = pz;
        v[1].x = px - ax + bx;  v[1].y = py - ay + by;  v[1].z = pz;
        v[2].x = px + ax * 0.25f;  v[2].y = py + ay * 0.25f;  v[2].z = pz;
        v[3].x = v[2].x;  v[3].y = v[2].y;  v[3].z = v[2].z;
        v[4].x = v[1].x;  v[4].y = v[1].y;  v[4].z = v[1].z;
        v[5].x = px - ax * 0.25f;  v[5].y = py - ay * 0.25f;  v[5].z = pz;

        for (int i = 0; i < VERTS_PER_BLAST; ++i) {
            v[i].nx = 0.0f;
            v[i].ny = 0.0f;
            v[i].nz = 1.0f;
            v[i].colour = col;
            v[i].u = 1.0f;   // binary: all verts at (1, 0)
            v[i].v = 0.0f;
        }

        blastCount++;
    }

    if (blastCount == 0) return;

    // Identity world matrix — vertices are already in world space.
    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();

    g_BombTexture->Set();

    if (Renderer* r = Renderer::GetInstance()) {
        r->DrawTriList(s_BlastVerts, blastCount * VERTS_PER_BLAST);
    }

    g_BombTexture->UnSet();
}

// Matches RemoveFlashEntities (0x169ca0) — called by UpdateBombHit when
// Game.bombHitTimer drops below 1.55s.
void BombBlast::RemoveAll() {
    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (!am) return;
    // Binary DeactivateAllEntities(type) @ 0x0016fb44 — set ENT_KILLED on
    // every entity of the given type. Next Update sweep returns them to
    // the free pool.
    am->DeactivateAllEntities(4);
}

// ---- AUTO-STUB MERGE: STUB -- gen_stubs.py ----
// STUB: BombBlast::DrawBlast -- auto stub
void BombBlast::DrawBlast() {}
// STUB: BombBlast::DrawUpdate -- auto stub
void BombBlast::DrawUpdate(float) {}
// ---- end AUTO-STUB MERGE ----
