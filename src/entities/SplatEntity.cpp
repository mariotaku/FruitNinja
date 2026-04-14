//
// SplatEntity — juice-splat pool. See SplatEntity.h for binary refs.
//
// Analysed: 2026-04-14T01:00
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

// --- Constants ---------------------------------------------------------
// All resolved from binary DATs at 0x0017f564, 0x0017fd40-0x0017fd50,
// and the atlas UV table at 0x001bd014 (see docs/engine/splat-notes.md).

// Lifetime clamps from binary UpdateActiveSplats tail section.
static const float LIFE_INIT_BASE = 3.75f;
static const float LIFE_INIT_RAND = 2.5f;
static const float DECAY_INIT_BASE = 0.375f;
static const float DECAY_INIT_RAND = 0.25f;

// Velocity clamps from UpdateActiveSplats DAT block at 0x0017fd40-fd4c.
static const float VEL_CLAMP_XY_MIN = -50.0f;   // DAT_0017fd40
static const float VEL_CLAMP_XY_MAX =  50.0f;   // DAT_0017fd44
static const float VEL_CLAMP_Y_MIN  = -200.0f;  // DAT_0017fd48
static const float VEL_CLAMP_Y_MAX  =  200.0f;  // DAT_0017fd4c

// Gravity: binary applies `game_dt * DAT * 10` to m_Vel.y per frame.
// The DAT at runtime corresponds to an approx -1.0 scalar so the
// effective gravity is -600 u/s at the frame-rate base. Port uses
// a direct -200 u/s² for a comparable curve.
static const float SPLAT_GRAVITY_Y = -200.0f;

// Colour-phase timer (binary m_field_0x4 initial value). DAT_0017f564
// resolves at runtime to 1.5f for the non-fruit fallback path;
// MakeSplat assigns it.
static const float COLOUR_PHASE_INIT = 1.5f;
// Threshold below which the colour lerps toward the fruit tint.
static const float COLOUR_LERP_START = 0.5f;

// Splat quad half-size. Binary computes width/height from axis vectors
// (+0x44/+0x48 scalars) × 2.5 × sprite atlas row weight. Port uses a
// fixed scalar here for simplicity.
static const float SPLAT_QUAD_SIZE = 24.0f;

// Atlas UV table — resolved from DAT_0017f234 + entry * 0x10.
// Stored at binary 0x001bd014. Six entries, each 4 floats:
//   +0x00: u0 (U for one edge, multiplied by 0.5 in DrawSplat)
//   +0x04: u1 (U for other edge, multiplied by 0.5)
//   +0x08: v0 (V for one edge, stored as float raw)
//   +0x0c: v1 (V for other edge)
// With `bSpecial` the binary adds 0.5 to u0/u1 — atlas right half.
struct SplatAtlasEntry { float u0, u1, v0, v1; };
static const SplatAtlasEntry SPLAT_ATLAS[6] = {
    { 0.0f, 0.5f, 0.00f, 0.25f },  // type 0: 32x16 top-left cell
    { 0.5f, 1.0f, 0.00f, 0.25f },  // type 1: 32x16 top-right cell
    { 0.0f, 0.5f, 0.25f, 0.50f },  // type 2: 32x16 row2 left
    { 0.5f, 1.0f, 0.25f, 0.50f },  // type 3: 32x16 row2 right
    { 0.0f, 1.0f, 0.50f, 0.75f },  // type 4: 64x16 full-width row3
    { 0.0f, 1.0f, 0.75f, 1.00f },  // type 5: 64x16 full-width row4
};

// Splat texture "base" colour (pbVar10 in the binary colour lerp).
// The binary loads this from a runtime BSS pointer that I couldn't
// resolve statically; approximated here as a neutral pink that blends
// toward every fruit colour cleanly. Used as the "texture-natural"
// tint before the fruit colour lerp kicks in at COLOUR_LERP_START.
static const uint8_t BASE_R = 220;
static const uint8_t BASE_G = 160;
static const uint8_t BASE_B = 160;
static const uint8_t BASE_A = 255;

static Mortar::MemoryPool<SplatEntity> s_Pool;
static SmartPtr<Mortar::Texture>       s_SplatTex;

// Scratch vertex buffer for one batched draw. Sized for the pool cap.
static const int MAX_SPLATS_PER_FRAME = 128;
static QUADCUSTOMVERTEX s_SplatVerts[MAX_SPLATS_PER_FRAME * 6];

// --- Helpers -----------------------------------------------------------

static float RandRange(float r) {
    return ((float)rand() / (float)RAND_MAX) * r;
}

static uint16_t Atan2_16(float y, float x) {
    const float rad = atan2f(y, x);
    return (uint16_t)((int)(rad * (65536.0f / 6.2831853f)) & 0xFFFF);
}

// --- SplatEntity -------------------------------------------------------

SplatEntity::SplatEntity()
    : m_Scale(0.0f)
    , m_Life(0.0f)
    , m_DecayRate(0.0f)
    , m_SplatType(-1)
    , m_FruitR(255), m_FruitG(255), m_FruitB(255), m_FruitA(255)
    , m_ColourPhase(0.0f)
    , m_Angle(0)
    , m_bSpecial(0)
    , m_bFlipV(0)
{
    entityType = 2;
    active = false;
}

SplatEntity::~SplatEntity() {}

// Matches MakeSplat (0x17f2f0). Simplified: no same-screen multiplayer
// override, no alt-axis computation — the binary's axis vectors
// (+0x1c/+0x28) are derived from the velocity direction each frame
// in DrawSplat; the port re-derives them from m_Angle in DrawActive.
void SplatEntity::MakeSplat(const Vec3& p, const Vec3& v, int fruitType) {
    pos = p;
    pos.z = 0.0f;
    vel = v;

    m_Angle = Atan2_16(v.y, v.x);

    // Binary life / decay randomisation.
    m_Life      = LIFE_INIT_BASE + RandRange(LIFE_INIT_RAND);
    m_DecayRate = DECAY_INIT_BASE + RandRange(DECAY_INIT_RAND);

    // Capture the fruit colour for the colour-phase lerp. FruitInfo
    // stores BGRA in +0x240 — port takes R/G/B direct. The binary
    // also reads +0x19 (m_bSpecial) from the fruit entry; use it for
    // the atlas-right-half variant selection.
    const FruitInfo* info = FruitInfo_Get(fruitType);
    if (info) {
        m_FruitB = info->m_FruitColour[0];
        m_FruitG = info->m_FruitColour[1];
        m_FruitR = info->m_FruitColour[2];
        m_FruitA = 255;
        // Binary: this->field_0x19 = pFVar4->m_bSpecial.
        m_bSpecial = info->m_bSpecial;
    } else {
        m_FruitR = m_FruitG = m_FruitB = 255;
        m_FruitA = 255;
        m_bSpecial = 0;
    }

    // Binary picks splat type in UpdateActiveSplats after pos.z
    // crosses a threshold; port picks at spawn.
    m_SplatType = rand() % 6;

    // Random horizontal flip (50% chance). Matches binary
    // `this->field_0x34 = RandUint_Splat(2) != 0`.
    m_bFlipV = (rand() & 1) ? 1 : 0;

    m_ColourPhase = COLOUR_PHASE_INIT;
    m_Scale = SPLAT_QUAD_SIZE;

    active = true;
    flags &= ~0x10;
}

void SplatEntity::UpdateSplat(float dt) {
    if (!active) return;

    // Integrate position + gravity, then clamp velocity components to
    // the binary's per-axis cap (UpdateActiveSplats DAT_0017fd40-fd4c).
    pos += vel * dt;
    vel.y += SPLAT_GRAVITY_Y * dt;

    if (vel.x < VEL_CLAMP_XY_MIN) vel.x = VEL_CLAMP_XY_MIN;
    if (vel.x > VEL_CLAMP_XY_MAX) vel.x = VEL_CLAMP_XY_MAX;
    if (vel.y < VEL_CLAMP_Y_MIN)  vel.y = VEL_CLAMP_Y_MIN;
    if (vel.y > VEL_CLAMP_Y_MAX)  vel.y = VEL_CLAMP_Y_MAX;

    // Tick colour-phase timer down toward 0. Matches binary's
    //   field_0x4 -= game_dt
    // with the same 0.0 clamp floor.
    if (m_ColourPhase > 0.0f) {
        m_ColourPhase -= dt;
        if (m_ColourPhase < 0.0f) m_ColourPhase = 0.0f;
    }

    // Life decay. Binary: `m_Life -= game_dt * m_DecayRate`.
    m_Life -= dt * m_DecayRate * 6.0f;
    if (m_Life <= 0.0f) {
        m_Life = 0.0f;
        active = false;
    }
}

// --- Pool / global ops -------------------------------------------------

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
    // Slot state is whatever the last user left — caller will overwrite
    // via MakeSplat. Clear the active flag explicitly so an early
    // UpdateActive read sees it inactive if MakeSplat is deferred.
    s->active = false;
    return s;
}

void SplatEntity::UpdateActive(float dt) {
    const int N = s_Pool.Capacity();
    for (int i = 0; i < N; ++i) {
        SplatEntity* s = s_Pool.SlotAt(i);
        if (!s || !s->active) continue;

        s->UpdateSplat(dt);

        if (!s->active) {
            // Life ran out — return to pool.
            s_Pool.Push(s);
        }
    }
}

void SplatEntity::RemoveAll() {
    const int N = s_Pool.Capacity();
    for (int i = 0; i < N; ++i) {
        SplatEntity* s = s_Pool.SlotAt(i);
        if (!s || !s->active) continue;
        s->active = false;
        s_Pool.Push(s);
    }
}

// --- Batched draw ------------------------------------------------------

// Matches DrawActiveSplats (0x180344). Iterates pool, builds 6-vertex
// quads into s_SplatVerts, issues one DrawTriList.
void SplatEntity::DrawActive() {
    if (!s_SplatTex.IsValid()) return;

    const int N = s_Pool.Capacity();
    int count = 0;

    for (int i = 0; i < N && count < MAX_SPLATS_PER_FRAME; ++i) {
        SplatEntity* s = s_Pool.SlotAt(i);
        if (!s || !s->active) continue;

        // Orientation axes derived from the stored 16-bit angle.
        const float rad = (float)s->m_Angle * (6.2831853f / 65536.0f);
        const float c   = cosf(rad);
        const float sn  = sinf(rad);
        const float sz  = s->m_Scale;

        // Two half-extent vectors perpendicular to each other,
        // oriented along the splat's travel direction.
        const float ax = c  * sz;
        const float ay = sn * sz;
        const float bx = -sn * sz;
        const float by =  c  * sz;

        // Colour lerp — matches binary UpdateActiveSplats colour block.
        // fVar14 is 0 while m_ColourPhase >= 0.5 (showing base colour),
        // and ramps from 0→1 as m_ColourPhase drains from 0.5 → 0
        // (lerping base → fruit colour).
        float fVar14 = 0.0f;
        if (s->m_ColourPhase > 0.0f && s->m_ColourPhase < COLOUR_LERP_START) {
            fVar14 = 1.0f - 2.0f * s->m_ColourPhase;
        }
        // Lerp base colour (pbVar10 in binary) → fruit colour.
        const int rMix = (int)BASE_R + (int)((int)s->m_FruitR - (int)BASE_R) * fVar14;
        const int gMix = (int)BASE_G + (int)((int)s->m_FruitG - (int)BASE_G) * fVar14;
        const int bMix = (int)BASE_B + (int)((int)s->m_FruitB - (int)BASE_B) * fVar14;

        // Vertex alpha drives the life-based fade. Starts at full,
        // drops with m_Life.
        float lifeFrac = s->m_Life / LIFE_INIT_BASE;
        if (lifeFrac < 0.0f) lifeFrac = 0.0f;
        if (lifeFrac > 1.0f) lifeFrac = 1.0f;
        const uint8_t a = (uint8_t)(lifeFrac * 255.0f);

        const uint8_t rR = (uint8_t)(rMix < 0 ? 0 : (rMix > 255 ? 255 : rMix));
        const uint8_t gG = (uint8_t)(gMix < 0 ? 0 : (gMix > 255 ? 255 : gMix));
        const uint8_t bB = (uint8_t)(bMix < 0 ? 0 : (bMix > 255 ? 255 : bMix));
        const uint32_t col =
            ((uint32_t)a  << 24) |
            ((uint32_t)bB << 16) |
            ((uint32_t)gG <<  8) |
            ((uint32_t)rR);

        // Atlas UV sub-region from the binary table at 0x001bd014.
        // `bSpecial` selects the right half of the atlas (binary
        // DrawSplat `+= 0.5` on u0/u1). `bFlipV` swaps V edges.
        const SplatAtlasEntry& e = SPLAT_ATLAS[s->m_SplatType & 7];
        float u0 = e.u0, u1 = e.u1;
        if (s->m_bSpecial) { u0 += 0.5f; u1 += 0.5f; }
        float v0 = e.v0, v1 = e.v1;
        if (s->m_bFlipV) { float t = v0; v0 = v1; v1 = t; }

        QUADCUSTOMVERTEX* v = &s_SplatVerts[count * 6];
        // Two triangles forming a centred quad with axes (ax, ay)
        // and (bx, by). Layout: (v0, v1, v2), (v3=v2, v4=v1, v5).
        const float px = s->pos.x, py = s->pos.y, pz = s->pos.z;

        v[0].x = px - ax + bx;  v[0].y = py - ay + by;  v[0].z = pz;
        v[0].u = u0;            v[0].v = v0;
        v[1].x = px + ax + bx;  v[1].y = py + ay + by;  v[1].z = pz;
        v[1].u = u1;            v[1].v = v0;
        v[2].x = px - ax - bx;  v[2].y = py - ay - by;  v[2].z = pz;
        v[2].u = u0;            v[2].v = v1;
        v[3] = v[2];
        v[4] = v[1];
        v[5].x = px + ax - bx;  v[5].y = py + ay - by;  v[5].z = pz;
        v[5].u = u1;            v[5].v = v1;

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
