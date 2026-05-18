//
// DebugFlags — draws fruit/bomb collision spheres as translucent
// outlined discs. Toggle with F1 in the SDL event loop (Game::run).
//

#ifndef __bada__

#include "DebugFlags.h"
#include "entities/ActorManager.h"
#include "entities/Entity.h"
#include "hud/MenuButton.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "render/gl_funcs.h"
#include "math/Vec3.h"
#include <cmath>
#include <cstdio>
#include <list>

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

    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (!am) return;

    EnsureWhiteTex();

    // Reset world matrix — entity m_Col centres are already in world
    // space, so we draw at identity transform.
    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_WhiteTex);

    // Scratch vertex buffer: ring (32×6=192) + crosshair (4×6=24) = 216 verts/entity.
    static QUADCUSTOMVERTEX s_Verts[216];

    Renderer* r = Renderer::GetInstance();
    if (!r) return;

    int drawn = 0;
    // Only fruits (0) and bombs (1) — same set the slash collision
    // pass tests against. Skip splats and others.
    for (int t = 0; t <= 1; t++) {
    const std::list<Mortar::Entity*>& list = am->GetTypeList(t);
    for (auto it = list.begin(); it != list.end(); ++it) {
        Mortar::Entity* e = *it;
        if (!e || !e->IsActive()) continue;
        if (!e->m_Col) continue;
        ColSphere* cs = static_cast<ColSphere*>(e->m_Col);
        if (cs->radius <= 0.0f) continue;

        const uint32_t col = ColourFor(e->entityType);
        const float cx = cs->center.x;
        const float cy = cs->center.y;
        const float cz = -1.0f;          // slightly in front of the plane
        const float outerR = cs->radius;
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

// Build an AABB box outline as 4 thin-quad sides, 6 verts each = 24 verts.
// (left, right, bottom, top) are the hitbox edges in centered world space.
static void BuildAABBOutline(QUADCUSTOMVERTEX* out,
                             float left, float right,
                             float bottom, float top,
                             float z, float thickness,
                             uint32_t col) {
    // 4 sides: bottom, top, left, right
    struct Side { float x0, y0, x1, y1; };
    const Side sides[4] = {
        { left,  bottom, right, bottom },  // bottom edge (horizontal)
        { left,  top,    right, top    },  // top edge    (horizontal)
        { left,  bottom, left,  top    },  // left edge   (vertical)
        { right, bottom, right, top    },  // right edge  (vertical)
    };
    const float h = thickness * 0.5f;
    for (int i = 0; i < 4; ++i) {
        const float dx = sides[i].x1 - sides[i].x0;
        const float dy = sides[i].y1 - sides[i].y0;
        // Perpendicular outward half-width vector.
        float len = sqrtf(dx * dx + dy * dy);
        if (len < 0.001f) len = 0.001f;
        const float px = (-dy / len) * h;
        const float py = (dx  / len) * h;
        QUADCUSTOMVERTEX* v = &out[i * 6];
        v[0].x = sides[i].x0 + px; v[0].y = sides[i].y0 + py; v[0].z = z;
        v[1].x = sides[i].x0 - px; v[1].y = sides[i].y0 - py; v[1].z = z;
        v[2].x = sides[i].x1 + px; v[2].y = sides[i].y1 + py; v[2].z = z;
        v[3].x = sides[i].x1 + px; v[3].y = sides[i].y1 + py; v[3].z = z;
        v[4].x = sides[i].x0 - px; v[4].y = sides[i].y0 - py; v[4].z = z;
        v[5].x = sides[i].x1 - px; v[5].y = sides[i].y1 - py; v[5].z = z;
        for (int k = 0; k < 6; ++k) {
            v[k].nx = 0.0f; v[k].ny = 0.0f; v[k].nz = 1.0f;
            v[k].u  = 0.5f; v[k].v  = 0.5f;
            v[k].colour = col;
        }
    }
}

void DebugMenuButton_Draw() {
    if (!g_DebugHitboxes) return;

    const std::list<MenuButton*>& buttons = MenuButton::GetActiveButtons();
    if (buttons.empty()) return;

    EnsureWhiteTex();

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_WhiteTex);

    Renderer* r = Renderer::GetInstance();
    if (!r) return;

    // Magenta at 80% alpha (BGRA: B=0xFF G=0x00 R=0xFF A=0xCC).
    static const uint32_t kMenuBoxColour = 0xCCFF00FF;
    // 4 sides x 6 verts = 24 verts
    static QUADCUSTOMVERTEX s_BoxVerts[24];

    for (std::list<MenuButton*>::const_iterator it = buttons.begin();
         it != buttons.end(); ++it) {
        MenuButton* btn = *it;
        if (!btn || !btn->m_bActive) continue;

        float hw, hh;
        if (btn->m_bHasHitArea) {
            hw = btn->m_TargetSize.x * 0.5f;
            hh = btn->m_TargetSize.y * 0.5f;
        } else {
            hw = btn->size.x * 0.5f;
            hh = btn->size.y * 0.5f;
        }
        const float left   = btn->pos.x - hw - btn->m_AnimSpeed2;
        const float right  = btn->pos.x + hw + btn->m_AnimSpeed2;
        const float bottom = btn->pos.y - hh - btn->m_AnimSpeed;
        const float top    = btn->pos.y + hh + btn->m_AnimSpeed;

        BuildAABBOutline(s_BoxVerts, left, right, bottom, top, -0.5f, 1.5f, kMenuBoxColour);
        r->DrawTriList(s_BoxVerts, 24);
    }
}

} // namespace FN

#endif // !__bada__
