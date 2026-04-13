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
// Lifetime clamps from binary UpdateActiveSplats (0x17fd68) DATs.
static const float LIFE_INIT_BASE = 3.75f;   // DAT base
static const float LIFE_INIT_RAND = 2.5f;    // Rand(2.5)
static const float DECAY_INIT_BASE = 0.375f;
static const float DECAY_INIT_RAND = 0.25f;
// Binary gravity for splats is game dt * DAT*10, applied to Vel.y only.
static const float SPLAT_GRAVITY_Y = -200.0f;
// Default splat quad size (world units). Tweaked so slice_fruit.tex
// covers a visible area on the 480x320 ortho.
static const float SPLAT_QUAD_SIZE = 24.0f;

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
    , m_ColourR(255), m_ColourG(255), m_ColourB(255), m_ColourA(255)
    , m_Angle(0)
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

    // Tint from fruit colour. FruitInfo stores BGRA in +0x240; we tint
    // by the first 3 bytes and leave alpha at 255 (vertex alpha will
    // fade with life). Binary mixes this with the splat sprite colour
    // each frame in UpdateSplat — port holds constant for v1.
    const FruitInfo* info = FruitInfo_Get(fruitType);
    if (info) {
        m_ColourB = info->m_FruitColour[0];
        m_ColourG = info->m_FruitColour[1];
        m_ColourR = info->m_FruitColour[2];
        m_ColourA = 255;
    } else {
        m_ColourR = m_ColourG = m_ColourB = m_ColourA = 255;
    }

    // Binary m_SplatType stays -1 until UpdateActiveSplats bumps it
    // into the [0, 5] range; port picks immediately from a simple roll.
    m_SplatType = rand() % 6;

    m_Scale = SPLAT_QUAD_SIZE;

    active = true;
    flags &= ~0x10;
}

void SplatEntity::UpdateSplat(float dt) {
    if (!active) return;

    // Integrate position + gravity. Binary's integration is stripped
    // for brevity — the port just advances pos and applies gravity.y.
    pos += vel * dt;
    vel.y += SPLAT_GRAVITY_Y * dt;

    // Life decay. Binary uses `life -= game_dt * decay_rate`. Port
    // uses real dt with the same decay rate.
    m_Life -= dt * m_DecayRate * 6.0f;  // ×6 so lifetimes land around 0.5-1.5s
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

        // Colour packs as (R, G, B, A) in memory for GL_UNSIGNED_BYTE
        // normalised. Vertex alpha uses remaining life for fade.
        float lifeFrac = s->m_Life / LIFE_INIT_BASE;
        if (lifeFrac < 0.0f) lifeFrac = 0.0f;
        if (lifeFrac > 1.0f) lifeFrac = 1.0f;
        const uint8_t a = (uint8_t)(lifeFrac * 255.0f);
        const uint32_t col =
            ((uint32_t)a << 24) |
            ((uint32_t)s->m_ColourB << 16) |
            ((uint32_t)s->m_ColourG <<  8) |
            ((uint32_t)s->m_ColourR);

        QUADCUSTOMVERTEX* v = &s_SplatVerts[count * 6];
        // Two triangles forming a centred quad with axes (ax, ay)
        // and (bx, by). Layout: (v0, v1, v2), (v3=v2, v4=v1, v5).
        //   v0 = pos - ax - ay - bx - by   (bottom-left in local)
        //   v1 = pos + ax + ay - bx - by   (bottom-right)
        //   v2 = pos - ax - ay + bx + by   (top-left)
        //   v5 = pos + ax + ay + bx + by   (top-right)
        const float px = s->pos.x, py = s->pos.y, pz = s->pos.z;

        v[0].x = px - ax + bx;  v[0].y = py - ay + by;  v[0].z = pz;
        v[0].u = 0.0f;          v[0].v = 0.0f;
        v[1].x = px + ax + bx;  v[1].y = py + ay + by;  v[1].z = pz;
        v[1].u = 1.0f;          v[1].v = 0.0f;
        v[2].x = px - ax - bx;  v[2].y = py - ay - by;  v[2].z = pz;
        v[2].u = 0.0f;          v[2].v = 1.0f;
        v[3] = v[2];
        v[4] = v[1];
        v[5].x = px + ax - bx;  v[5].y = py + ay - by;  v[5].z = pz;
        v[5].u = 1.0f;          v[5].v = 1.0f;

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
