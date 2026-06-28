// BakedStringTTF — arc-text baking for MenuButton labels.
// v1.6.1 Mortar::BakedStringTTF @0x00249a5c
//
// Reuses FontCacheObjectTTF::GetGlyph + FontInterface atlas.
// Draw path mirrors BakedStringBox::Draw (GL triangle strip via vertex array).
// BakeGradient path mirrors BakedStringBox::BakeGradient (per-vertex Y-lerp).

#include "render/BakedStringTTF.h"
#include "render/FontCacheObjectTTF.h"
#include "render/FontInterface.h"
#include "render/Utf8StringIterator.h"
#include "render/MatrixManager.h"
#include "render/MatrixStack.h"
#include "render/Renderer.h"
#include "render/gl_funcs.h"
#include "math/Matrix44.h"
#include "math/Vec2.h"
#include "math/Vec3.h"
#include "math/Colour.h"
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <vector>

// Binary constants
// v1.6.1 Mortar::BakedStringTTF @0x00249a5c
static const float k_PI         = 3.14159265f;
static const float k_DEG2RAD    = 0.017453292f;
static const float k_HalfTexel  = 0.001953125f;  // 0.5/256

// Rotate a 2D point (x,y) by angle (radians), result in (ox,oy).
// v1.6.1 Rotate2DVector used in FinishMesh @0x002480a8
static void Rotate2DVector(float x, float y, float angle, float& ox, float& oy)
{
    float s = sinf(angle);
    float c = cosf(angle);
    ox = c * x - s * y;
    oy = s * x + c * y;
}

namespace Mortar {

BakedStringTTF::BakedStringTTF(FontCacheObjectTTF* fc,
                               const char* text,
                               float fontScale,
                               Colour col,
                               long alignSigned,
                               float,
                               FONT_EFFECT eff)
    : m_ScaledHeight(0.0f)
    , m_pFontCache(fc)
    , m_Text(0)
    , m_GlyphsBuilt(false)
    , m_SurfacesBuilt(false)
    , m_CircleFlag(0)
    , m_reserved5f(255)
    , m_TotalAdvance(0.0f)
{
    // Zero the base struct.
    memset(&m_Base, 0, sizeof(m_Base));

    // Weight and fmt count computation.
    // v1.6.1 BakedStringTTF ctor @0x00249a5c:
    //   n = clamp(ceil(weight * fc[+0x10c][+0x0c]), 0, 0x20)
    //   if(eff==0 && n>0) eff=1
    //   m_FmtCount(+0x2c)=n; m_Flag(+0x30)=eff
    //   m_Weight(+0x28) = signedToFloat(alignSigned) * fc[+0x10c][+0x10]
    // fc[+0x10c] is an embedded sub-struct within FontCacheObjectTTF; in the port
    // there is no matching sub-struct at that offset — this is FontCacheObjectTTF's
    // binary layout detail. The port approximates: weight affects outline count only.
    // Using alignSigned directly as the weight multiplier (= alignSigned * 1.0).
    // TODO: v1.6.1 0x00249a5c (Mortar::BakedStringTTF::BakedStringTTF) -- fc[+0x10c] weight/count fields
    //   need the full FontCacheObjectTTF binary layout to compute exactly.
    float weightF = (float)alignSigned;
    float rawN = ceilf(weightF);
    int n = (rawN < 0.0f) ? 0 : (rawN > 32.0f) ? 32 : (int)rawN;
    if (eff == FONT_EFFECT_NONE && n > 0) eff = FONT_EFFECT_BOLD;
    m_Base.m_FmtCount = (uint32_t)n;
    m_Base.m_Flag = (uint8_t)eff;
    m_Base.m_Weight = weightF;

    // m_ScaledHeight = fontScale * atlas->m_FontScale
    // v1.6.1 BakedStringTTF ctor @0x00249a5c: atlas[+0x14] = m_FontScale (=1.0 default)
    float atlasScale = 1.0f;
    if (fc) {
        FontInterface* atlas = fc->GetAtlas();
        if (atlas) atlasScale = atlas->m_FontScale;
    }
    m_ScaledHeight = fontScale * atlasScale;

    // AddColour(col, 0.0) -- store as first gradient stop
    AddColour(col, 0.0f);

    // m_Weight: in the binary = signedToFloat(alignSigned) * fc[+0x10c][+0x10]
    // Port: keep as float cast of alignSigned (already set above)

    // strdup(text)
    if (text) {
        m_Text = (char*)malloc(strlen(text) + 1);
        if (m_Text) strcpy(m_Text, text);
    } else {
        m_Text = (char*)malloc(1);
        if (m_Text) m_Text[0] = '\0';
    }

    FullInternalRebuild();
}

BakedStringTTF::~BakedStringTTF()
{
    DeleteSurfaces();
    DeleteGlyphs();
    if (m_Text) {
        free(m_Text);
        m_Text = 0;
    }
}

void BakedStringTTF::AddColour(Colour col, float t)
{
    // Store into m_Base.m_Effect colour stop slots.
    // Slot 0 when t==0.0 exactly; slot 1 for any other t.
    // The old guard "|| m_T0==0.0f" caused the 2nd AddColour call to overwrite slot 0
    // when slot 0 held 0.0 from the reset, corrupting both stops.
    if (t == 0.0f) {
        m_Base.m_Effect.m_Col0 = col;
        m_Base.m_Effect.m_T0   = t;
    } else {
        m_Base.m_Effect.m_Col1 = col;
        m_Base.m_Effect.m_Tc   = t;
    }
}

void BakedStringTTF::DeleteGlyphs()
{
    for (size_t i = 0; i < m_Glyphs.size(); ++i) {
        delete m_Glyphs[i];
    }
    m_Glyphs.clear();
    m_GlyphsBuilt = false;
}

void BakedStringTTF::DeleteSurfaces()
{
    for (size_t i = 0; i < m_Surfaces.size(); ++i) {
        BakedStringTTF_Surface* s = m_Surfaces[i];
        if (s) {
            if (s->m_Verts) {
                delete[] s->m_Verts;
                s->m_Verts = 0;
            }
            delete s;
        }
    }
    m_Surfaces.clear();
    m_SurfacesBuilt = false;
}

// BuildGlyphs @0x00248b28:
// DeleteGlyphs; Utf8StringIterator(m_Text); per cp:
//   g = fc->GetGlyph(cp, m_ScaledHeight); alloc GlyphTTF; fill from GlyphAtlasEntry;
//   m_Glyphs.push_back; m_GlyphsBuilt=true.
void BakedStringTTF::BuildGlyphs()
{
    DeleteGlyphs();
    if (!m_pFontCache || !m_Text) return;

    // Pre-render all codepoints so atlas UVs are populated before we read them.
    {
        Utf8StringIterator it(m_Text);
        while (!it.IsEmpty()) {
            m_pFontCache->GetGlyph(it.m_CurrentCodepoint, m_ScaledHeight);
            it++;
        }
    }
    FontInterface* atlas = m_pFontCache->GetAtlas();
    if (atlas) atlas->BuildPendingTextures();

    Utf8StringIterator it(m_Text);
    while (!it.IsEmpty()) {
        uint32_t cp = it.m_CurrentCodepoint;
        const GlyphAtlasEntry* entry = m_pFontCache->GetGlyph(cp, m_ScaledHeight);

        GlyphTTF* g = new GlyphTTF();
        g->m_CharCode  = cp;
        g->m_FontSize  = m_ScaledHeight;
        g->m_Font      = m_pFontCache;
        g->m_SurfaceKey = 0;  // port: single atlas, no per-page key needed
        g->m_RotAngle  = 0.0f;
        g->m_RotBasis  = Vec2(0.0f, 0.0f);
        g->m_GlyphScale = Vec2(1.0f, 1.0f);
        g->m_QuadMin  = Vec2(0.0f, 0.0f);

        if (entry) {
            // UV with half-texel inset (spec: +-0.001953125 = 0.5/256)
            // v1.6.1 FinishMesh @0x002480a8: UVs get +-halfTexel inset
            g->m_UvU0 = entry->u0 + k_HalfTexel;
            g->m_UvV0 = entry->v0 + k_HalfTexel;
            g->m_UvU1 = entry->u1 - k_HalfTexel;
            g->m_UvV1 = entry->v1 - k_HalfTexel;
            g->m_QuadSize = Vec2(entry->width, entry->height);
        } else {
            g->m_UvU0 = g->m_UvV0 = g->m_UvU1 = g->m_UvV1 = 0.0f;
            g->m_QuadSize = Vec2(0.0f, 0.0f);
        }

        m_Glyphs.push_back(g);
        it++;
    }
    m_GlyphsBuilt = true;
}

// ApplyFormatting_LeftJustify @0x00247874:
// pen-advance per-glyph position into m_QuadMin.
// +1.0 inter-glyph gap (binary constant). Sets field_60 = total advance.
void BakedStringTTF::ApplyFormatting_LeftJustify()
{
    if (!m_pFontCache) return;

    float penX = 0.0f;
    for (size_t i = 0; i < m_Glyphs.size(); ++i) {
        GlyphTTF* g = m_Glyphs[i];
        const GlyphAtlasEntry* entry = m_pFontCache->GetGlyph(g->m_CharCode, m_ScaledHeight);
        if (!entry) {
            g->m_QuadMin = Vec2(penX, 0.0f);
            continue;
        }

        // m_QuadMin: pen-space left edge of this glyph's quad.
        // x = penX + bearingX, y = bearingY (above baseline = positive).
        g->m_QuadMin = Vec2(penX + entry->bearingX, entry->bearingY);

        // Advance: advanceX + 1.0 inter-glyph gap.
        penX += entry->advanceX + 1.0f;
    }
    m_TotalAdvance = penX;
}

// BuildSurfaces @0x00248c14:
// Group glyphs by m_SurfaceKey -> one Surface per atlas page.
// Port: single Surface (one FontInterface atlas).
// FinishMesh @0x002480a8 builds the 6-vert/glyph buffer per glyph.
void BakedStringTTF::BuildSurfaces()
{
    DeleteSurfaces();
    if (!m_GlyphsBuilt || m_Glyphs.empty()) return;
    if (!m_pFontCache) return;

    // Count drawable glyphs (skip w<1 or h<1 = whitespace).
    uint32_t drawableCount = 0;
    for (size_t i = 0; i < m_Glyphs.size(); ++i) {
        GlyphTTF* g = m_Glyphs[i];
        if (g->m_QuadSize.x >= 1.0f && g->m_QuadSize.y >= 1.0f) {
            drawableCount++;
        }
    }
    if (drawableCount == 0) return;

    // Allocate one surface.
    BakedStringTTF_Surface* surf = new BakedStringTTF_Surface();
    memset(surf, 0, sizeof(BakedStringTTF_Surface));

    surf->m_DrawMode    = -1;  // single-buffer path
    surf->m_VertCount   = drawableCount * 6;
    surf->m_Verts       = new QUADCUSTOMVERTEX[surf->m_VertCount];
    memset(surf->m_Verts, 0, sizeof(QUADCUSTOMVERTEX) * surf->m_VertCount);

    // Base colour from m_Base.m_Effect.m_Col0 (the colour passed to AddColour(col,0)).
    Colour baseCol = m_Base.m_Effect.m_Col0;
    surf->m_PlatformColour = baseCol.PlatformColour();
    uint32_t packed = surf->m_PlatformColour;

    // FinishMesh @0x002480a8: build 6-vert/glyph tri-list.
    // Vertex layout (QUADCUSTOMVERTEX 0x24 bytes):
    //   +0x00 x, +0x04 y, +0x08 z=0
    //   +0x0c nx=0, +0x10 ny=0, +0x14 nz=1
    //   +0x18 colour
    //   +0x1c u, +0x20 v
    //
    // 4 corners = Rotate2DVector(corner, m_RotAngle) + m_QuadMin.
    // UVs: u0/v0/u1/v1 already have half-texel inset from BuildGlyphs.
    // Winding order: NON-flip branch (GLES port).
    // v1.6.1 FinishMesh @0x002480a8: two winding orders by FontInterface+0x14c==1 (RT Y-flip).
    // Port uses non-flip branch: tri0=(BL,TL,BR), tri1=(TR,BR,TL) = standard CCW.
    //
    // Spec vertex format (+0x00..+0x24):
    //   tri0: BL, TL, BR
    //   tri1: TR, BR, TL
    // = 6 verts per quad (degenerate tri-list).

    uint32_t vi = 0;
    for (size_t i = 0; i < m_Glyphs.size(); ++i) {
        GlyphTTF* g = m_Glyphs[i];
        // Skip whitespace / invisible glyphs.
        if (g->m_QuadSize.x < 1.0f || g->m_QuadSize.y < 1.0f) continue;

        const GlyphAtlasEntry* entry = m_pFontCache->GetGlyph(g->m_CharCode, m_ScaledHeight);
        if (!entry) continue;

        // Quad corners in pen-local space (before rotation + translation).
        // bearingX/Y define the glyph origin; QuadMin.x = penX + bearingX, QuadMin.y = bearingY.
        // Corner offsets from m_QuadMin:
        //   BL = (0,         -QuadSize.y)  (bottom of glyph, below baseline)
        //   TL = (0,          0          )  (top of glyph, at bearingY)
        //   BR = (QuadSize.x, -QuadSize.y)
        //   TR = (QuadSize.x,  0         )
        // Note: m_QuadMin.y = bearingY (top), so:
        //   actual BL world y = m_QuadMin.y - g->m_QuadSize.y = bearingY - height = bottom
        //   actual TL world y = m_QuadMin.y                   = bearingY          = top
        float qx = g->m_QuadMin.x;
        float qy = g->m_QuadMin.y;
        float qw = g->m_QuadSize.x;
        float qh = g->m_QuadSize.y;

        // Local corners relative to m_QuadMin (before rotation).
        float cx[4] = { 0.0f,  0.0f,  qw,   qw   };
        float cy[4] = { -qh,   0.0f,  -qh,  0.0f };  // BL, TL, BR, TR

        // Apply rotation then add QuadMin.
        float wx[4], wy[4];
        for (int k = 0; k < 4; k++) {
            float rx, ry;
            Rotate2DVector(cx[k], cy[k], g->m_RotAngle, rx, ry);
            wx[k] = qx + rx;
            wy[k] = qy + ry;
        }

        float u0 = g->m_UvU0, v0 = g->m_UvV0;
        float u1 = g->m_UvU1, v1 = g->m_UvV1;

        // Non-flip winding: BL(u0,v1), TL(u0,v0), BR(u1,v1), TR(u1,v0), BR, TL
        // (tri0=BL,TL,BR; tri1=TR,BR,TL reordered for strip connectivity)
        // v1.6.1 FinishMesh @0x002480a8: 6-vert emit order matches BakedStringBox.
        QUADCUSTOMVERTEX* v = surf->m_Verts + vi;
        v[0] = QUADCUSTOMVERTEX(); v[0].x=wx[0]; v[0].y=wy[0]; v[0].z=0; v[0].nx=0; v[0].ny=0; v[0].nz=1; v[0].colour=packed; v[0].u=u0; v[0].v=v1;
        v[1] = QUADCUSTOMVERTEX(); v[1].x=wx[1]; v[1].y=wy[1]; v[1].z=0; v[1].nx=0; v[1].ny=0; v[1].nz=1; v[1].colour=packed; v[1].u=u0; v[1].v=v0;
        v[2] = QUADCUSTOMVERTEX(); v[2].x=wx[2]; v[2].y=wy[2]; v[2].z=0; v[2].nx=0; v[2].ny=0; v[2].nz=1; v[2].colour=packed; v[2].u=u1; v[2].v=v1;
        v[3] = QUADCUSTOMVERTEX(); v[3].x=wx[3]; v[3].y=wy[3]; v[3].z=0; v[3].nx=0; v[3].ny=0; v[3].nz=1; v[3].colour=packed; v[3].u=u1; v[3].v=v0;
        v[4] = v[3];
        v[5] = v[3];
        vi += 6;
    }
    surf->m_VertCount = vi;

    // Store glyph range in surface (spec: m_GlyphsBegin/End).
    surf->m_GlyphsBegin = m_Glyphs.empty() ? 0 : m_Glyphs[0];
    surf->m_GlyphsEnd   = m_Glyphs.empty() ? 0 : m_Glyphs[m_Glyphs.size() - 1];

    m_Surfaces.push_back(surf);
    m_SurfacesBuilt = true;
}

// ApplyEffects @0x00249684: tail-branch dispatch.
// Not decompiled this pass; safe no-op (circle/gradient called explicitly by caller).
// Defunct: ApplyEffects dispatch -- no-op stub; v1.6.1 Mortar::BakedStringTTF::ApplyEffects @ 0x00249684
void BakedStringTTF::ApplyEffects()
{
}

// FullInternalRebuild @0x00249780:
// 1. BuildGlyphs
// 2. ApplyFormatting_LeftJustify
// 3. BuildSurfaces
// 4. ApplyEffects (no-op)
void BakedStringTTF::FullInternalRebuild()
{
    BuildGlyphs();
    ApplyFormatting_LeftJustify();
    BuildSurfaces();
    ApplyEffects();
}

// FitStringToWidth @0x00248734 (static):
// pen += glyphAdvance + 1.0; track break (whitespace/0x200b/0xa); write outWidth; split at break.
// Returns total advance; outWidth gets width up to the break point.
float BakedStringTTF::FitStringToWidth(FontCacheObjectTTF* fc, const char* text,
                                        float fontScale, float maxWidth, float* outWidth)
{
    if (!fc || !text) {
        if (outWidth) *outWidth = 0.0f;
        return 0.0f;
    }

    float total = 0.0f;
    float breakAdv = 0.0f;
    Utf8StringIterator it(text);
    while (!it.IsEmpty()) {
        uint32_t cp = it.m_CurrentCodepoint;
        // Break characters: space, zero-width space (0x200b), newline (0x0a)
        bool isBreak = (cp == ' ' || cp == 0x200b || cp == 0x0a);
        if (isBreak) {
            breakAdv = total;
        }
        const GlyphAtlasEntry* g = fc->GetGlyph(cp, fontScale);
        float adv = g ? (g->advanceX + 1.0f) : 0.0f;
        total += adv;
        if (total > maxWidth && breakAdv > 0.0f) {
            if (outWidth) *outWidth = breakAdv;
            return breakAdv;
        }
        it++;
    }
    if (outWidth) *outWidth = total;
    return total;
}

// ApplyFormatting_Circle_Internal @0x00248cc8:
// if(!m_GlyphsBuilt) return;
// DeleteSurfaces(); ApplyFormatting_LeftJustify(); field_5e=1;
// degPerUnit = 360.0 / (radius * 2*PI);
// half = degPerUnit * field_60 * 0.5;
// for each glyph g:
//   ang = PI/2 - (degPerUnit*g.x - half) * DEG2RAD;
//   r   = radius + g.y;
//   g.x = cos(ang)*r;  g.y = sin(ang)*r;
//   g.rotZ = (PI/2 - (degPerUnit*((g.x0 + g.adv*0.5) - g.bearingX) - half)*DEG2RAD) - PI/2;
// BuildSurfaces();
void BakedStringTTF::ApplyFormatting_Circle_Internal(float radius)
{
    if (!m_GlyphsBuilt) return;

    DeleteSurfaces();
    ApplyFormatting_LeftJustify();
    m_CircleFlag = 1;

    float degPerUnit = 360.0f / (radius * 2.0f * k_PI);
    float half       = degPerUnit * m_TotalAdvance * 0.5f;

    for (size_t i = 0; i < m_Glyphs.size(); ++i) {
        GlyphTTF* g = m_Glyphs[i];
        float gx = g->m_QuadMin.x;
        float gy = g->m_QuadMin.y;

        float ang = k_PI * 0.5f - (degPerUnit * gx - half) * k_DEG2RAD;
        float r   = radius + gy;

        g->m_QuadMin.x = cosf(ang) * r;
        g->m_QuadMin.y = sinf(ang) * r;

        // Rotation angle: (PI/2 - (degPerUnit*((gx0+adv*0.5) - bearingX) - half)*DEG2RAD) - PI/2
        // gx0 = original penX (before bearingX offset), adv = advanceX.
        // Since gx = penX + bearingX, and penX = gx - bearingX:
        const GlyphAtlasEntry* entry = m_pFontCache->GetGlyph(g->m_CharCode, m_ScaledHeight);
        float bearingX = entry ? entry->bearingX : 0.0f;
        float adv      = entry ? entry->advanceX : 0.0f;
        // penX = gx - bearingX
        float penX = gx - bearingX;
        float midAdv = penX + adv * 0.5f;
        float rotAngle = (k_PI * 0.5f - (degPerUnit * (midAdv - bearingX) - half) * k_DEG2RAD) - k_PI * 0.5f;
        g->m_RotAngle = rotAngle;
    }

    BuildSurfaces();
}

// ApplyFormatting_Circle @0x00248dd0:
// public sets m_Radius(+0x24)=radius then Internal.
void BakedStringTTF::ApplyFormatting_Circle(float radius)
{
    m_Base.m_Radius = radius;
    ApplyFormatting_Circle_Internal(radius);
}

// ApplyGradient_TopBottom_Internal @0x00247c54:
// if(m_SurfacesBuilt) for each surface: per-vertex vertical Y-lerp top->bottom.
// Mirrors BakedStringBox::BakeGradient (Transform_LinearGradient_TopBottom @0x00247a48).
// ASM-spec v1.6.1 BakedStringTTF::ApplyGradient_TopBottom_Internal @0x00247c54 /
//   ApplyGradient_TopBottom @0x0024863c: top/bottom passed as explicit params.
void BakedStringTTF::ApplyGradient_TopBottom_Internal(Colour top, Colour bottom)
{
    if (!m_SurfacesBuilt) return;
    if (m_Surfaces.empty()) return;

    // Find Y bounds across all drawable verts.
    float yTop = -1e30f;
    float yBot =  1e30f;
    for (size_t si = 0; si < m_Surfaces.size(); ++si) {
        BakedStringTTF_Surface* s = m_Surfaces[si];
        for (uint32_t vi = 0; vi < s->m_VertCount; ++vi) {
            float y = s->m_Verts[vi].y;
            if (y > yTop) yTop = y;
            if (y < yBot) yBot = y;
        }
    }
    float yRange = yTop - yBot;
    if (yRange < 1.0f) yRange = 1.0f;

    // Gradient stops from explicit params (not m_Base.m_Effect which may be stale).
    Colour colTop = top;
    Colour colBot = bottom;

    // Per-vertex Y-lerp.
    // Transform_LinearGradient_TopBottom @0x00247a48: t = (yTop - y) / range; lerp top->bot.
    for (size_t si = 0; si < m_Surfaces.size(); ++si) {
        BakedStringTTF_Surface* s = m_Surfaces[si];
        for (uint32_t vi = 0; vi < s->m_VertCount; ++vi) {
            float y = s->m_Verts[vi].y;
            float t;
            if (y >= yTop || y < yBot) {
                t = 0.0f;
            } else {
                t = (yTop - y) / yRange;
            }
            float fr = (colTop.r / 255.0f) * (1.0f - t) + (colBot.r / 255.0f) * t;
            float fg = (colTop.g / 255.0f) * (1.0f - t) + (colBot.g / 255.0f) * t;
            float fb = (colTop.b / 255.0f) * (1.0f - t) + (colBot.b / 255.0f) * t;
            float fa = (colTop.a / 255.0f) * (1.0f - t) + (colBot.a / 255.0f) * t;
            unsigned char r = (unsigned char)(int)(fr * 255.0f);
            unsigned char g = (unsigned char)(int)(fg * 255.0f);
            unsigned char b = (unsigned char)(int)(fb * 255.0f);
            unsigned char a = (unsigned char)(int)(fa * 255.0f);
            s->m_Verts[vi].colour = Colour(r, g, b, a).PlatformColour();
        }
    }
}

// ApplyGradient_TopBottom @0x0024863c:
// public: m_Base[0x1c]=m_Base[0x18]; AddColour(top,0.0); AddColour(bottom,1.0); then Internal.
// Offsets 0x18/0x1c within BakedStringTTF are m_Base.m_Effect.m_Col1 and m_Base.m_Effect.m_Tc.
// "0x1c <- 0x18" = copy 4 bytes at 0x18 into 0x1c = save m_Col1 raw bits into m_Tc.
void BakedStringTTF::ApplyGradient_TopBottom(Colour top, Colour bottom)
{
    // Save gradient cache: m_Base[0x1c] <- m_Base[0x18]
    // m_Effect.m_Col1 (at +0x18) raw-copied into m_Effect.m_Tc (at +0x1c).
    uint32_t saved = m_Base.m_Effect.m_Col1.PlatformColour();
    memcpy(&m_Base.m_Effect.m_Tc, &saved, sizeof(float));

    // Reset colour stops and set gradient stops.
    m_Base.m_Effect.m_Col0 = Colour(0, 0, 0, 0);
    m_Base.m_Effect.m_T0   = 0.0f;
    m_Base.m_Effect.m_Col1 = Colour(0, 0, 0, 0);
    // (m_Tc already written above; AddColour(bottom,1.0) will overwrite)

    AddColour(top,    0.0f);
    AddColour(bottom, 1.0f);

    ApplyGradient_TopBottom_Internal(top, bottom);
}

// Draw @0x002497a8:
// if(!m_SurfacesBuilt) FullInternalRebuild(); if 0 glyphs return.
// FontInterface::BuildPendingTextures(). MatrixStack reset + identity.
// if(field_5e==0) apply align (bits0-1 horiz, bits2-3 vert).
// TranslateLocal(alignOffset); Scale; RotZ; Translate(anchor); Upload.
// per surface (m_DrawMode<0): DrawTriList via vertex array.
void BakedStringTTF::Draw(const Vec3& anchor, Vec2 scale, float rotZ, uint32_t align)
{
    if (!m_SurfacesBuilt) FullInternalRebuild();
    if (m_Surfaces.empty() || m_Glyphs.empty()) return;

    FontInterface* atlas = m_pFontCache ? m_pFontCache->GetAtlas() : 0;
    if (!atlas) return;
    atlas->BuildPendingTextures();

    // Compute alignment offset.
    // v1.6.1 BakedStringTTF::Draw @0x002497a8:
    //   bits0-1 horiz: 2=right -width, 3=centre -width/2
    //   bits2-3 vert:  4=top, 0xc=centre (skipped when field_5e=1)
    float alignOffX = 0.0f;
    float alignOffY = 0.0f;

    if (m_CircleFlag == 0) {
        // Compute bounding box from surface verts.
        float xMin =  1e30f, xMax = -1e30f;
        float yMin =  1e30f, yMax = -1e30f;
        for (size_t si = 0; si < m_Surfaces.size(); ++si) {
            BakedStringTTF_Surface* s = m_Surfaces[si];
            for (uint32_t vi = 0; vi < s->m_VertCount; ++vi) {
                float x = s->m_Verts[vi].x;
                float y = s->m_Verts[vi].y;
                if (x < xMin) xMin = x;
                if (x > xMax) xMax = x;
                if (y < yMin) yMin = y;
                if (y > yMax) yMax = y;
            }
        }
        float width  = (xMax > xMin) ? (xMax - xMin) : 0.0f;
        float height = (yMax > yMin) ? (yMax - yMin) : 0.0f;

        // Horiz align (bits 0-1)
        uint32_t hAlign = align & 0x3u;
        if (hAlign == 2) {
            alignOffX = -width;
        } else if (hAlign == 3) {
            alignOffX = -width * 0.5f;
        }
        // Vert align (bits 2-3)
        uint32_t vAlign = align & 0xcu;
        if (vAlign == 4) {
            alignOffY = 0.0f;  // top
        } else if (vAlign == 0xc) {
            alignOffY = -height * 0.5f;  // centre
        }
    }

    // Build rotation coefficients for rotZ.
    float theta = rotZ * (k_PI / 180.0f);
    float sinT  = sinf(theta);
    float cosT  = cosf(theta);

    // Use identity world matrix (world space vertex draw).
    MatrixStack& world = MatrixManager::GetInstance().GetWorldStack();
    world.Push();
    world.m_Current.Identity();
    world.m_Version++;
    MatrixManager::GetInstance().UploadModelViewOnly();

    for (size_t si = 0; si < m_Surfaces.size(); ++si) {
        BakedStringTTF_Surface* s = m_Surfaces[si];
        if (!s || !s->m_Verts || s->m_VertCount == 0) continue;
        if (s->m_DrawMode >= 0) continue;  // only single-buffer path

        const uint32_t nVerts = s->m_VertCount;
        std::vector<QUADCUSTOMVERTEX> wv(s->m_Verts, s->m_Verts + nVerts);

        // Transform: alignOffset + scale + rotZ + anchor.
        for (uint32_t vi = 0; vi < nVerts; ++vi) {
            float lx = (wv[vi].x + alignOffX) * scale.x;
            float ly = (wv[vi].y + alignOffY) * scale.y;
            wv[vi].x = cosT * lx - sinT * ly + anchor.x;
            wv[vi].y = sinT * lx + cosT * ly + anchor.y;
        }

        // Wire degenerate connectors between glyphs.
        for (uint32_t gi = 1; (int)(gi * 6) < (int)nVerts; ++gi) {
            wv[gi * 6 - 1] = wv[gi * 6];
        }

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, atlas->GetTextureID());
        glEnable(GL_TEXTURE_2D);
        TexEnvModulate();  // must precede DrawTriStrip (it does not set tex-env)

        Renderer::GetInstance()->DrawTriStrip(&wv[0], (int)nVerts);
    }

    world.Pop();
}

float BakedStringTTF::GetTotalAdvance() const
{
    return m_TotalAdvance;
}

int BakedStringTTF::GetGlyphCount() const
{
    return (int)m_Glyphs.size();
}

} // namespace Mortar
