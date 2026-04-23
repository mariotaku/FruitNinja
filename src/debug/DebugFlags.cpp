//
// DebugFlags — draws fruit/bomb collision spheres as translucent
// outlined discs. Toggle with F1 in the SDL event loop (Game::run).
//

#include "DebugFlags.h"
#include "entities/ActorManager.h"
#include "entities/Entity.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "render/gl_funcs.h"
#include "math/Vec3.h"
#include <cmath>
#include <cstdio>

namespace FN {

bool  g_DebugHitboxes  = false;
bool  g_DebugWireframe = false; // Port specific: desktop GL only (F2)
float g_DebugTimeScale = 1.0f; // Port specific: debug-only, no binary equivalent

// Lazy 1×1 white texture for the vertex-colour shader path. The
// Renderer's program_vc samples a texture and multiplies by the vertex
// color; without a bound texture we'd see undefined samples. A solid
// white sample lets the vertex colour drive the visible tint.
static GLuint s_WhiteTex = 0;

static void EnsureWhiteTex() {
    if (s_WhiteTex) return;
    glGenTextures(1, &s_WhiteTex);
    glBindTexture(GL_TEXTURE_2D, s_WhiteTex);
    static const uint8_t white[4] = { 255, 255, 255, 255 };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

// --- Geometry helpers --------------------------------------------------

static const int   RING_SEGMENTS = 32;
static const float RING_THICKNESS = 1.5f;  // half-width of the ring band

// Build a hollow ring centred at (cx, cy) with the given inner/outer
// radii into `out`. Writes RING_SEGMENTS * 6 verts (one quad per seg).
static void BuildRing(QUADCUSTOMVERTEX* out,
                      float cx, float cy, float cz,
                      float innerR, float outerR,
                      uint32_t col) {
    const float twoPi = 6.2831853f;
    for (int i = 0; i < RING_SEGMENTS; ++i) {
        const float a0 = ((float)i       / RING_SEGMENTS) * twoPi;
        const float a1 = ((float)(i + 1) / RING_SEGMENTS) * twoPi;
        const float c0 = cosf(a0), s0 = sinf(a0);
        const float c1 = cosf(a1), s1 = sinf(a1);

        const float ix0 = cx + c0 * innerR, iy0 = cy + s0 * innerR;
        const float ox0 = cx + c0 * outerR, oy0 = cy + s0 * outerR;
        const float ix1 = cx + c1 * innerR, iy1 = cy + s1 * innerR;
        const float ox1 = cx + c1 * outerR, oy1 = cy + s1 * outerR;

        QUADCUSTOMVERTEX* v = &out[i * 6];

        // Triangle 1: inner0, outer0, inner1
        v[0].x = ix0; v[0].y = iy0; v[0].z = cz;
        v[1].x = ox0; v[1].y = oy0; v[1].z = cz;
        v[2].x = ix1; v[2].y = iy1; v[2].z = cz;

        // Triangle 2: inner1, outer0, outer1
        v[3].x = ix1; v[3].y = iy1; v[3].z = cz;
        v[4].x = ox0; v[4].y = oy0; v[4].z = cz;
        v[5].x = ox1; v[5].y = oy1; v[5].z = cz;

        for (int k = 0; k < 6; ++k) {
            v[k].nx = 0.0f; v[k].ny = 0.0f; v[k].nz = 1.0f;
            v[k].u  = 0.5f; v[k].v  = 0.5f;   // sample centre of white tex
            v[k].colour = col;
        }
    }
}

// Build a centre crosshair (4 short line segments rendered as thin
// quads) into `out`. 4 quads × 6 verts = 24 verts.
static void BuildCrosshair(QUADCUSTOMVERTEX* out,
                           float cx, float cy, float cz,
                           float radius, uint32_t col) {
    const float armLen   = radius * 0.4f;
    const float halfThk  = 0.6f;
    struct Arm { float dx, dy; };
    const Arm arms[4] = {
        { +1.0f,  0.0f },
        { -1.0f,  0.0f },
        {  0.0f, +1.0f },
        {  0.0f, -1.0f },
    };
    for (int a = 0; a < 4; ++a) {
        const float ex = cx + arms[a].dx * armLen;
        const float ey = cy + arms[a].dy * armLen;
        // Perpendicular to the arm direction.
        const float px = -arms[a].dy * halfThk;
        const float py =  arms[a].dx * halfThk;

        QUADCUSTOMVERTEX* v = &out[a * 6];
        v[0].x = cx + px; v[0].y = cy + py; v[0].z = cz;
        v[1].x = cx - px; v[1].y = cy - py; v[1].z = cz;
        v[2].x = ex + px; v[2].y = ey + py; v[2].z = cz;
        v[3].x = ex + px; v[3].y = ey + py; v[3].z = cz;
        v[4].x = cx - px; v[4].y = cy - py; v[4].z = cz;
        v[5].x = ex - px; v[5].y = ey - py; v[5].z = cz;

        for (int k = 0; k < 6; ++k) {
            v[k].nx = 0.0f; v[k].ny = 0.0f; v[k].nz = 1.0f;
            v[k].u  = 0.5f; v[k].v  = 0.5f;
            v[k].colour = col;
        }
    }
}

// Per-entity colours (BGRA packed).
static uint32_t ColourFor(int entityType) {
    switch (entityType) {
        case 0:  return 0x8000FF00;  // Fruit  -> green @ 50% alpha
        case 1:  return 0x800000FF;  // Bomb   -> red   @ 50% alpha
        case 2:  return 0x80FFFF00;  // Splat  -> cyan  @ 50% alpha
        default: return 0x80FFFFFF;  // other  -> white @ 50% alpha
    }
}

// ---------------------------------------------------------------------

void DebugHitbox_Draw() {
    if (!g_DebugHitboxes) return;

    ActorManager* am = ActorManager::GetInstance();
    if (!am) return;

    EnsureWhiteTex();

    // Reset world matrix — entity m_Col centres are already in world
    // space, so we draw at identity transform.
    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_WhiteTex);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    // Scratch vertex buffer: ring (32×6=192) + crosshair (4×6=24) = 216 verts/entity.
    static QUADCUSTOMVERTEX s_Verts[216];

    Renderer* r = Renderer::GetInstance();
    if (!r) return;

    int drawn = 0;
    // Only fruits (0) and bombs (1) — same set the slash collision
    // pass tests against. Skip splats and others.
    for (int t = 0; t <= 1; t++) {
    const std::list<Entity*>& list = am->GetTypeList(t);
    for (auto it = list.begin(); it != list.end(); ++it) {
        Entity* e = *it;
        if (!e || !e->IsActive()) continue;
        if (e->m_Col.radius <= 0.0f) continue;

        const uint32_t col = ColourFor(e->entityType);
        const float cx = e->m_Col.center.x;
        const float cy = e->m_Col.center.y;
        const float cz = -1.0f;          // slightly in front of the plane
        const float outerR = e->m_Col.radius;
        const float innerR = outerR - RING_THICKNESS;

        BuildRing(&s_Verts[0], cx, cy, cz,
                  innerR > 0 ? innerR : 0, outerR, col);
        BuildCrosshair(&s_Verts[RING_SEGMENTS * 6], cx, cy, cz, outerR, col);

        r->DrawTriList(s_Verts, RING_SEGMENTS * 6 + 24);
        ++drawn;
    }
    }  // end type loop

    (void)drawn;
}

} // namespace FN
