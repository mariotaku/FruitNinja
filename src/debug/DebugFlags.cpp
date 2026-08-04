//
// DebugFlags — draws fruit/bomb collision spheres as translucent
// outlined discs. Toggle with F1 in the SDL event loop (Game::run).
//

#ifndef __bada__

#include "DebugFlags.h"
#include "debug/Logger.h"
#include "entities/ActorManager.h"
#include "entities/Entity.h"
#include "entities/SlashEntity.h"
#include "hud/HUDControl.h"
#include "hud/ScrollingMenu.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/Layout.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "render/Font.h"
#include "render/BakedStringTTF.h"
#include "render/FontCacheObjectTTF.h"
#include "render/FontTTFRegistry.h"
#include "asset/TextureManager.h"
#include "math/_Vector3.h"
#include "math/Colour.h"
#include "game/GameWork.h"
#include "engine/system/PowerManager.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <list>
#include <typeinfo>

namespace FN {

int   g_DebugHitboxes        = 0;     // Port specific: 0=off 1=entity 2=+HUD 3=+font (F1 cycles)
bool  g_DebugWireframe       = false; // Port specific: desktop GL only (F2)
float g_DebugTimeScale       = 1.0f;  // Port specific: debug-only, no binary equivalent
bool  g_ShowFps              = false; // Port specific: FPS counter overlay (F3, --fps, ?fps=1)
bool  g_FpsCap60             = false; // Port specific: cap render/present rate to 60fps (SettingsScreen checkbox), default OFF
bool  g_SuppressTextOverlay  = false; // Port specific: suppresses DebugText_Overlay for debug-drawn text
bool  g_bOsdSfx              = false; // Port specific: OSD toast per SFX played (F4, --osd-sfx, ?osdsfx=1)
bool  g_MotionMode           = true;  // Port specific: velocity-gated pointer slash (F5, --motion), default ON (pointer-path only; does not affect touch input)
float g_MotionSpeedThreshold = 10.0f; // Port specific: g_MotionMode cut speed threshold, px/sim-tick (tune F6/F8)
bool  g_BombSpinTimeScaled   = false; // DIFFERS: opt-in time-scaled alive-bomb spin (F9, --bomb-spin-timescaled), default OFF = faithful

// Lazy 1x1 white texture for the vertex-colour shader path. The
// Renderer's program_vc samples a texture and multiplies by the vertex
// color; without a bound texture we'd see undefined samples. A solid
// white sample lets the vertex colour drive the visible tint.
static uint32_t s_WhiteTex = 0;

// Lazy debug font (verdana.fnt) for pointer-address labels in DebugHUDBounds_Draw.
static Mortar::SmartPtr<Mortar::Font> s_DebugFont;

static void EnsureDebugFont() {
    if (s_DebugFont.IsValid()) return;
    s_DebugFont = Mortar::Font::Create("fonts/verdana.fnt");
}

static void EnsureWhiteTex() {
    if (s_WhiteTex) return;
    s_WhiteTex = Mortar::TextureManager::CreateSolidTexture(255, 255, 255, 255);
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
    if (g_DebugHitboxes < 1) return;

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

            LOG_DEBUG("DebugHitbox", "bM_Mode=%u pmState=%u active=%d colSpheres=%d",
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

    r->BindTexture2D(s_WhiteTex);

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
    if (g_DebugHitboxes < 2) return;

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

    r->BindTexture2D(s_WhiteTex);

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
                const _Vector3<float> labelPos(ox0 + 2.0f, oy1 - 2.0f, -0.4f);
                mm.GetWorldStack().Reset();
                mm.UploadModelViewOnly();
                g_SuppressTextOverlay = true;
                s_DebugFont->DrawString(8.0f, 1.0f, 0.0f, ptrBuf, labelPos,
                                        kSmLabel, Mortar::FONT_ALIGN_LEFT);
                g_SuppressTextOverlay = false;
            }
        }

        const float hw = ctrl->size.x * 0.5f;
        const float hh = ctrl->size.y * 0.5f;
        if (hw == 0.0f && hh == 0.0f) continue;

        const _Vector3<float> dp = ctrl->GetDrawPos();
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
            const _Vector3<float> classPos(left + kLabelInsetX, top - kLabelInsetY, kLabelZ);
            const _Vector3<float> ptrPos(left + kLabelInsetX, top - kLabelInsetY - lineH, kLabelZ);

            mm.GetWorldStack().Reset();
            mm.UploadModelViewOnly();
            g_SuppressTextOverlay = true;
            s_DebugFont->DrawString(kLabelScale, 1.0f, 0.0f,
                                    className, classPos, kLabelColour,
                                    Mortar::FONT_ALIGN_LEFT);
            s_DebugFont->DrawString(kLabelScale, 1.0f, 0.0f,
                                    ptrBuf, ptrPos, kLabelColour,
                                    Mortar::FONT_ALIGN_LEFT);
            g_SuppressTextOverlay = false;
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
        if (!s_FpsTTFFont.IsValid()) {
            LOG_WARN("DebugFps", "Font::Create(fontstruetype/gangofchinese.ttf) returned null -- FPS overlay will not render");
            return 0;
        }
    }
    if (!s_FpsFontCache) {
        s_FpsFontCache = Mortar::FontTTFRegistry::GetInstance().Lookup(s_FpsTTFFont.Get());
        if (!s_FpsFontCache) {
            LOG_WARN("DebugFps", "FontTTFRegistry::Lookup returned null -- FPS overlay will not render");
            return 0;
        }
    }
    return s_FpsFontCache;
}

// Port specific: shared accessor so OSD_Draw (src/debug/OSD.cpp) renders
// toasts through the exact same lazily-created TTF font cache as the FPS
// counter.
Mortar::FontCacheObjectTTF* DebugFontTTF_Get() {
    return EnsureFpsFontCache();
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
            Mortar::FontCacheObjectTTF::FONT_EFFECT_NONE);
        // Do NOT call ApplyGradient_TopBottom here.
        // BuildSurfaces (called by the ctor) bakes all vertices to the ctor
        // base colour (255,255,255,255) -- flat white.  ApplyGradient_TopBottom
        // has a bug in its AddColour slot-selection logic: the second
        // AddColour(bottom, 1.0f) call incorrectly overwrites slot-0 (because
        // m_T0==0.0f after the first call satisfies the slot-0 condition), leaving
        // m_Col1=={0,0,0,0} (black). The gradient then lerps white->black,
        // producing the dark bottom ramp. Leaving the ctor-baked white vertices
        // untouched gives flat 255,255,255,255 on every pixel (>= 80% brightness).
        s_FpsLastInt = fpsInt;
    }

    if (!s_FpsBaked) return;

    // Project through the SAME widened ortho FruitCamera uses (0x0019e724 +
    // FruitCamera::SetupPerspective @0x001ee124's PT_GENERIC bounds --
    // SetupOrtho(160,-160,-Layout::HalfWidth(),Layout::HalfWidth(),2000,-6000)),
    // NOT Renderer::SetupGameOrtho()'s fixed -240..240. The glViewport is
    // pillarboxed to Layout::EffectiveAspect() when widescreen is on
    // (GameSDL.cpp's renderFrame), so a fixed -240..240 projection only spans
    // the CENTER of that wider viewport; the left-edge anchor at -HalfWidth()+5
    // would land outside the [-240,240] clip range and get clipped off-screen.
    // Matching the projection to HalfWidth() keeps -HalfWidth() == the true
    // viewport left edge, at any aspect. Identity at HalfWidth()==240 (3:2 /
    // __bada__): reduces to Renderer::SetupGameOrtho()'s exact -240..240.
    MatrixManager& mm = MatrixManager::GetInstance();
    mm.SetupOrtho(160.0f, -160.0f, -Layout::HalfWidth(), Layout::HalfWidth(), 2000.0f, -6000.0f);
    mm.GetViewStack().Reset();
    mm.GetWorldStack().Reset();
    mm.UploadAll();

    // BakedStringTTF::Draw sets its own GL state (blend, texture, no cull).
    // SetupOrtho's UploadAll() updates m_CachedProjView which GetMVP() reads
    // inside Draw. No additional GL state setup needed here.

    // Top-left corner anchor.
    // align=0x4: hAlign=0 (left -- text extends rightward from anchor X),
    //            vAlign=4 (no alignOffY -- baseline at anchor Y, glyphs extend
    //                      upward by their bearingY).
    // At size 12, bearingY ~= 10 units; set anchor Y to +148 so the glyph top
    // lands near +158, a few units below the top edge (+160).
    // Anchor X = -HalfWidth() + 5, just inside the (possibly widened) left edge.
    static const float kAnchorXMargin = 5.0f;   // left-edge margin, 3:2-identical
    static const float kAnchorY       = 138.0f; // top edge +160 - ~22 margin (~10px extra top spacing)
    static const float kZ             = -0.1f;  // in front of game content

    const float kAnchorX = -Layout::HalfWidth() + kAnchorXMargin;

    const _Vector3<float> anchor(kAnchorX, kAnchorY, kZ);
    const _Vector2<float> scale(1.0f, 1.0f);
    // align 0x4: bits 0-1 = 0 (left-H), bits 2-3 = 4 (V-top, no y offset).
    s_FpsBaked->Draw(anchor, scale, 0.0f, (Mortar::ALIGNMENT_TYPE)0x4);
}

// Build a thick line segment (two triangles = one quad) from (x0,y0) to (x1,y1)
// into `out` (6 verts). `thickness` is total width in world units.
static void BuildSegment(QUADCUSTOMVERTEX* out,
                         float x0, float y0,
                         float x1, float y1,
                         float z, float thickness,
                         uint32_t col) {
    const float dx  = x1 - x0;
    const float dy  = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) len = 0.001f;
    const float h   = thickness * 0.5f;
    const float px  = (-dy / len) * h;
    const float py  = ( dx / len) * h;

    out[0].x = x0 + px; out[0].y = y0 + py; out[0].z = z;
    out[1].x = x0 - px; out[1].y = y0 - py; out[1].z = z;
    out[2].x = x1 + px; out[2].y = y1 + py; out[2].z = z;
    out[3].x = x1 + px; out[3].y = y1 + py; out[3].z = z;
    out[4].x = x0 - px; out[4].y = y0 - py; out[4].z = z;
    out[5].x = x1 - px; out[5].y = y1 - py; out[5].z = z;

    for (int k = 0; k < 6; ++k) {
        out[k].nx = 0.0f; out[k].ny = 0.0f; out[k].nz = 1.0f;
        out[k].u  = 0.5f; out[k].v  = 0.5f;
        out[k].colour = col;
    }
}

// Port specific: crosshair arm length / thickness for DebugText_Overlay anchor marker.
static const float kTextAnchorArm = 5.0f;
static const float kTextAnchorThk = 0.8f;

void DebugText_Overlay(float anchorX, float anchorY,
                       bool hasBox,
                       float boxX0, float boxY0, float boxX1, float boxY1,
                       bool hasInk,
                       float inkX0, float inkY0, float inkX1, float inkY1)
{
    if (g_DebugHitboxes < 3) return;

    Renderer* r = Renderer::GetInstance();
    if (!r) return;

    EnsureWhiteTex();

    // Re-establish the scene projection + identity world matrix. This function
    // may be called mid-text-draw while the world matrix is in an arbitrary
    // state; reset to ensure overlay geometry lands in centred-ortho world space.
    r->SetupGameOrtho();

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();

    r->BindTexture2D(s_WhiteTex);

    // Colour constants (BGRA packed):
    //   MAGENTA anchor crosshair: B=0xFF G=0x00 R=0xFF A=0xFF
    static const uint32_t kAnchorCol = 0xFFFF00FF;
    //   GREEN box rect: B=0x00 G=0xFF R=0x00 A=0xFF
    static const uint32_t kBoxCol    = 0xFF00FF00;
    //   YELLOW ink rect: B=0x00 G=0xFF R=0xFF A=0xFF
    static const uint32_t kInkCol    = 0xFF00FFFF;

    static const float kZ   = -0.3f;
    static const float kThk = 0.8f;

    // Anchor crosshair: 4 arms * 6 verts = 24 verts.
    {
        static QUADCUSTOMVERTEX cv[24];
        const float arm = kTextAnchorArm;
        const float h   = kTextAnchorThk * 0.5f;
        // +X arm
        cv[ 0] = {anchorX,     anchorY + h, kZ, 0,0,1, kAnchorCol, 0.5f, 0.5f};
        cv[ 1] = {anchorX,     anchorY - h, kZ, 0,0,1, kAnchorCol, 0.5f, 0.5f};
        cv[ 2] = {anchorX+arm, anchorY + h, kZ, 0,0,1, kAnchorCol, 0.5f, 0.5f};
        cv[ 3] = {anchorX+arm, anchorY + h, kZ, 0,0,1, kAnchorCol, 0.5f, 0.5f};
        cv[ 4] = {anchorX,     anchorY - h, kZ, 0,0,1, kAnchorCol, 0.5f, 0.5f};
        cv[ 5] = {anchorX+arm, anchorY - h, kZ, 0,0,1, kAnchorCol, 0.5f, 0.5f};
        // -X arm
        cv[ 6] = {anchorX-arm, anchorY + h, kZ, 0,0,1, kAnchorCol, 0.5f, 0.5f};
        cv[ 7] = {anchorX-arm, anchorY - h, kZ, 0,0,1, kAnchorCol, 0.5f, 0.5f};
        cv[ 8] = {anchorX,     anchorY + h, kZ, 0,0,1, kAnchorCol, 0.5f, 0.5f};
        cv[ 9] = {anchorX,     anchorY + h, kZ, 0,0,1, kAnchorCol, 0.5f, 0.5f};
        cv[10] = {anchorX-arm, anchorY - h, kZ, 0,0,1, kAnchorCol, 0.5f, 0.5f};
        cv[11] = {anchorX,     anchorY - h, kZ, 0,0,1, kAnchorCol, 0.5f, 0.5f};
        // +Y arm
        cv[12] = {anchorX - h, anchorY,     kZ, 0,0,1, kAnchorCol, 0.5f, 0.5f};
        cv[13] = {anchorX + h, anchorY,     kZ, 0,0,1, kAnchorCol, 0.5f, 0.5f};
        cv[14] = {anchorX - h, anchorY+arm, kZ, 0,0,1, kAnchorCol, 0.5f, 0.5f};
        cv[15] = {anchorX - h, anchorY+arm, kZ, 0,0,1, kAnchorCol, 0.5f, 0.5f};
        cv[16] = {anchorX + h, anchorY,     kZ, 0,0,1, kAnchorCol, 0.5f, 0.5f};
        cv[17] = {anchorX + h, anchorY+arm, kZ, 0,0,1, kAnchorCol, 0.5f, 0.5f};
        // -Y arm
        cv[18] = {anchorX - h, anchorY-arm, kZ, 0,0,1, kAnchorCol, 0.5f, 0.5f};
        cv[19] = {anchorX + h, anchorY-arm, kZ, 0,0,1, kAnchorCol, 0.5f, 0.5f};
        cv[20] = {anchorX - h, anchorY,     kZ, 0,0,1, kAnchorCol, 0.5f, 0.5f};
        cv[21] = {anchorX - h, anchorY,     kZ, 0,0,1, kAnchorCol, 0.5f, 0.5f};
        cv[22] = {anchorX + h, anchorY-arm, kZ, 0,0,1, kAnchorCol, 0.5f, 0.5f};
        cv[23] = {anchorX + h, anchorY,     kZ, 0,0,1, kAnchorCol, 0.5f, 0.5f};
        r->DrawTriList(cv, 24);
    }

    // Box rect (GREEN) -- 4 sides * 6 verts = 24 verts.
    if (hasBox) {
        static QUADCUSTOMVERTEX bv[24];
        BuildAABBOutline(bv, boxX0, boxX1, boxY0, boxY1, kZ - 0.01f, kThk, kBoxCol);
        r->DrawTriList(bv, 24);
    }

    // Ink bounds rect (YELLOW) -- 4 sides * 6 verts = 24 verts.
    // Port specific: skipped when hasInk is false -- callers with no real
    // per-vertex ink measurement (BakedStringBox, which only has a declared
    // box) used to pass ink==box, drawing the same rect twice (once green,
    // once yellow) on top of itself. hasInk lets them opt out instead.
    if (hasInk) {
        static QUADCUSTOMVERTEX iv[24];
        BuildAABBOutline(iv, inkX0, inkX1, inkY0, inkY1, kZ - 0.02f, kThk, kInkCol);
        r->DrawTriList(iv, 24);
    }
}

void DebugBladeTrails_Draw() {
    if (g_DebugHitboxes < 1) return;

    Renderer* r = Renderer::GetInstance();
    if (!r) return;

    EnsureWhiteTex();

    // Restore scene ortho + identity world matrix -- same state as DebugHitbox_Draw.
    // SlashEntity positions (m_TailPos, m_HeadPos) are in the same scene world space
    // as entity ColSphere centers.
    r->SetupGameOrtho();

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();

    r->BindTexture2D(s_WhiteTex);

    // Yellow, fully opaque (BGRA: B=0x00 G=0xFF R=0xFF A=0xFF).
    static const uint32_t kBladeLineCol = 0xFF00FFFF;
    // Bright cyan end-cap markers (BGRA: B=0xFF G=0xFF R=0x00 A=0xFF) -- tail.
    static const uint32_t kTailCapCol   = 0xFFFFFF00;
    // Orange end-cap for head (BGRA: B=0x00 G=0x80 R=0xFF A=0xFF).
    static const uint32_t kHeadCapCol   = 0xFF0080FF;

    // Scratch buffer: 6 verts for the line segment + 24 for a crosshair cap = 30 max per call.
    static QUADCUSTOMVERTEX s_SegVerts[6];
    static QUADCUSTOMVERTEX s_CapVerts[24]; // BuildCrosshair uses 24 verts (4*6)

    static const float kLineThickness = 2.5f;
    static const float kCapRadius     = 4.0f;
    static const float kZ             = -0.8f; // slightly behind fruit spheres (-1.0f)

    for (int i = 0; i < 16; ++i) {
        SlashEntity* se = g_pSlashEntities[i];
        if (!se) continue;
        if (!se->IsBladeActive()) continue;

        const _Vector3<float>& tail = se->GetTailPos();
        const _Vector3<float>& head = se->GetHeadPos();

        // Draw the main trail line: tail -> head.
        BuildSegment(s_SegVerts,
                     tail.x, tail.y,
                     head.x, head.y,
                     kZ, kLineThickness, kBladeLineCol);
        r->DrawTriList(s_SegVerts, 6);

        // Tail end-cap (cyan crosshair).
        BuildCrosshair(s_CapVerts, tail.x, tail.y, kZ - 0.1f, kCapRadius, kTailCapCol);
        r->DrawTriList(s_CapVerts, 24);

        // Head end-cap (orange crosshair).
        BuildCrosshair(s_CapVerts, head.x, head.y, kZ - 0.1f, kCapRadius, kHeadCapCol);
        r->DrawTriList(s_CapVerts, 24);
    }
}

// Port specific: drop every lazily-created font object this TU owns so their
// GL handles die while the context is still live. Contract in DebugFlags.h.
void DebugFlags_ReleaseResources() {
    // s_FpsBaked points into s_FpsFontCache, which FontTTFRegistry destroys
    // together with s_FpsTTFFont -- delete it first, then drop the cache
    // pointer and the rebuild sentinel so EnsureFpsFontCache/DebugFps_Draw
    // rebuild from scratch on the next draw.
    delete s_FpsBaked;
    s_FpsBaked     = 0;
    s_FpsLastInt   = -1;
    s_FpsFontCache = 0;
    s_FpsTTFFont.SetNull();

    s_DebugFont.SetNull();
}

} // namespace FN

#endif // !__bada__
