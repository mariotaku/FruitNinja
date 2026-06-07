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
#include "render/MatrixManager.h"
#include "render/gl_funcs.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "asset/Mesh.h"
#include "asset/Texture.h"
#include "util/SmartPtr.h"
#include "math/Matrix44.h"
#include <cstdlib>
#include <cmath>
#include <cstdio>

// Binary constants (resolved from memory at agent-reported DAT addrs).
static const float RADIUS_GROWTH = 100.0f;   // DAT_0017120c
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

// Running per-frame blast counter. Binary @ 0x171354 reads a global int*
// (resolved via GOT off DAT_001714d8) and increments it once per blast in
// DrawActiveBlasts (0x171aa0). DrawBlast keys its 6-vertex slot off this
// counter; the port mirrors that with a file-static index.
static int s_BlastCounter = 0;

// --------------------------------------------------------------------------

BombBlast::BombBlast()
    : m_PosA(0, 0, 0)
    , m_PosB(0, 0, 0)
    , m_Vel1(0, 0, 0)
    , m_Vel2(0, 0, 0)
    , m_Lifetime(0.0f)
    , m_BlastRadius(0.0f)
{
    entityType = 4;
    m_Angle = 0;  // inherited from Mortar::Entity base at +0x36
    // Binary ctor clears 0x11 (inactive + killed); we start without both.
    flags &= ~ENT_SKIP_MASK;
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

    if (m_Lifetime >= BLAST_LIFE) {
        flags |= ENT_KILLED;
    }
}

// Binary @ 0x00171034 — vtable Draw: no-op. Rendering via DrawActiveBlasts.
void BombBlast::Draw(Renderer& r) { (void)r; }

// Binary @ 0x00171030 — vtable PostUpdate (DrawUpdate): no-op.
void BombBlast::PostUpdate(float /*dt*/) {}

// Matches DrawActiveBlasts (0x171aa0).
//
// Binary control flow:
//   if (g_BombTexture is valid) {
//       g_BombTexture->Set();
//       *g_BlastCounter = 0;                  // reset shared blast index
//       for each type-4 entity e (GetEntityFirst/Next):
//           e->vtable[0x34]()  -> DrawBlast   // writes 6 verts at counter slot
//           (*g_BlastCounter)++;              // bump after each blast
//       g_BombTexture->UnSet();
//       g_BombTexture->Set();                 // re-bind for the batched draw
//       worldStack.Reset(); UploadMatrices_Coin();
//       DrawTriList(g_BlastVerts, *g_BlastCounter * 6, false, NULL);
//       g_BombTexture->UnSet();
//   }
//
// Note the binary iterates EVERY type-4 entity unconditionally (no IsActive
// / radius gate inside the loop); the kill sweep already removed dead blasts
// from the type list before draw, and a zero-radius blast emits a degenerate
// (zero-area) quad. The port keeps a MAX_BLASTS clamp on the static buffer.
void BombBlast::DrawActiveBlasts() {
    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (!am) return;

    // Share the texture Bomb::Init already loaded (g_bombData->tex_02
    // in the binary). Skip the pass if no bomb has spawned yet.
    if (!g_BombTexture.IsValid()) return;

    g_BombTexture->Set();

    // Reset the shared blast counter, then let each blast emit its own
    // 6-vertex slot via DrawBlast (vtable+0x34 in the binary).
    s_BlastCounter = 0;
    const std::list<Mortar::Entity*>& blastList = am->GetTypeList(4);
    for (std::list<Mortar::Entity*>::const_iterator it = blastList.begin();
         it != blastList.end() && s_BlastCounter < MAX_BLASTS; ++it) {
        Mortar::Entity* e = *it;
        if (!e) continue;
        static_cast<BombBlast*>(e)->DrawBlast();
        s_BlastCounter++;
    }

    g_BombTexture->UnSet();

    if (s_BlastCounter == 0) return;

    // Re-bind and issue the single batched tri-list (binary Set/UnSet pair).
    g_BombTexture->Set();

    // Identity world matrix — vertices are already in world space.
    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();

    Mortar::Mesh::DrawTriList(s_BlastVerts, s_BlastCounter * VERTS_PER_BLAST, false, NULL);
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

// Binary @ 0x171354 — emit this blast's 6 vertices (two triangles) into the
// shared tri-list at the current frame-counter slot. Called per blast from
// DrawActiveBlasts (vtable+0x34) with the global counter bumped after each.
//
// Geometry (A = m_PosA = narrow axis, B = m_PosB = long axis):
//   v0 = pos + A + B          // far corner, +A side
//   v1 = pos + (B - A)        // far corner, -A side  (== pos - A + B)
//   v2 = pos + A * 0.25       // near-centre, +A side
//   v5 = pos + A * -0.25      // near-centre, -A side
//   v3 = copy of v2           // second triangle reuses v2
//   v4 = copy of v1           // second triangle reuses v1
// Triangle list: (v0, v1, v2), (v3=v2, v4=v1, v5).
//
// UVs (from the binary's per-vertex stores, NOT a flat single-texel):
//   v0 (1,0)  v1 (0,0)  v2 (1,1)  v3 (1,1)  v4 (0,0)  v5 (0,1)
// i.e. a proper textured quad of bomb_explode.tex stretched across the kite.
//
// Per-vertex: normal = (0,0,1); z = 0 (DAT_001714d0); colour = a fixed
// global blast Colour run through Colour::PlatformColour (no age fade).
void BombBlast::DrawBlast() {
    if (s_BlastCounter >= MAX_BLASTS) return;

    const float px = pos.x;
    const float py = pos.y;
    const float ax = m_PosA.x;
    const float ay = m_PosA.y;
    const float bx = m_PosB.x;
    const float by = m_PosB.y;

    QUADCUSTOMVERTEX* v = &s_BlastVerts[s_BlastCounter * VERTS_PER_BLAST];

    // Positions (binary writes only x,y; z stays 0 via the colour/normal loop).
    v[0].x = px + ax + bx;        v[0].y = py + ay + by;
    v[1].x = px + (bx - ax);      v[1].y = py + (by - ay);
    v[2].x = px + ax * 0.25f;     v[2].y = py + ay * 0.25f;
    v[5].x = px + ax * -0.25f;    v[5].y = py + ay * -0.25f;
    v[3].x = v[2].x;              v[3].y = v[2].y;   // v3 = v2
    v[4].x = v[1].x;              v[4].y = v[1].y;   // v4 = v1

    // Per-vertex UVs (binary stores: see table above).
    v[0].u = 1.0f;  v[0].v = 0.0f;
    v[1].u = 0.0f;  v[1].v = 0.0f;
    v[2].u = 1.0f;  v[2].v = 1.0f;
    v[3].u = 1.0f;  v[3].v = 1.0f;   // v3 = v2
    v[4].u = 0.0f;  v[4].v = 0.0f;   // v4 = v1
    v[5].u = 0.0f;  v[5].v = 1.0f;

    // DIFFERS: original = fixed global Colour (DAT @ 0x1ef4d4 -> Colour @ 0x16d880)
    //   run through Colour::PlatformColour with no lifetime input; exact RGBA
    //   is unresolved (vtable-bearing Colour singleton, needs the Colour
    //   subsystem). Port uses solid opaque white so the blast renders at full
    //   tint like the binary's constant colour. The previous age-based alpha
    //   fade was a port-side band-aid not present in the binary and is removed.
    const uint32_t col = 0xFFFFFFFFu; // ABGR opaque white

    for (int i = 0; i < VERTS_PER_BLAST; ++i) {
        v[i].z  = 0.0f;       // DAT_001714d0
        v[i].nx = 0.0f;       // DAT_001714d0
        v[i].ny = 0.0f;       // DAT_001714d0
        v[i].nz = 1.0f;       // 0x3f800000
        v[i].colour = col;    // Colour::PlatformColour(global blast colour)
    }
}
// Binary @ 0x171030 — DrawUpdate(float): a bare `return;` (no-op). Realized
// here as the standalone symbol; the PostUpdate vtable slot aliases it.
void BombBlast::DrawUpdate(float) {}
