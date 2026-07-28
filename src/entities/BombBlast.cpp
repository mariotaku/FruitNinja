//
// BombBlast — shockwave ring spawned by a slashed Bomb.
//
// Key RE facts:
//   - DrawBlast writes a 6-vertex parallelogram per blast: two
//     triangles forming a kite whose wide end points along m_Vel2
//     (perpendicular to the blast's random angle) and tapers near
//     the bomb centre along m_Vel1.
//   - DrawActiveBlasts iterates every live type-4 entity, builds ONE
//     shared vertex buffer, then issues a single DrawTriList for all
//     blasts on that frame.
//   - Texture is `bomb_explode.tex`, loaded by Bomb::Init into
//     g_bombData.m_blastTexture (+0x2C in the binary block @ 0x31785C).
//     There is NO separate "blast ring" texture.
//   - Init seeds the INHERITED Entity::scale to (5.0, 50.0, 1.0). scale is
//     not a render size here: scale.x/scale.y are the two independent
//     expansion accumulators that Update integrates and multiplies into
//     m_PosA/m_PosB, which DrawBlast then uses as the kite's axes.
//

#include "BombBlast.h"
#include "Bomb.h"
#include "ActorManager.h"
#include "Game.h"
#include "game/BombHit.h"
#include "game/GameWork.h"
#include "render/MatrixManager.h"
#include "render/gl_funcs.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "asset/Mesh.h"
#include "asset/Texture.h"
#include "util/SmartPtr.h"
#include "math/Matrix44.h"
#include "math/Random.h"
#include <cstdlib>
#include <cmath>
#include <cstdio>

// Binary constants (v1.6.1 BombBlast::Update @ 0x001d4f2c).
static const float SCALE_X_GROWTH = 100.0f;    // const @ 0x001d4fe0 (0x42c80000)
static const float SCALE_Y_GROWTH = 2500.0f;   // const @ 0x001d4fe4 (0x451c4000)
static const float BLAST_LIFE     = 3.0f;      // kill test is strictly greater-than

// v1.6.1 BombBlast::Init @ 0x001d58f8 seeds Entity::scale with these.
static const float SCALE_X_INIT   = 5.0f;
static const float SCALE_Y_INIT   = 50.0f;

// Shared texture — loaded by Bomb::Init into g_bombData.m_blastTexture, not re-loaded here.

// Static scratch buffer for the batched tri-list. Binary uses a global
// at 0x00232618 sized for ~512 blasts per frame (0x1B000 / 36 / 6).
// Port: keep it small. MAX_POOL ~= 64 blasts is comfortable since
// Bomb::Update spawns at 0.05s intervals over a 2s window → max 40.
static const int  MAX_BLASTS = 64;
static const int  VERTS_PER_BLAST = 6;
static QUADCUSTOMVERTEX s_BlastVerts[MAX_BLASTS * VERTS_PER_BLAST];

// Running per-frame blast counter. The binary's DrawBlast reads a global int*
// (BombBlast::m_curr_drawing_blast) and increments it once per blast in
// DrawActiveBlasts (v1.6.1 @ 0x001d67cc). DrawBlast keys its 6-vertex slot off this
// counter; the port mirrors that with a file-static index.
static int s_BlastCounter = 0;

// --------------------------------------------------------------------------

BombBlast::BombBlast()
    : m_PosA(0, 0, 0)
    , m_PosB(0, 0, 0)
    , m_Vel1(0, 0, 0)
    , m_Vel2(0, 0, 0)
    , m_Lifetime(0.0f)
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

// ASM-spec v1.6.1 BombBlast::Init @ 0x001d58f8 — vtable slot 2.
// Ghidra's void* p1 is a mis-decompile artifact -- the binary writes through
// r0 which is `this`; runtime caller passes (this, 0, 0, 0). Body operates
// exclusively on `this` and ignores all three explicit params. It does NOT
// touch pos.
void BombBlast::Init(void* /*p1*/, long /*p2*/, _Vector3<float>* /*p3*/) {

    // Activate: clear ENT_INACTIVE | ENT_KILLED. Mortar::ActorManager::Add already
    // cleared these on the recycle path; redundant on the factory path
    // but harmless and matches the binary's explicit Init sequence.
    flags &= ~ENT_SKIP_MASK;

    // Random 16-bit angle: the 19-bit draw is scaled into the 0..65535 index range.
    // ASM-spec v1.6.1 BombBlast::Init @0x001d58f8: Math::g_random.Rand32(0x7FFFF) x1
    m_Angle = (uint16_t)((float)Math::g_Random.Rand32(0x7FFFF) / 524287.0f * 360.0f * 182.0f);

    const float rad = (float)m_Angle * 6.2831853f / 65536.0f;
    const float c = cosf(rad);
    const float s = sinf(rad);

    // Vel1: randomly-angled direction scaled by 0.5 (the "narrow" axis).
    m_Vel1 = _Vector3<float>(c, s, 0.0f) * 0.5f;

    // Vel2: perpendicular (angle + 0x3FFC = +90° in 16-bit), full magnitude.
    //       Binary uses CosIdx/SinIdx on (angle + 0x3FFC) which is literally
    //       +90° — i.e. a rotated copy of Vel1 without the 0.5 scale.
    const float rad2 = rad + 1.5707963f;
    m_Vel2 = _Vector3<float>(cosf(rad2), sinf(rad2), 0.0f);

    // Initial m_PosA/m_PosB = copies of the velocities (frame-zero positions).
    m_PosA = m_Vel1;
    m_PosB = m_Vel2;

    // The two expansion accumulators (inherited Entity::scale +0x28 / +0x2C).
    scale = _Vector3<float>(SCALE_X_INIT, SCALE_Y_INIT, 1.0f);

    m_Lifetime = 0.0f;

    // m_Col stays null (inherited from Mortar::Entity ctor) — BombBlast doesn't collide.
}

// ASM-spec v1.6.1 BombBlast::Update @ 0x001d4f2c
//
// The `dt` parameter is ignored; the binary loads game_work.dt (+0x38, GOT
// slot 0x77f4). GameUpdate freezes ActorManager (dt=0) during a bomb hit, so
// this bypass is what keeps blasts expanding through the freeze.
void BombBlast::Update(float /*dt*/) {
    if (!IsActive()) return;

    const float dtG = game_work.dt;

    m_Lifetime += dtG;

    // Two independent accumulators with different seeds and different rates.
    scale.x += dtG * SCALE_X_GROWTH;
    m_PosA = m_Vel1 * scale.x;

    scale.y += dtG * SCALE_Y_GROWTH;
    m_PosB = m_Vel2 * scale.y;

    if (m_Lifetime > BLAST_LIFE) {
        flags |= ENT_KILLED;
    }
}

// v1.6.1 BombBlast::Draw @ 0x001d4dd0 — vtable Draw: no-op. Rendering via
// DrawActiveBlasts.
void BombBlast::Draw(Renderer& r) { (void)r; }

// v1.6.1 BombBlast::DrawUpdate @ 0x001d4dcc — vtable PostUpdate slot: no-op.
void BombBlast::PostUpdate(float /*dt*/) {}

// Matches DrawActiveBlasts (v1.6.1 @ 0x001d67cc).
//
// TODO: v1.6.1 0x001d67cc (DrawActiveBlasts) — the control flow below was RE'd
// against v1.5.1 and has NOT been re-verified against v1.6.1.
//
// Binary control flow:
//   if (g_bombData.m_blastTexture is valid) {
//       g_bombData.m_blastTexture->Set();
//       *g_BlastCounter = 0;                  // reset shared blast index
//       for each type-4 entity e (GetEntityFirst/Next):
//           e->vtable[0x34]()  -> DrawBlast   // writes 6 verts at counter slot
//           (*g_BlastCounter)++;              // bump after each blast
//       g_bombData.m_blastTexture->UnSet();
//       g_bombData.m_blastTexture->Set();                 // re-bind for the batched draw
//       worldStack.Reset(); UploadMatrices_Coin();
//       DrawTriList(g_BlastVerts, *g_BlastCounter * 6, false, NULL);
//       g_bombData.m_blastTexture->UnSet();
//   }
//
// Note the binary iterates EVERY type-4 entity unconditionally (no IsActive
// / radius gate inside the loop); the kill sweep already removed dead blasts
// from the type list before draw, and a zero-radius blast emits a degenerate
// (zero-area) quad. The port keeps a MAX_BLASTS clamp on the static buffer.
void BombBlast::DrawActiveBlasts() {
    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (!am) return;

    // Share the texture Bomb::Init already loaded into g_bombData.m_blastTexture (+0x2C).
    // Skip the pass if no bomb has spawned yet.
    if (!g_bombData.m_blastTexture.IsValid()) return;

    g_bombData.m_blastTexture->Set();

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

    g_bombData.m_blastTexture->UnSet();

    if (s_BlastCounter == 0) return;

    // Re-bind and issue the single batched tri-list (binary Set/UnSet pair).
    g_bombData.m_blastTexture->Set();

    // Identity world matrix — vertices are already in world space.
    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();

    Mortar::Mesh::DrawTriList(s_BlastVerts, s_BlastCounter * VERTS_PER_BLAST, false, NULL);
    g_bombData.m_blastTexture->UnSet();
}

// Port redirect: binary symbol is RemoveFlashEntities @ 0x001cb4b0 (free function).
// BombBlast::RemoveAll() is a port artifact; delegates to the faithful free function
// so call sites that haven't been updated yet still get the correct 0x11 flag OR.
void BombBlast::RemoveAll() {
    RemoveFlashEntities();
}

// v1.6.1 BombBlast::DrawBlast @ 0x001d51e8 — emit this blast's 6 vertices (two
// triangles) into the shared tri-list at the current frame-counter slot. Called
// per blast from DrawActiveBlasts (vtable+0x34) with the global counter bumped
// after each.
//
// TODO: v1.6.1 0x001d51e8 (BombBlast::DrawBlast) — the geometry, UVs and colour
// below were RE'd against v1.5.1 (the DAT_00171xxx addresses cited are v1.5.1
// data addresses) and have NOT been re-verified against v1.6.1.
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
// v1.6.1 BombBlast::DrawUpdate @ 0x001d4dcc: a bare `return;` (no-op). Realized
// here as the standalone symbol; the PostUpdate vtable slot aliases it.
void BombBlast::DrawUpdate(float) {}
