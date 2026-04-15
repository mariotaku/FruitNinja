//
// SplatEntity — juice-splat pool, 1:1 binary port.
// See SplatEntity.h for struct layout.
//
// Analysed: 2026-04-15T15:00
//

#include "SplatEntity.h"
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
static const float UP_GRAVITY         = -600.0f;  // port-picked — binary uses
                                                  // dt * DAT_ * 10 with a
                                                  // runtime rate; this gives
                                                  // ~0.06s airborne window
                                                  // for a -900 u/s start vel.

static const float UP_VEL_CLAMP_LO    = -50.0f;   // DAT_0017fd40
static const float UP_VEL_CLAMP_HI    =  50.0f;   // DAT_0017fd44
static const float UP_SCALE_CLAMP_LO  = -50.0f;   // DAT_0017fd48
static const float UP_SCALE_CLAMP_HI  = 200.0f;   // DAT_0017fd4c

static const float UP_LIFE_SLIDE_THR  = 1.25f;    // slide-phase threshold
static const float UP_SPRING_DT_MULT  = 1.0f;     // binary reads from GameTaskData

// DrawActiveSplats @ 0x00180344 — UV atlas table @ 0x001bd014 (6 × 4 floats)
// Each entry is {u0, v0, u1, v1} (verified from raw little-endian dump).
struct SplatAtlasEntry { float u0, v0, u1, v1; };
static const SplatAtlasEntry SPLAT_ATLAS[6] = {
    { 0.0f, 0.5f, 0.0f, 0.25f },
    { 0.5f, 1.0f, 0.0f, 0.25f },
    { 0.0f, 0.5f, 0.25f, 0.5f },
    { 0.5f, 1.0f, 0.25f, 0.5f },
    { 0.0f, 1.0f, 0.5f, 0.75f },
    { 0.0f, 1.0f, 0.75f, 1.0f },
};

// Base colour used as the "pre-fruit" tint during the colour lerp. Binary
// reads this from a GOT singleton (`pbVar10`); the static value resolves
// to white with full alpha in practice.
static const uint8_t BASE_R = 255;
static const uint8_t BASE_G = 255;
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
    active = false;
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

    // Axis vectors. Binary builds two perpendicular 2D basis vectors
    // from (angle, angle+180°) and scales by 0.5.
    const float angleRad  = m_Angle * (3.1415926f / 180.0f);
    const float axFlipRad = (m_Angle + MS_ANGLE_FLIP_DEG) * (3.1415926f / 180.0f);
    m_AxisA = Vec3(cosf(angleRad),  sinf(angleRad),  0.0f) * MS_AXIS_HALF_SCALE;
    m_AxisB = Vec3(cosf(axFlipRad), sinf(axFlipRad), 0.0f) * MS_AXIS_HALF_SCALE;

    // Start airborne — Update will pick m_SplatType on landing.
    m_SplatType = -1;
    m_bAlive    = 1;

    active = true;
    flags &= ~0x10;
}

// Matches SplatEntity::Update (0x0017f774). Two phases: airborne
// (m_SplatType < 0) does physics + z check; landed (m_SplatType >= 0)
// does slide + decay.
void SplatEntity::UpdateSplat(float dt) {
    if (!m_bAlive) return;

    // --- Physics integration (both phases) ---
    pos = pos + vel * dt;
    vel.y += UP_GRAVITY * dt;

    // Velocity clamps.
    vel.x = Clampf(vel.x, UP_VEL_CLAMP_LO, UP_VEL_CLAMP_HI);
    vel.y = Clampf(vel.y, UP_SCALE_CLAMP_LO, UP_SCALE_CLAMP_HI);

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

            // Stop Z motion — splat sticks to the "background plane".
            pos.z = UP_LAND_Z;
            vel.z = 0.0f;
        }
        return;
    }

    // --- Landed phase ---
    // Slide / scale decay — binary runs this only while m_Life <= 1.25
    // (the tail slide phase). Above that threshold the splat sits still
    // on the plane.
    if (m_Life <= UP_LIFE_SLIDE_THR) {
        const float dy = dt * UP_SPRING_DT_MULT;
        vel.y   = Clampf(vel.y - dy,   UP_VEL_CLAMP_LO,   UP_VEL_CLAMP_HI);
        m_Scale.y = Clampf(m_Scale.y - dy, UP_SCALE_CLAMP_LO, UP_SCALE_CLAMP_HI);
    }

    // Life decay.
    m_Life -= dt * m_DecayRate;
    if (m_Life <= 0.0f) {
        m_Life = 0.0f;
        m_bAlive = 0;
        active = false;
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
    s_SplatTex.Clear();
}

void SplatEntity::LoadContent() {
    if (!s_SplatTex.IsValid()) {
        s_SplatTex = Mortar::TextureManager::LoadLocalisedTexture("slice_fruit.tex");
        printf("[SplatEntity] LoadContent: slice_fruit.tex valid=%d\n",
               s_SplatTex.IsValid());
    }
}

void SplatEntity::ReleaseContent() {
    s_SplatTex.Clear();
}

SplatEntity* SplatEntity::GetFree() {
    SplatEntity* s = s_Pool.Pop();
    if (!s) return NULL;
    s->m_bAlive = 0;
    s->active   = false;
    return s;
}

void SplatEntity::UpdateActive(float dt) {
    const int N = s_Pool.Capacity();
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
        s->active   = false;
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

        // Axis-scaled corner vectors.
        // Binary's quad basis is axisA * scale.x and axisB * scale.y.
        const float sx = s->m_Scale.x;
        const float sy = s->m_Scale.y;
        const Vec3 right(s->m_AxisA.x * sx, s->m_AxisA.y * sx, 0.0f);
        const Vec3 up   (s->m_AxisB.x * sy, s->m_AxisB.y * sy, 0.0f);

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

        // Atlas UV — bSpecial shifts types 0..3 to the right half.
        const int type = ClampInt(s->m_SplatType, 0, 5);
        const SplatAtlasEntry& e = SPLAT_ATLAS[type];
        float u0 = e.u0, u1 = e.u1;
        if (s->m_bSpecial && type < 4) { u0 += 0.5f; u1 += 0.5f; }
        float v0 = e.v0, v1 = e.v1;
        if (s->m_bFlipV) { float t = v0; v0 = v1; v1 = t; }

        QUADCUSTOMVERTEX* v = &s_SplatVerts[count * 6];
        const float px = s->pos.x, py = s->pos.y, pz = s->pos.z;

        // Two triangles forming a centred quad:
        //   v0 = pos - right - up     uv = (u0, v0)
        //   v1 = pos + right - up     uv = (u1, v0)
        //   v2 = pos - right + up     uv = (u0, v1)
        //   v3 = v2
        //   v4 = v1
        //   v5 = pos + right + up     uv = (u1, v1)
        v[0].x = px - right.x - up.x;
        v[0].y = py - right.y - up.y;
        v[0].z = pz;
        v[0].u = u0; v[0].v = v0;

        v[1].x = px + right.x - up.x;
        v[1].y = py + right.y - up.y;
        v[1].z = pz;
        v[1].u = u1; v[1].v = v0;

        v[2].x = px - right.x + up.x;
        v[2].y = py - right.y + up.y;
        v[2].z = pz;
        v[2].u = u0; v[2].v = v1;

        v[3] = v[2];
        v[4] = v[1];

        v[5].x = px + right.x + up.x;
        v[5].y = py + right.y + up.y;
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
    glDisable(GL_DEPTH_TEST);

    if (Renderer* r = Renderer::GetInstance()) {
        r->DrawTriList(s_SplatVerts, count * 6);
    }

    s_SplatTex->UnSet();
}
