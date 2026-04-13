//
// SliceEffect — slash-line visual pool.
// See SliceEffect.h for binary refs.
//
// Analysed: 2026-04-14T01:00
//

#include "SliceEffect.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "render/gl_funcs.h"
#include "asset/Texture.h"
#include "asset/TextureManager.h"
#include "util/SmartPtr.h"
#include "util/MemoryPool.h"
#include <cmath>
#include <cstdio>

namespace FN {

// Binary uses a 6-frame keyframe animation over ~0.1s at 60Hz; port
// approximates with a simple linear fade over FADE_TIME seconds.
static const float FADE_TIME   = 0.35f;
static const float SLICE_LEN   = 80.0f;    // streak length in world units
static const float SLICE_HALF_H =  6.0f;   // streak half-height

static Mortar::MemoryPool<SliceEffect> s_Pool;
static SmartPtr<Mortar::Texture>       s_Tex;

// Scratch vertex buffer for the per-frame batch.
static const int MAX_SLICES = 32;
static QUADCUSTOMVERTEX s_Verts[MAX_SLICES * 6];

// --- Pool / content ----------------------------------------------------

void SliceEffect_CreatePool(int capacity) {
    s_Pool.Create(capacity);

    if (!s_Tex.IsValid()) {
        // Placeholder: reuse blade.tex so the line looks streak-like.
        // Once LoadFruitModels is ported we can swap to slice_fx.mmd
        // for the binary-accurate mesh-based look.
        s_Tex = Mortar::TextureManager::LoadLocalisedTexture("blade.tex");
        printf("[SliceEffect] CreatePool: capacity=%d tex_valid=%d\n",
               capacity, s_Tex.IsValid());
    }
}

void SliceEffect_DestroyPool() {
    s_Pool.Destroy();
    s_Tex.Clear();
}

// --- Spawn -------------------------------------------------------------

// Matches AddSlice (0x16b480), port simplified. Binary:
//   1. Pop a SliceEffect::Node from the MemoryPool
//   2. Fill timer=0, length, pos, angle, critical flag
//   3. Append to the global List<SliceEffect>
//   4. (if impulse > 2.5) random whoosh SFX — skipped in port
void SliceEffect_Add(const Vec3& pos, uint16_t angle, float impulse, bool critical) {
    SliceEffect* s = s_Pool.Pop();
    if (!s) return;  // pool exhausted

    s->timer     = 0.0f;
    s->_reserved = 0.0f;
    s->impulse   = impulse;
    s->pos       = pos;
    s->critical  = critical ? 1 : 0;
    s->angleRaw  = angle;

    // TODO: whoosh SFX via GameSound::SFXPlay when audio is wired.
}

// --- Tick + draw -------------------------------------------------------

// Matches DrawSlices (0x169ac8). Port flattened into one pass that
// advances the timer, builds a fading textured quad per slice, and
// returns expired slices to the pool.
void SliceEffect_Draw(float dt) {
    if (!s_Tex.IsValid()) return;

    const int N = s_Pool.Capacity();
    int count = 0;

    for (int i = 0; i < N && count < MAX_SLICES; ++i) {
        SliceEffect* s = s_Pool.SlotAt(i);
        if (!s) continue;

        // Inactive if timer is negative sentinel. The pool's free list
        // keeps track of which slots are live, so we identify an
        // in-use slot by the simple heuristic "timer >= 0 and less than
        // FADE_TIME". The slotwise check isn't strictly needed with the
        // stack-based MemoryPool (only popped slots are touched), but
        // keeping it makes bulk iteration safe even if a caller leaked
        // a Pop without SliceEffect_Add.
        if (s->timer < 0.0f || s->timer >= FADE_TIME) continue;

        s->timer += dt;
        if (s->timer >= FADE_TIME) {
            s->timer = -1.0f;
            s_Pool.Push(s);
            continue;
        }

        // Fade alpha linearly.
        const float t = s->timer / FADE_TIME;
        const float alpha = 1.0f - t;
        uint8_t a = (uint8_t)(alpha * 255.0f);

        // Tint — red for critical, white otherwise.
        uint32_t col;
        if (s->critical) {
            col = ((uint32_t)a << 24) | 0x000000FF;  // A / B / G / R
        } else {
            col = ((uint32_t)a << 24) | 0x00FFFFFF;
        }

        // Rotated quad aligned with slice direction.
        const float rad = (float)s->angleRaw * (6.2831853f / 65536.0f);
        const float c   = cosf(rad);
        const float sn  = sinf(rad);

        const float lx = c * SLICE_LEN * 0.5f;
        const float ly = sn * SLICE_LEN * 0.5f;
        const float nx = -sn * SLICE_HALF_H;
        const float ny =  c  * SLICE_HALF_H;

        const float px = s->pos.x, py = s->pos.y, pz = s->pos.z;

        QUADCUSTOMVERTEX* v = &s_Verts[count * 6];
        // Two triangles forming a centred strip along (lx, ly) with
        // perpendicular half-height (nx, ny).
        v[0].x = px - lx + nx; v[0].y = py - ly + ny; v[0].z = pz;
        v[0].u = 0.0f;         v[0].v = 0.0f;
        v[1].x = px + lx + nx; v[1].y = py + ly + ny; v[1].z = pz;
        v[1].u = 1.0f;         v[1].v = 0.0f;
        v[2].x = px - lx - nx; v[2].y = py - ly - ny; v[2].z = pz;
        v[2].u = 0.0f;         v[2].v = 1.0f;
        v[3] = v[2];
        v[4] = v[1];
        v[5].x = px + lx - nx; v[5].y = py + ly - ny; v[5].z = pz;
        v[5].u = 1.0f;         v[5].v = 1.0f;

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

    s_Tex->Set();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    if (Renderer* r = Renderer::GetInstance()) {
        r->DrawTriList(s_Verts, count * 6);
    }

    s_Tex->UnSet();
}

} // namespace FN
