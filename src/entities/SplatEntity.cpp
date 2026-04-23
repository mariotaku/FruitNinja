//
// SplatEntity — juice-splat pool, 1:1 binary port.
// See SplatEntity.h for struct layout.
//
// Analysed: 2026-04-15T15:00
//

#include "SplatEntity.h"
#include "ActorManager.h"
#include "FruitInfo.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "render/gl_funcs.h"
#include "asset/Texture.h"
#include "asset/TextureManager.h"
#include "util/SmartPtr.h"
#include "util/MemoryPool.h"
#include <cstdlib>
#include <cmath>
#include <cstdio>

// ---------------------------------------------------------------------
// Binary constants (resolved from Ghidra, all addresses below refer to
// the ARM32 .text/.rodata in FruitNinja.exe)
// ---------------------------------------------------------------------

// MakeSplat @ 0x0017f2f0
static const float MS_Z_ZERO          = 0.0f;     // DAT_0017f564 = 0x00000000
static const float MS_Z_BIAS          = 150.0f;   // DAT_0017f568 = 0x43160000
static const float MS_ANGLE_SCALE     = 182.0f;   // DAT_0017f56c = 0x43360000
static const float MS_ANGLE_FLIP_DEG  = 180.0f;   // DAT_0017f570 = 0x43340000
static const float MS_COL_PHASE_DEF   = 0.75f;    //           0x3fc00000
static const float MS_VEL_FINAL_MULT  = 6.0f;
static const float MS_VEL_Y_STRETCH   = 1.5f;
static const float MS_Z_RAND_RANGE    = 10.0f;
static const float MS_AXIS_HALF_SCALE = 0.5f;

static const float MS_SCALE_BASE      = 10.0f;    // sc = Rand(10) + 10
static const float MS_SCALE_RAND      = 10.0f;

static const float MS_LIFE_BASE       = 3.75f;    // Rand(2.5) + 3.75
static const float MS_LIFE_RAND       = 2.5f;
static const float MS_DECAY_BASE      = 0.375f;   // Rand(0.25) + 0.375
static const float MS_DECAY_RAND      = 0.25f;

// Update @ 0x0017f774
static const float UP_LAND_Z          = -50.0f;   // DAT_0017faa8 (landing)
// Verified 2026-04-15 from instruction at 0x0017fa90:
//   vmov.f32 s13, 0xc1200000   ; -10.0f
//   vmla.f32 s15, s13, s14     ; vel.y += -10.0 * dt
// where s14 = *(GameTaskData + 0x38) is the per-frame dt scalar.
// The port uses the SDL frame dt directly here.
static const float UP_GRAVITY         = -10.0f;

static const float UP_VEL_CLAMP_LO    = -50.0f;   // DAT_0017fd40
static const float UP_VEL_CLAMP_HI    =  50.0f;   // DAT_0017fd44
static const float UP_SCALE_CLAMP_LO  = -50.0f;   // DAT_0017fd48
static const float UP_SCALE_CLAMP_HI  = 200.0f;   // DAT_0017fd4c

static const float UP_LIFE_SLIDE_THR  = 1.25f;    // slide-phase threshold

// Per-frame "spring rate" (binary GameTaskData+0x2c). Recomputed in
// UpdateActive each frame from the active entity counts; UpdateSplat
// reads it during the slide-decay phase. See UpdateActive() for the
// full derivation.
static float s_SpringRate = 1.25f;

// DrawActiveSplats @ 0x00180344 — UV atlas table @ 0x001bd014 (6 × 4 floats)
// Each entry is {u0, u1, v0, v1} — verified from raw little-endian dump.
// Parse-order note: the previous comment had this as {u0,v0,u1,v1} which
// gave zero-width quads for the small-cell entries. The true layout
// pairs the U bounds first, then the V bounds.
struct SplatAtlasEntry { float u0, u1, v0, v1; };
static const SplatAtlasEntry SPLAT_ATLAS[6] = {
    { 0.0f, 0.5f, 0.0f,  0.25f },  // type 0: upper-left small cell
    { 0.5f, 1.0f, 0.0f,  0.25f },  // type 1: upper-right small cell
    { 0.0f, 0.5f, 0.25f, 0.5f  },  // type 2: middle-left small cell
    { 0.5f, 1.0f, 0.25f, 0.5f  },  // type 3: middle-right small cell
    { 0.0f, 1.0f, 0.5f,  0.75f },  // type 4: full-width middle row
    { 0.0f, 1.0f, 0.75f, 1.0f  },  // type 5: full-width bottom row
};

// Base colour used as the "pre-fruit" tint during the colour lerp.
// Binary reads `pbVar10` from GOT+0x7A44 (resolved address 0x001F3B74),
// initialized in _GLOBAL__I_Fruit_cpp @ 0x0017a514 as a copy of the
// engine's Blue constant from _GLOBAL__I_Colour_cpp @ 0x0018406a:
//   MakeColour_Engine(ptr, 0, 0, 0xff)  → R=0, G=0, B=255, A=255
// Verified 2026-04-15. The earlier (255,255,255,255) was a placeholder.
static const uint8_t BASE_R = 0;
static const uint8_t BASE_G = 0;
static const uint8_t BASE_B = 255;
static const uint8_t BASE_A = 255;

// ---------------------------------------------------------------------
// Pool + shared content
// ---------------------------------------------------------------------

static Mortar::MemoryPool<SplatEntity> s_Pool;
static SmartPtr<Mortar::Texture>       s_SplatTex;

static const int MAX_SPLATS_PER_FRAME = 128;
static QUADCUSTOMVERTEX s_SplatVerts[MAX_SPLATS_PER_FRAME * 6];

// ---------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------

// Uniform [0, r]
static float RandRange(float r) {
    return ((float)rand() / (float)RAND_MAX) * r;
}

// Uniform integer [0, n)
static int RandInt(int n) {
    if (n <= 0) return 0;
    return rand() % n;
}

static int ClampInt(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static float Clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// ---------------------------------------------------------------------
// SplatEntity
// ---------------------------------------------------------------------

SplatEntity::SplatEntity()
    : m_ColourPhase(0.0f)
    , m_ColB(255), m_ColG(255), m_ColR(255), m_ColA(255)
    , m_AlphaBase(255.0f)
    , m_Angle(0.0f)
    , m_bParam3(0)
    , m_bSpecial(0)
    , m_AxisA(0, 0, 0)
    , m_AxisB(0, 0, 0)
    , m_bFlipV(0)
    , m_Scale(0, 0, 0)
    , m_Life(0.0f)
    , m_DecayRate(0.0f)
    , m_FruitType(0)
    , m_SplatType(-1)
    , m_bAlive(0)
{
    entityType = 2;
    // SplatEntity runs its own s_Pool lifecycle independent of
    // ActorManager; m_bAlive is the live/dead flag. No ENT_INACTIVE
    // bookkeeping needed here.
}

SplatEntity::~SplatEntity() {}

// Matches SplatEntity::MakeSplat (0x0017f2f0). Every field stored by the
// binary's 131-line init sequence is mirrored here, in order.
void SplatEntity::MakeSplat(const Vec3& p, const Vec3& v, bool param3, int fruitType) {
    m_bParam3 = param3 ? 1 : 0;

    // Colour selection. Binary reads FruitTypeColour(fruitType) when in
    // range, else falls back to a default colour + ColourPhase = 0.75.
    const FruitInfo* info = FruitInfo_Get(fruitType);
    if (info) {
        m_ColourPhase = 0.0f;
        m_ColB = info->m_FruitColour[0];
        m_ColG = info->m_FruitColour[1];
        m_ColR = info->m_FruitColour[2];
        m_ColA = 255;
    } else {
        m_ColourPhase = MS_COL_PHASE_DEF;
        m_ColB = BASE_B;
        m_ColG = BASE_G;
        m_ColR = BASE_R;
        m_ColA = BASE_A;
    }

    m_bFlipV    = (RandInt(2) != 0) ? 1 : 0;
    m_AlphaBase = (float)m_ColA;

    // Position — Z forced to 0.
    pos = p;
    pos.z = MS_Z_ZERO;

    // Velocity transform (binary @ 0x0017f3??):
    //   m_Vel   = vel
    //   speed   = |m_Vel|
    //   m_Vel.y *= 1.5
    //   m_Vel.z = speed * -0.5 - 150.0 - rand(10)
    //   m_Vel  *= 6.0
    vel = v;
    const float speed = vel.Magnitude();
    vel.y *= MS_VEL_Y_STRETCH;
    vel.z = speed * -0.5f - MS_Z_BIAS - RandRange(MS_Z_RAND_RANGE);
    vel = vel * MS_VEL_FINAL_MULT;

    // Angle: uniform [0, 360).
    m_Angle = (float)RandInt(360);

    // bSpecial from FruitInfo.
    m_bSpecial = info ? info->m_bSpecial : 0;

    m_FruitType = fruitType;

    // Scale triple: sc random [10, 20], stored as (sc, -sc, sc).
    const float sc = MS_SCALE_BASE + RandRange(MS_SCALE_RAND);
    m_Scale = Vec3(sc, -sc, sc);

    // Life + decay randomisation (binary tail section @ 0x0017f???).
    m_Life      = MS_LIFE_BASE  + RandRange(MS_LIFE_RAND);
    m_DecayRate = MS_DECAY_BASE + RandRange(MS_DECAY_RAND);

    // Axis vectors. Binary (verified at SplatEntity::Update landing branch
    // ~0x0017f9xx): two perpendicular 2D basis vectors with DIFFERENT
    // scalars — axisA = (cos, sin) * 0.5 and axisB = perp * 0.25. The
    // half/quarter pair gives the splat a 2:1 wide-to-tall aspect ratio
    // before the per-type scale multiply.
    const float angleRad  = m_Angle * (3.1415926f / 180.0f);
    const float axPerpRad = angleRad + 1.5707963f;  // +90°
    m_AxisA = Vec3(cosf(angleRad),  sinf(angleRad),  0.0f) * 0.5f;
    m_AxisB = Vec3(cosf(axPerpRad), sinf(axPerpRad), 0.0f) * 0.25f;

    // Start airborne — Update will pick m_SplatType on landing.
    m_SplatType = -1;
    m_bAlive    = 1;
    flags &= ~ENT_KILLED;
}

// Matches SplatEntity::Update (0x0017f774). Two phases: airborne
// (m_SplatType < 0) does physics + z check; landed (m_SplatType >= 0)
// does slide + decay.
void SplatEntity::UpdateSplat(float dt) {
    if (!m_bAlive) return;

    // --- Physics integration (airborne only) ---
    // Once landed (m_SplatType >= 0) the binary stops integrating pos
    // and only touches m_Scale.y in the slide-decay phase.
    if (m_SplatType < 0) {
        pos = pos + vel * dt;
        vel.y += UP_GRAVITY * dt;

        // Velocity floor clamp — binary uses max(vel, DAT_0017fd40 = -50)
        // to prevent downward velocity runaway.
        if (vel.x < UP_VEL_CLAMP_LO) vel.x = UP_VEL_CLAMP_LO;
        if (vel.y < UP_VEL_CLAMP_LO) vel.y = UP_VEL_CLAMP_LO;
    }

    if (m_SplatType < 0) {
        // --- Airborne phase ---
        // Check landing threshold. Binary: pos.z < -50 → assign type.
        if (pos.z < UP_LAND_Z) {
            // Landing — pick splat variant.
            //   Normal path:
            //     1/4 chance  → type = (Rand(2)==0) ? 2 : 3 (large round)
            //     else (3/4)  → type = (Rand(6)!=0) ? 0 : 1 (small round)
            //   param3 override: 1/2 chance swap to type 4 or 5
            int type;
            if (RandInt(4) == 0) {
                type = (RandInt(2) == 0) ? 2 : 3;
            } else {
                type = (RandInt(6) != 0) ? 0 : 1;
            }
            if (m_bParam3 && RandInt(2) == 0) {
                type = (RandInt(2) == 0) ? 4 : 5;
            }
            m_SplatType = (int8_t)type;

            // Per-type size multiplier — binary at landing branch:
            //   m_Scale *= (perTypeScale[type] * 2.5)
            // Verified table @ 0x001BD074 (GOT_BASE + DAT_0017fad4 + 0x60):
            //   types 0..3 → 1.6  (small splats, final *= 4.0)
            //   types 4..5 → 2.9  (golden splats, final *= 7.25)
            static const float PER_TYPE_SCALE[6] = {
                1.6f, 1.6f, 1.6f, 1.6f, 2.9f, 2.9f
            };
            const int idx = (type >= 0 && type < 6) ? type : 0;
            m_Scale = m_Scale * (PER_TYPE_SCALE[idx] * 2.5f);

            // Stick to the "background plane" — splat freezes in place.
            // Binary's landed Update doesn't integrate pos, so we zero
            // vel completely. Slide-decay phase (life <= 1.25) only
            // touches m_Scale.y, not pos.
            pos.z = UP_LAND_Z;
            vel = Vec3(0.0f, 0.0f, 0.0f);
        }
        return;
    }

    // --- Landed phase ---
    // Slide / scale decay — binary runs this only while m_Life <= 1.25
    // (the tail slide phase). Above that threshold the splat sits still
    // on the plane. Spring rate computed in UpdateActive each frame
    // from active entity counts (binary GameTaskData+0x2c, see
    // UpdateActive for the formula).
    if (m_Life <= UP_LIFE_SLIDE_THR) {
        const float dy = dt * s_SpringRate;
        vel.y   = Clampf(vel.y - dy,   UP_VEL_CLAMP_LO,   UP_VEL_CLAMP_HI);
        m_Scale.y = Clampf(m_Scale.y - dy, UP_SCALE_CLAMP_LO, UP_SCALE_CLAMP_HI);
    }

    // Life decay.
    m_Life -= dt * m_DecayRate;
    if (m_Life <= 0.0f) {
        m_Life = 0.0f;
        m_bAlive = 0;
        return;
    }

    // Alpha = min(base, base * life). Binary clamp to uint8.
    const float rawAlpha = m_AlphaBase * m_Life;
    const float aF = rawAlpha < m_AlphaBase ? rawAlpha : m_AlphaBase;
    m_ColA = (uint8_t)Clampf(aF, 0.0f, 255.0f);
}

// ---------------------------------------------------------------------
// Pool / static ops
// ---------------------------------------------------------------------

void SplatEntity::CreatePool(int capacity) {
    s_Pool.Create(capacity);
    printf("[SplatEntity] CreatePool: capacity=%d\n", capacity);
}

void SplatEntity::DestroyPool() {
    s_Pool.Destroy();
    s_SplatTex.SetNull();
}

void SplatEntity::LoadContent() {
    if (!s_SplatTex.IsValid()) {
        s_SplatTex = Mortar::TextureManager::LoadLocalisedTexture("white_splash.tex");
        printf("[SplatEntity] LoadContent: white_splash.tex valid=%d\n",
               s_SplatTex.IsValid());
    }
}

void SplatEntity::ReleaseContent() {
    s_SplatTex.SetNull();
}

SplatEntity* SplatEntity::GetFree() {
    SplatEntity* s = s_Pool.Pop();
    if (!s) return nullptr;
    s->m_bAlive = 0;
    return s;
}

void SplatEntity::UpdateActive(float dt) {
    // Per-frame spring rate compute — matches binary's
    // SplatEntity::UpdateActiveSplats @ 0x0017fd68 (instructions
    // 0x0017fe46..0x0017feda):
    //
    //   N_total  = ActorManager::GetNumEntities()
    //   N_active = (count of live splats in pool)
    //   raw      = (N_total + N_active) / 15.0 - 0.15
    //   if raw <= 0:   spring = 1.25
    //   elif raw >= 3: spring = 4.25
    //   else:          spring = raw + 1.25  (linear ramp 1.25..4.25)
    //
    // The binary additionally multiplies by 1.5 if !IsFastHardware()
    // OR (multiplayer && game->field_0x170 == 0). Port skips the slow-
    // hardware branch (always treats as fast hw) and the multiplayer
    // gate (no MP support yet).
    const int N = s_Pool.Capacity();
    int activeCount = 0;
    for (int i = 0; i < N; ++i) {
        SplatEntity* s = s_Pool.SlotAt(i);
        if (s && s->m_bAlive) ++activeCount;
    }
    int totalEntities = 0;
    if (ActorManager* am = ActorManager::GetInstance()) {
        totalEntities = am->GetNumEntities();
    }
    {
        const float raw = (float)(totalEntities + activeCount) / 15.0f - 0.15f;
        if (raw <= 0.0f)      s_SpringRate = 1.25f;
        else if (raw >= 3.0f) s_SpringRate = 4.25f;
        else                  s_SpringRate = raw + 1.25f;
    }

    for (int i = 0; i < N; ++i) {
        SplatEntity* s = s_Pool.SlotAt(i);
        if (!s || !s->m_bAlive) continue;

        s->UpdateSplat(dt);

        if (!s->m_bAlive) {
            s_Pool.Push(s);
        }
    }
}

void SplatEntity::RemoveAll() {
    const int N = s_Pool.Capacity();
    for (int i = 0; i < N; ++i) {
        SplatEntity* s = s_Pool.SlotAt(i);
        if (!s || !s->m_bAlive) continue;
        s->m_bAlive = 0;
        s_Pool.Push(s);
    }
}

// ---------------------------------------------------------------------
// Batched draw — matches DrawActiveSplats (0x00180344)
// ---------------------------------------------------------------------
//
// For each alive splat with m_SplatType >= 0, build a 6-vertex quad:
//   - corners from (pos ± axisA * scaleX ± axisB * scaleY)
//   - UVs from SPLAT_ATLAS[type], with bSpecial shifting U by +0.5 for
//     types 0..3 and bFlipV swapping V edges
//   - colour lerped from BASE_RGBA toward fruit colour by
//     fade = clamp(2.0 * m_ColourPhase, 0, 1)
//   - alpha = m_ColA (pre-clamped in UpdateSplat)
//
// All quads go into s_SplatVerts and render as a single batched
// DrawTriList with slice_fruit.tex bound.
void SplatEntity::DrawActive() {
    if (!s_SplatTex.IsValid()) return;

    const int N = s_Pool.Capacity();
    int count = 0;

    for (int i = 0; i < N && count < MAX_SPLATS_PER_FRAME; ++i) {
        SplatEntity* s = s_Pool.SlotAt(i);
        if (!s || !s->m_bAlive)     continue;
        if (s->m_SplatType < 0)     continue;  // still airborne

        // Axis-scaled corner vectors. Scale triple stored as (sc,-sc,sc)
        // where sc is the random radius. m_Scale.y is negative on purpose
        // (it's the value that decays during the slide phase) — use the
        // magnitude as the quad's uniform half-extent so the quad isn't
        // back-facing.
        // Quad corner construction — matches binary DrawSplat (0x0017f008).
        // The four corners are built from the sum/diff of the two axis
        // vectors, scaled by m_Scale.x for X and m_Scale.y for Y. Note
        // that m_Scale.y is the negative `-sc` from the spawn triple,
        // which the binary keeps negative on purpose (it's the value the
        // slide-decay phase mutates).
        //
        //   v0 (TL): pos + (+ax + bx) * scX, (+ay + by) * scY
        //   v1 (TR): pos + (-ax + bx) * scX, (-ay + by) * scY
        //   v2 (BL): pos + (+ax - bx) * scX, (+ay - by) * scY
        //   v5 (BR): pos + (-ax - bx) * scX, (-ay - by) * scY
        const float scX = s->m_Scale.x;
        const float scY = s->m_Scale.y;
        const float ax = s->m_AxisA.x, ay = s->m_AxisA.y;
        const float bx = s->m_AxisB.x, by = s->m_AxisB.y;

        // Colour lerp from base RGBA toward the captured fruit colour.
        // Binary formula: fade = clamp(2.0 * m_ColourPhase, 0, 1).
        // Since m_ColourPhase ticks toward zero, fade shrinks — meaning
        // the splat lerps FROM fruit colour TOWARD base as ColourPhase
        // drains. (FruitTypeColour path starts at phase=0, so fruit
        // colour is persistent; default path starts at 0.75 so the base
        // colour fades to fruit over time.)
        // Colour lerp from base RGBA toward the captured fruit colour.
        // Binary formula: fade = clamp(2.0 * m_ColourPhase, 0, 1).
        // Since m_ColourPhase ticks toward zero, fade shrinks — meaning
        // the splat lerps FROM fruit colour TOWARD base as ColourPhase
        // drains. (FruitTypeColour path starts at phase=0, so fruit
        // colour is persistent; default path starts at 0.75 so the base
        // colour fades to fruit over time.)
        float fade = s->m_ColourPhase * 2.0f;
        if (fade < 0.0f) fade = 0.0f;
        if (fade > 1.0f) fade = 1.0f;
        const int rMix = (int)s->m_ColR + (int)((int)BASE_R - (int)s->m_ColR) * fade;
        const int gMix = (int)s->m_ColG + (int)((int)BASE_G - (int)s->m_ColG) * fade;
        const int bMix = (int)s->m_ColB + (int)((int)BASE_B - (int)s->m_ColB) * fade;

        const uint8_t rR = (uint8_t)ClampInt(rMix, 0, 255);
        const uint8_t gG = (uint8_t)ClampInt(gMix, 0, 255);
        const uint8_t bB = (uint8_t)ClampInt(bMix, 0, 255);
        const uint8_t a  = s->m_ColA;
        const uint32_t col =
            ((uint32_t)a  << 24) |
            ((uint32_t)bB << 16) |
            ((uint32_t)gG <<  8) |
            ((uint32_t)rR);

        // Atlas UV — verified via SplatEntity::DrawSplat @ 0x0017f008.
        // The binary halves the table-stored U coords (`* 0.5`) before
        // applying them, then optionally shifts both by +0.5 when
        // m_bSpecial (binary field_0x19) is set — selecting the right
        // half of the texture. Without the halving the quad samples
        // twice as wide as the actual splat sprite cell and ends up
        // mostly transparent / invisible.
        //
        //   u0 = table.u_start * 0.5
        //   u1 = table.u_end   * 0.5
        //   if (m_bSpecial)  { u0 += 0.5; u1 += 0.5; }
        //   if (m_bFlipV)    swap(v0, v1);
        const int type = ClampInt(s->m_SplatType, 0, 5);
        const SplatAtlasEntry& e = SPLAT_ATLAS[type];
        float u0 = e.u0 * 0.5f;
        float u1 = e.u1 * 0.5f;
        if (s->m_bSpecial) { u0 += 0.5f; u1 += 0.5f; }
        float v0 = e.v0, v1 = e.v1;
        if (s->m_bFlipV) { float t = v0; v0 = v1; v1 = t; }

        QUADCUSTOMVERTEX* v = &s_SplatVerts[count * 6];
        const float px = s->pos.x, py = s->pos.y, pz = s->pos.z;

        // Vertex layout matches binary DrawSplat (vertices 0..5 written in
        // order, then v3=v2 and v4=v1 via memcpy):
        //   v[0] = TL
        //   v[1] = TR
        //   v[2] = BL
        //   v[3] = v[2]
        //   v[4] = v[1]
        //   v[5] = BR
        v[0].x = px + ( ax + bx) * scX;
        v[0].y = py + ( ay + by) * scY;
        v[0].z = pz;
        v[0].u = u0; v[0].v = v0;

        v[1].x = px + (-ax + bx) * scX;
        v[1].y = py + (-ay + by) * scY;
        v[1].z = pz;
        v[1].u = u1; v[1].v = v0;

        v[2].x = px + ( ax - bx) * scX;
        v[2].y = py + ( ay - by) * scY;
        v[2].z = pz;
        v[2].u = u0; v[2].v = v1;

        v[3] = v[2];
        v[4] = v[1];

        v[5].x = px + (-ax - bx) * scX;
        v[5].y = py + (-ay - by) * scY;
        v[5].z = pz;
        v[5].u = u1; v[5].v = v1;

        for (int k = 0; k < 6; ++k) {
            v[k].nx = 0.0f;
            v[k].ny = 0.0f;
            v[k].nz = 1.0f;
            v[k].colour = col;
        }

        ++count;
    }

    if (count == 0) return;

    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();

    s_SplatTex->Set();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Read-only depth test — matches binary's SetDepthBufferWrite(0)
    // call BEFORE this draw (after ActorManager::Draw). Splats render
    // on the background plane (z=-50) but should be culled by any 3D
    // fruit/bomb pixel currently in front. The port's Fruit::Draw
    // explicitly disables depth test on exit, so we re-enable read-only
    // here so the cleared/fruit depth values still drive z-rejection.
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_FALSE);

    if (Renderer* r = Renderer::GetInstance()) {
        r->DrawTriList(s_SplatVerts, count * 6);
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    s_SplatTex->UnSet();
}
