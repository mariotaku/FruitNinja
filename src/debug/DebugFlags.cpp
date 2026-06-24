//
// DebugFlags — draws fruit/bomb collision spheres as translucent
// outlined discs. Toggle with F1 in the SDL event loop (Game::run).
//

#ifndef __bada__

#include "DebugFlags.h"
#include "entities/ActorManager.h"
#include "entities/Entity.h"
#include "hud/HUDControl.h"
#include "hud/ScrollingMenu.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "render/Font.h"
#include "render/BakedStringTTF.h"
#include "render/FontCacheObjectTTF.h"
#include "render/FontTTFRegistry.h"
#include "render/gl_funcs.h"
#include "math/Vec3.h"
#include "math/Colour.h"
#include "game/GameWork.h"
#include "engine/system/PowerManager.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <list>
#include <typeinfo>

namespace FN {

bool  g_DebugHitboxes  = false;
bool  g_DebugWireframe = false; // Port specific: desktop GL only (F2)
float g_DebugTimeScale = 1.0f; // Port specific: debug-only, no binary equivalent
bool  g_ShowFps        = false; // Port specific: FPS counter overlay (F3, --fps, ?fps=1)

// Lazy 1x1 white texture for the vertex-colour shader path. The
// Renderer's program_vc samples a texture and multiplies by the vertex
// color; without a bound texture we'd see undefined samples. A solid
// white sample lets the vertex colour drive the visible tint.
static GLuint s_WhiteTex = 0;

// Lazy debug font (verdana.fnt) for pointer-address labels in DebugHUDBounds_Draw.
static Mortar::SmartPtr<Mortar::Font> s_DebugFont;

static void EnsureDebugFont() {
    if (s_DebugFont.IsValid()) return;
    s_DebugFont = Mortar::Font::Create("fonts/verdana.fnt");
}

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

    // Port specific: periodic diagnostic log (~1/s) to help debug "slicing
    // doesn't cut" in the tutorial. Logs the active-gate state so we can see
    // whether the collision gate is open or closed, and the number of entities
    // with live collision spheres.
    {
        static int s_diagFrames = 0;
        static const int DIAG_INTERVAL = 60; // ~1 second at 60 ticks/s
        ++s_diagFrames;
        if (s_diagFrames >= DIAG_INTERVAL) {
            s_diagFrames = 0;

            uint8_t bm  = game_work.bM_Mode ? 1 : 0;
            uint32_t pm = Mortar::PowerManager::GetInstance()->GetState();
            int active  = (bm == 0 && pm == 0) ? 1 : 0;

            // Count entities with live ColSpheres.
            int sphereCount = 0;
            for (int t = 0; t <= 1; t++) {
                const std::list<Mortar::Entity*>& lst = am->GetTypeList(t);
                for (std::list<Mortar::Entity*>::const_iterator it2 = lst.begin();
                     it2 != lst.end(); ++it2) {
                    Mortar::Entity* e2 = *it2;
                    if (e2 && e2->IsActive() && e2->m_Col) {
                        ColSphere* cs2 = static_cast<ColSphere*>(e2->m_Col);
                        if (cs2->radius > 0.0f) ++sphereCount;
                    }
                }
            }

            printf("[DebugHitbox] bM_Mode=%u pmState=%u active=%d colSpheres=%d\n",
                   (unsigned)bm, (unsigned)pm, active, sphereCount);
        }
    }

    EnsureWhiteTex();

    Renderer* r = Renderer::GetInstance();
    if (!r) return;

    // Re-establish the scene projection + view before drawing. DebugHitbox_Draw
    // runs after all HUD::Draw passes (GameInit.cpp:766), which leave the HUD
    // matrix active in MatrixManager. Entity ColSphere centers are in scene
    // world space, so we must restore the scene ortho + identity view to
    // project them identically to how the fruit/bomb models were drawn.
    r->SetupGameOrtho();

    // Reset world matrix — entity m_Col centres are already in world
    // space, so we draw at identity transform.
    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_WhiteTex);

    // Scratch vertex buffer: ring (32*6=192) + crosshair (4*6=24) = 216 verts/entity.
    static QUADCUSTOMVERTEX s_Verts[216];

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
        const float cx = cs->center().x;
        const float cy = cs->center().y;
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

static const char* StripMangle(const char* s) {
    if (!s || *s == '\0') return "?";
    // GCC: mangled name starts with decimal digit(s) for name length (e.g. "9MenuButton")
    while (*s >= '0' && *s <= '9') ++s;
    // MSVC: "class MenuButton" or "struct Foo"
    if (strncmp(s, "class ", 6) == 0) s += 6;
    if (strncmp(s, "struct ", 7) == 0) s += 7;
    return (*s != '\0') ? s : "?";
}

void DebugHUDBounds_Draw() {
    if (!g_DebugHitboxes) return;

    const std::list<HUDControl*>& controls = HUDControl::GetActiveControls();
    if (controls.empty()) return;

    EnsureWhiteTex();
    EnsureDebugFont();

    Renderer* r = Renderer::GetInstance();
    if (!r) return;

    // Re-establish the scene projection + view before drawing. DebugHUDBounds_Draw
    // runs after all HUD::Draw passes (GameInit.cpp:767), which leave the HUD
    // matrix active in MatrixManager. HUDControl::GetDrawPos() returns positions
    // in centered scene world space (same coordinate space as the scene ortho),
    // so we must restore the scene projection to align the AABB overlays with
    // the visible HUD controls.
    r->SetupGameOrtho();

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_WhiteTex);

    // Magenta at 80% alpha (BGRA: B=0xFF G=0x00 R=0xFF A=0xCC).
    static const uint32_t kHUDBoxColour = 0xCCFF00FF;
    // 4 sides x 6 verts = 24 verts
    static QUADCUSTOMVERTEX s_BoxVerts[24];

    // Yellow, fully opaque for high contrast against magenta outline.
    static const Colour kLabelColour(255, 255, 0, 255);
    static const float kLabelScale  = 8.0f;
    static const float kLabelInsetX = 2.0f;
    static const float kLabelInsetY = 2.0f;
    static const float kLabelZ      = -0.4f; // slightly in front of the AABB outline

    for (std::list<HUDControl*>::const_iterator it = controls.begin();
         it != controls.end(); ++it) {
        HUDControl* ctrl = *it;
        if (!ctrl || !ctrl->m_Active) continue;

        // ScrollingMenu: overlay the touch-acquire regions BEFORE the
        // size==0 gate, since ScrollingMenu typically has size=(0,0) but
        // its touch hitbox lives in m_OuterRegion / m_InnerRegion.
        // - OuterRegion (cyan): the bounds that acquire a new touch.
        // - InnerRegion (yellow): the drag-tracking bounds.
        // Bounds formula must match ScrollingMenu::Update Phase 2 exactly.
        if (ScrollingMenu* sm = dynamic_cast<ScrollingMenu*>(ctrl)) {
            // Outer region (cyan @ 70%): BGRA = B=0xFF G=0xFF R=0x00 A=0xB0
            static const uint32_t kOuterColour = 0xB0FFFF00;
            // Inner region (yellow @ 70%): BGRA = B=0x00 G=0xFF R=0xFF A=0xB0
            static const uint32_t kInnerColour = 0xB000FFFF;

            // Region layout: [0]=LEFT, [1]=TOP, [2]=RIGHT, [3]=BOTTOM.
            const float ox0 = sm->pos.x + sm->m_OuterRegion[0];  // LEFT
            const float oy1 = sm->pos.y + sm->m_OuterRegion[1];  // TOP
            const float ox1 = sm->pos.x + sm->m_OuterRegion[2];  // RIGHT
            const float oy0 = sm->pos.y + sm->m_OuterRegion[3];  // BOTTOM
            BuildAABBOutline(s_BoxVerts, ox0, ox1, oy0, oy1, -0.6f, 2.0f, kOuterColour);
            r->DrawTriList(s_BoxVerts, 24);

            const float ix0 = sm->pos.x + sm->m_InnerRegion[0];  // LEFT
            const float iy1 = sm->pos.y + sm->m_InnerRegion[1];  // TOP
            const float ix1 = sm->pos.x + sm->m_InnerRegion[2];  // RIGHT
            const float iy0 = sm->pos.y + sm->m_InnerRegion[3];  // BOTTOM
            BuildAABBOutline(s_BoxVerts, ix0, ix1, iy0, iy1, -0.7f, 1.5f, kInnerColour);
            r->DrawTriList(s_BoxVerts, 24);

            // Label at the outer-rect corner so we know which control this is.
            if (s_DebugFont.IsValid()) {
                char ptrBuf[40];
                snprintf(ptrBuf, sizeof(ptrBuf), "ScrollingMenu %p outer=(%.0f..%.0f,%.0f..%.0f)",
                         static_cast<void*>(sm), ox0, ox1, oy0, oy1);
                static const Colour kSmLabel(0, 255, 255, 255);
                const Vec3 labelPos(ox0 + 2.0f, oy1 - 2.0f, -0.4f);
                mm.GetWorldStack().Reset();
                mm.UploadModelViewOnly();
                s_DebugFont->DrawString(8.0f, 1.0f, 0.0f, ptrBuf, labelPos,
                                        kSmLabel, Mortar::FONT_ALIGN_LEFT);
            }
        }

        const float hw = ctrl->size.x * 0.5f;
        const float hh = ctrl->size.y * 0.5f;
        if (hw == 0.0f && hh == 0.0f) continue;

        const Vec3 dp = ctrl->GetDrawPos();
        const float left   = dp.x - hw;
        const float right  = dp.x + hw;
        const float bottom = dp.y - hh;
        const float top    = dp.y + hh;

        BuildAABBOutline(s_BoxVerts, left, right, bottom, top, -0.5f, 1.5f, kHUDBoxColour);
        r->DrawTriList(s_BoxVerts, 24);

        if (s_DebugFont.IsValid()) {
            const char* className = StripMangle(typeid(*ctrl).name());
            char ptrBuf[32];
            snprintf(ptrBuf, sizeof(ptrBuf), "%p", static_cast<void*>(ctrl));

            // Class name on the top line, pointer address one line below.
            // kLabelScale is 8pt; use the same scale for both lines so the
            // vertical spacing is one line height (kLabelScale pixels).
            const float lineH = kLabelScale + 1.0f;
            const Vec3 classPos(left + kLabelInsetX, top - kLabelInsetY,        kLabelZ);
            const Vec3 ptrPos  (left + kLabelInsetX, top - kLabelInsetY - lineH, kLabelZ);

            mm.GetWorldStack().Reset();
            mm.UploadModelViewOnly();
            s_DebugFont->DrawString(kLabelScale, 1.0f, 0.0f,
                                    className, classPos, kLabelColour,
                                    Mortar::FONT_ALIGN_LEFT);
            s_DebugFont->DrawString(kLabelScale, 1.0f, 0.0f,
                                    ptrBuf, ptrPos, kLabelColour,
                                    Mortar::FONT_ALIGN_LEFT);
        }
    }
}

// Lazy TTF font (gangofchinese.ttf) for the FPS overlay — same face as MenuButton.
static Mortar::SmartPtr<Mortar::Font> s_FpsTTFFont;
// Cached FontCacheObjectTTF* (owned by FontTTFRegistry, valid as long as s_FpsTTFFont is valid).
static Mortar::FontCacheObjectTTF* s_FpsFontCache = 0;
// Baked string for the FPS counter. Rebuilt only when the displayed integer changes.
static Mortar::BakedStringTTF* s_FpsBaked = 0;
// Last integer value that s_FpsBaked was built for (sentinel -1 = not yet built).
static int s_FpsLastInt = -1;

static Mortar::FontCacheObjectTTF* EnsureFpsFontCache() {
    if (!s_FpsTTFFont.IsValid()) {
        s_FpsTTFFont = Mortar::Font::Create("fontstruetype/gangofchinese.ttf");
        s_FpsFontCache = 0;
    }
    if (!s_FpsTTFFont.IsValid()) return 0;
    if (!s_FpsFontCache)
        s_FpsFontCache = Mortar::FontTTFRegistry::GetInstance().Lookup(s_FpsTTFFont.Get());
    return s_FpsFontCache;
}

void DebugFps_Draw(float fps) {
    if (!g_ShowFps || fps <= 0.0f) return;

    Mortar::FontCacheObjectTTF* fc = EnsureFpsFontCache();
    if (!fc) return;

    Renderer* r = Renderer::GetInstance();
    if (!r) return;

    const int fpsInt = (int)(fps + 0.5f);

    if (fpsInt != s_FpsLastInt) {
        delete s_FpsBaked;
        s_FpsBaked = 0;
        s_FpsLastInt = -1;

        char buf[16];
        snprintf(buf, sizeof(buf), "FPS %d", fpsInt);

        // Plain white, size 12, no circle, no gradient, no glow.
        // weight=0 (left-aligned), FONT_EFFECT_NONE.
        s_FpsBaked = new Mortar::BakedStringTTF(
            fc, buf, 12.0f,
            Colour(255, 255, 255, 255),
            0L, 0.0f,
            Mortar::FONT_EFFECT_NONE);
        // Solid white: set both gradient stops to white so no colour shift occurs.
        s_FpsBaked->ApplyGradient_TopBottom(
            Colour(255, 255, 255, 255),
            Colour(255, 255, 255, 255));
        s_FpsLastInt = fpsInt;
    }

    if (!s_FpsBaked) return;

    // Restore game ortho so the coordinates match game space (centered 480x320).
    r->SetupGameOrtho();

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();

    // Top-left corner in centered game space:
    // X axis: +160 = top,  -160 = bottom  (landscape)
    // Y axis: -240 = left, +240 = right   (landscape)
    // Small margin from the edges so text is not clipped.
    static const float kMarginX = 5.0f;   // inset from top  edge (+160 side)
    static const float kMarginY = 8.0f;   // inset from left edge (-240 side)
    static const float kZ       = -0.1f;  // slightly in front of everything
    static const float kSize    = 12.0f;

    const Vec3 anchor(160.0f - kMarginX - kSize, -240.0f + kMarginY, kZ);
    const Vec2 scale(1.0f, 1.0f);
    s_FpsBaked->Draw(anchor, scale, 0.0f, 0xf);
}

} // namespace FN

#endif // !__bada__
