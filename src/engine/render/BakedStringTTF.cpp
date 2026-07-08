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
#include <string>
#include <vector>
#include <map>
#include "core/MortarTypes.h"

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
                               float effectSize,
                               FontCacheObjectTTF::FONT_EFFECT_ENUM eff)
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

    // Effect count + weight computation.
    // v1.6.1 BakedStringTTF ctor @0x00249a5c:
    //   n = clamp(ceil(effectSize * fc[+0x10c][+0x0c]), 0, 0x20)   <- 6th float param
    //   if(eff==0 && n>0) eff=1
    //   m_FmtCount(+0x2c)=n; m_Flag(+0x30)=eff
    //   m_Weight(+0x28) = signedToFloat(alignSigned) * fc[+0x10c][+0x10]
    // The radius/count comes from effectSize (the 6th float), NOT alignSigned;
    // alignSigned drives the weight multiplier. fc[+0x10c] is an embedded sub-struct
    // within FontCacheObjectTTF with no port-side counterpart at that offset, so the
    // two scale factors are approximated as 1.0.
    float scaleA = 1.0f; // TODO: v1.6.1 fc[+0x10c][+0xc] (m_FontScale); approximated 1.0
    int n = (int)ceilf(effectSize * scaleA);
    if (n < 0) n = 0; else if (n > 32) n = 32;
    if (eff == FontCacheObjectTTF::FONT_EFFECT_NONE && n > 0) eff = FontCacheObjectTTF::FONT_EFFECT_STROKE;
    m_Base.m_FmtCount = (uint32_t)n;
    m_Base.m_Flag = (uint8_t)eff;
    m_Base.m_Weight = (float)alignSigned * 1.0f; // TODO: fc[+0x10c][+0x10] approx 1.0

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

    // Effect + radius drive a SEPARATE glyph rasterisation (blur/stroke/bevel).
    // ASM-spec v1.6.1 BakedStringTTF::BuildGlyphs @0x00248b28: FetchGlyph passes
    //   m_FmtCount(radius)+m_Flag(effect). Without these the effect layers rasterise
    //   the plain sharp glyph, so blur/stroke never render.
    FontCacheObjectTTF::FONT_EFFECT_ENUM eff = (FontCacheObjectTTF::FONT_EFFECT_ENUM)m_Base.m_Flag;
    int rad = (int)m_Base.m_FmtCount;

    // Pre-render all codepoints so atlas UVs are populated before we read them.
    {
        Utf8StringIterator it(m_Text);
        while (!it.IsEmpty()) {
            m_pFontCache->GetGlyph(it.m_CurrentCodepoint, m_ScaledHeight, eff, rad);
            it++;
        }
    }
    FontInterface* atlas = m_pFontCache->GetAtlas();
    if (atlas) atlas->BuildPendingTextures();

    Utf8StringIterator it(m_Text);
    while (!it.IsEmpty()) {
        uint32_t cp = it.m_CurrentCodepoint;
        const GlyphAtlasEntry* entry = m_pFontCache->GetGlyph(cp, m_ScaledHeight, eff, rad);

        GlyphTTF* g = new GlyphTTF();
        g->m_CharCode  = cp;
        g->m_FontSize  = m_ScaledHeight;
        g->m_Font      = m_pFontCache;
        g->m_SurfaceKey = 0;  // set below from entry->pageTextureID
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
            // Store page texture ID as surface key so BuildSurfaces can group by page.
            // DIFFERS: binary uses TextureAtlasPage* as key; port uses resolved GL texture ID.
            g->m_SurfaceKey = (void*)(uintptr_t)entry->pageTextureID;
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
// DIFFERS: binary groups by TextureAtlasPage*; port groups by resolved GL texture ID
//   stored in g->m_SurfaceKey (set in BuildGlyphs from entry->pageTextureID).
// FinishMesh @0x002480a8 builds the 6-vert/glyph buffer per glyph.
void BakedStringTTF::BuildSurfaces()
{
    DeleteSurfaces();
    if (!m_GlyphsBuilt || m_Glyphs.empty()) return;
    if (!m_pFontCache) return;

    // Collect drawable glyph indices grouped by page, preserving page insertion order.
    // Using a std::vector<uint32_t> for page order and a std::map for index lookup.
    std::vector<uint32_t> pageOrder;
    std::map<uint32_t, std::vector<size_t> > pageGlyphs;

    for (size_t i = 0; i < m_Glyphs.size(); ++i) {
        GlyphTTF* g = m_Glyphs[i];
        if (g->m_QuadSize.x < 1.0f || g->m_QuadSize.y < 1.0f) continue;
        uint32_t texID = (uint32_t)(uintptr_t)g->m_SurfaceKey;
        if (pageGlyphs.find(texID) == pageGlyphs.end()) {
            pageOrder.push_back(texID);
            pageGlyphs[texID] = std::vector<size_t>();
        }
        pageGlyphs[texID].push_back(i);
    }

    if (pageOrder.empty()) return;

    // Base colour from m_Base.m_Effect.m_Col0 (the colour passed to AddColour(col,0)).
    Colour baseCol = m_Base.m_Effect.m_Col0;
    uint32_t packed = baseCol.PlatformColour();

    // FinishMesh @0x002480a8: build 6-vert/glyph tri-list per page.
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

    for (size_t pi = 0; pi < pageOrder.size(); ++pi) {
        uint32_t texID = pageOrder[pi];
        const std::vector<size_t>& glyphIdxs = pageGlyphs[texID];
        if (glyphIdxs.empty()) continue;

        BakedStringTTF_Surface* surf = new BakedStringTTF_Surface();
        memset(surf, 0, sizeof(BakedStringTTF_Surface));

        surf->m_DrawMode       = -1;  // single-buffer path
        surf->m_VertCount      = (uint32_t)glyphIdxs.size() * 6;
        surf->m_Verts          = new QUADCUSTOMVERTEX[surf->m_VertCount];
        memset(surf->m_Verts, 0, sizeof(QUADCUSTOMVERTEX) * surf->m_VertCount);
        surf->m_PageTextureID  = texID;
        surf->m_PlatformColour = packed;

        uint32_t vi = 0;
        for (size_t ii = 0; ii < glyphIdxs.size(); ++ii) {
            size_t i = glyphIdxs[ii];
            GlyphTTF* g = m_Glyphs[i];

            // Quad corners in pen-local space (before rotation + translation).
            float qx = g->m_QuadMin.x;
            float qy = g->m_QuadMin.y;
            float qw = g->m_QuadSize.x;
            float qh = g->m_QuadSize.y;

            float cx[4] = { 0.0f,  0.0f,  qw,   qw   };
            float cy[4] = { -qh,   0.0f,  -qh,  0.0f };  // BL, TL, BR, TR

            float wx[4], wy[4];
            for (int k = 0; k < 4; k++) {
                float rx, ry;
                Rotate2DVector(cx[k], cy[k], g->m_RotAngle, rx, ry);
                wx[k] = qx + rx;
                wy[k] = qy + ry;
            }

            float u0 = g->m_UvU0, v0 = g->m_UvV0;
            float u1 = g->m_UvU1, v1 = g->m_UvV1;

            // 6-vert TRI-LIST per glyph (two triangles covering the quad):
            //   tri0 = (BL, TL, BR), tri1 = (TR, BR, TL).
            // v1.6.1 FinishMesh @0x002480a8 emits a tri-list consumed by Mesh::DrawTriList.
            // (Previously v[4]=v[5]=TR -- a tri-STRIP+degenerate-padding hack that only
            //  rendered as a strip via the old Draw's runtime connector wiring; that path
            //  is gone now that Draw calls DrawTriList over these verts directly, so the
            //  second triangle must be real. tri1's winding (TR,BR,TL) shares the BR-TL
            //  diagonal with the old strip -> identical pixel coverage; cull is off.)
            QUADCUSTOMVERTEX* v = surf->m_Verts + vi;
            v[0] = QUADCUSTOMVERTEX(); v[0].x=wx[0]; v[0].y=wy[0]; v[0].z=0; v[0].nx=0; v[0].ny=0; v[0].nz=1; v[0].colour=packed; v[0].u=u0; v[0].v=v1; // BL
            v[1] = QUADCUSTOMVERTEX(); v[1].x=wx[1]; v[1].y=wy[1]; v[1].z=0; v[1].nx=0; v[1].ny=0; v[1].nz=1; v[1].colour=packed; v[1].u=u0; v[1].v=v0; // TL
            v[2] = QUADCUSTOMVERTEX(); v[2].x=wx[2]; v[2].y=wy[2]; v[2].z=0; v[2].nx=0; v[2].ny=0; v[2].nz=1; v[2].colour=packed; v[2].u=u1; v[2].v=v1; // BR
            v[3] = QUADCUSTOMVERTEX(); v[3].x=wx[3]; v[3].y=wy[3]; v[3].z=0; v[3].nx=0; v[3].ny=0; v[3].nz=1; v[3].colour=packed; v[3].u=u1; v[3].v=v0; // TR
            v[4] = v[2]; // BR
            v[5] = v[1]; // TL
            vi += 6;
        }
        surf->m_VertCount = vi;

        // Store first and last glyph pointers for this surface.
        surf->m_GlyphsBegin = m_Glyphs[glyphIdxs.front()];
        surf->m_GlyphsEnd   = m_Glyphs[glyphIdxs.back()];

        m_Surfaces.push_back(surf);
    }

    m_SurfacesBuilt = !m_Surfaces.empty();

    // Binary's only UpdateBounds caller is BuildSurfaces' tail (@0x00248c14).
    UpdateBounds();
}

// UpdateBounds @0x00247ed0:
// Seed {minX=999999, maxY=-999999, maxX=-999999, minY=999999} then fold each surface's
// vertex extents (truncated to long) into m_Base. Field mapping matches MortarRectangleT<long>:
//   m_BoundsMinX=left=min(x), m_BoundsMaxX=right=max(x),
//   m_BoundsMaxY=top=max(y),  m_BoundsMinY=bottom=min(y).
// TODO: v1.6.1 BakedStringTTF::UpdateBounds @0x00247ed0 -- NOT split into a per-surface
//   helper (binary folds each surface's +0x28..+0x34 extent fields). The port's
//   BakedStringTTF_Surface reuses +0x28 for m_PageTextureID and +0x2c..+0x34 as pads,
//   so storing per-surface bounds there would corrupt the GL texture ID. Kept inline
//   here; values are already correct. Splitting needs a surface-layout rework first.
void BakedStringTTF::UpdateBounds()
{
    long minX =  999999;
    long maxY = -999999;
    long maxX = -999999;
    long minY =  999999;

    for (size_t si = 0; si < m_Surfaces.size(); ++si) {
        BakedStringTTF_Surface* s = m_Surfaces[si];
        if (!s || !s->m_Verts) continue;
        for (uint32_t vi = 0; vi < s->m_VertCount; ++vi) {
            long x = (long)s->m_Verts[vi].x;
            long y = (long)s->m_Verts[vi].y;
            if (x < minX) minX = x;
            if (x > maxX) maxX = x;
            if (y > maxY) maxY = y;
            if (y < minY) minY = y;
        }
    }

    m_Base.m_BoundsMinX = minX;
    m_Base.m_BoundsMaxY = maxY;
    m_Base.m_BoundsMaxX = maxX;
    m_Base.m_BoundsMinY = minY;
}

// ApplyGradientSplit @0x00249bf4:
// AddColour(c, y) records the split stop, then every vertex above the split plane is
// repainted to c. plane = y * (m_BoundsMaxY + m_BoundsMinY); paint where vertY > plane.
// Split math is the metallic split lifted from BakedStringBox::BakeGradient
// (Transform_GradientSplit @0x0024954c): d = -(sum*frac); paint side vertY > -d.
// ASM-spec v1.6.1 BakedStringTTF::ApplyGradientSplit @0x00249bf4 / Transform_GradientSplit @0x0024954c.
void BakedStringTTF::ApplyGradientSplit(Colour c, float y)
{
    AddColour(c, y);
    ApplyGradientSplit_Internal(c, y);
}

// ApplyGradientSplit_Internal @0x002495fc: per-surface split-paint dispatch.
// Passes this object's m_Base bounds (aliased as a MortarRectangleT<long>) so each
// surface computes its split plane from the shared FG bbox.
void BakedStringTTF::ApplyGradientSplit_Internal(Colour c, float y)
{
    if (!m_SurfacesBuilt) return;
    for (size_t si = 0; si < m_Surfaces.size(); ++si) {
        BakedStringTTF_Surface* s = m_Surfaces[si];
        if (!s || !s->m_Verts) continue;
        s->Transform_GradientSplit(c, y, *GetRefRect());
    }
}

// BakedStringTTF_Surface::Transform_GradientSplit @0x0024954c: repaint every vertex
// above the split plane to c. plane = y * (rect.top + rect.bottom) = y*(maxY+minY).
void BakedStringTTF_Surface::Transform_GradientSplit(Colour c, float y, MortarRectangleT<long>& rect)
{
    uint32_t packed = c.PlatformColour();
    float plane = y * (float)(rect.top + rect.bottom);
    for (uint32_t vi = 0; vi < m_VertCount; ++vi) {
        if (m_Verts[vi].y > plane) {
            m_Verts[vi].colour = packed;
        }
    }
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
// ASM-spec v1.6.1 BakedStringTTF::FitStringToWidth @0x00248734
// Word-wrap line-breaker: modifies ioText in-place to the head that fits within maxWidth,
// sets outRemainder to the overflow tail, outWidth to the measured advance of the head,
// and outTruncated when an unbreakable word overflows.
// per-glyph advance = advance (binary's +1.0 and alignArg*fontScale=-1.0 cancel);
// whitespace/0x200b/0x0a = break point.
void BakedStringTTF::FitStringToWidth(FontCacheObjectTTF* fc, std::string& ioText,
                                       std::string& outRemainder, float fontSize,
                                       long maxWidth, int /*mode*/,
                                       float* outWidth, bool* outTruncated)
{
    outRemainder.clear();
    if (outTruncated) *outTruncated = false;

    if (!fc || ioText.empty()) {
        if (outWidth) *outWidth = 0.0f;
        return;
    }

    const char* text        = ioText.c_str();
    const char* cursor      = text;
    float       total       = 0.0f;
    float       breakAdv    = 0.0f;
    const char* breakCursor = 0;   // byte position immediately after last break char

    while (*cursor != '\0') {
        uint32_t cp = utf8::decode_next_unicode_character(&cursor);
        // Break characters: space (0x20), zero-width space (0x200b), newline (0x0a)
        bool isBreak = (cp == ' ' || cp == 0x200b || cp == 0x0a);
        if (isBreak) {
            breakAdv    = total;
            breakCursor = cursor;
        }
        const GlyphAtlasEntry* g = fc->GetGlyph(cp, fontSize);
        // ASM-spec v1.6.1 BakedStringTTF::FitStringToWidth @0x00248734:
        //   per-glyph: total += advance + alignArg*fontScaleFactor + 1.0f.
        //   alignArg = -1 (all v1.6.1 call sites), fontScaleFactor = *(*(fc+0x10c)+0x10) = 1.0
        //   -> net = advance + (-1.0) + 1.0 = advance. The earlier port kept the +1.0 but
        //   dropped the -1.0 term, over-measuring by 1px/glyph -> MenuButton arc labels
        //   (curved ring text) were over-shrunk.
        float adv = g ? g->advanceX : 0.0f;
        total += adv;
        if (total > (float)maxWidth) {
            if (breakCursor) {
                // Split at last break: head = [text, breakCursor), tail = rest.
                if (outWidth) *outWidth = breakAdv;
                outRemainder = std::string(breakCursor);   // construct tail first (text still valid)
                ioText       = std::string(text, (size_t)(breakCursor - text));
            } else {
                // No break point: unbreakable overflow.
                if (outTruncated) *outTruncated = true;
                if (outWidth) *outWidth = total;
                // ioText left as-is (no split possible).
            }
            return;
        }
    }
    // Everything fits within maxWidth.
    if (outWidth) *outWidth = total;
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
    // v1.6.1 ApplyGradient_TopBottom @0x0024863c: only the save + two AddColour +
    //   Internal; the binary does NOT zero m_Col0/m_T0/m_Col1 (AddColour overwrites
    //   both stops immediately, and Internal lerps from explicit params anyway).
    uint32_t saved = m_Base.m_Effect.m_Col1.PlatformColour();
    memcpy(&m_Base.m_Effect.m_Tc, &saved, sizeof(float));

    AddColour(top,    0.0f);
    AddColour(bottom, 1.0f);

    ApplyGradient_TopBottom_Internal(top, bottom);
}

// Draw @0x002497a8:
// ASM-spec v1.6.1 BakedStringTTF::Draw @0x002497a8:
//   (Vec3 anchor, Vec2 scale, float rotZ, ALIGNMENT_TYPE, MortarRectangleT<long>* refRect=nullptr)
//
// Pure-matrix pipeline (no CPU per-vertex transform, no vertex copy). The world
// stack composes
//   M = T(anchor) * RotZ(rotZ) * ScaleRows(scale) * TranslateLocal(alignOff)
// which is algebraically identical to the previous CPU loop
//   v' = R(rotZ) * S(scale) * (v + alignOff) + anchor
// so the on-screen text is unchanged; GL consumes the baked local verts directly.
//
// if(!m_SurfacesBuilt) FullInternalRebuild(); if 0 glyphs return.
// FontInterface::BuildPendingTextures(); world.Reset().
// Alignment (skipped when field_5e/m_CircleFlag != 0): bits0-1 horiz, bits2-3 vert.
//   refRect defaults to this (GetRefRect) -- the m_Base bounds aliased as a
//   MortarRectangleT<long>; bounds are read as long, per the binary.
void BakedStringTTF::Draw(const Vec3& anchor, Vec2 scale, float rotZ, ALIGNMENT_TYPE align,
                           MortarRectangleT<long>* refRect)
{
    if (!m_SurfacesBuilt) FullInternalRebuild();
    if (m_Surfaces.empty() || m_Glyphs.empty()) return;

    FontInterface* atlas = m_pFontCache ? m_pFontCache->GetAtlas() : 0;
    if (!atlas) return;
    atlas->BuildPendingTextures();

    // Alignment offset (Vec3; z=0). Skipped entirely for circle-layout (field_5e != 0).
    // v1.6.1 BakedStringTTF::Draw @0x002497a8:
    //   bits0-1 horiz: 2=right -width, 3=centre -width/2
    //   bits2-3 vert:  4=top, 0xc=centre
    Vec3 alignOff(0.0f, 0.0f, 0.0f);

    if (m_CircleFlag == 0) {
        // Default refRect to this object's own bounds (binary: GetRefRect() == this).
        if (refRect == 0) refRect = GetRefRect();
        // MortarRectangleT<long>: left=m_BoundsMinX(+0), top=m_BoundsMaxY(+4),
        //                         right=m_BoundsMaxX(+8), bottom=m_BoundsMinY(+0xc).
        float xMin = (float)refRect->left;
        float xMax = (float)refRect->right;
        float yMin = (float)refRect->bottom;
        float yMax = (float)refRect->top;
        float width  = (xMax > xMin) ? (xMax - xMin) : 0.0f;
        float height = (yMax > yMin) ? (yMax - yMin) : 0.0f;

        // Horiz align (bits 0-1) -- binary width formula.
        uint32_t hAlign = align & 0x3u;
        if (hAlign == 2) {
            alignOff.x = -width;
        } else if (hAlign == 3) {
            alignOff.x = -width * 0.5f;
        }
        // Vert align (bits 2-3) -- KEEP the port's existing result (its local space
        // is Y-flipped vs the binary; the on-screen positions are already correct).
        uint32_t vAlign = align & 0xcu;
        if (vAlign == 4) {
            alignOff.y = 0.0f;          // top
        } else if (vAlign == 0xc) {
            alignOff.y = -height * 0.5f; // centre
        }
    }

    // Build the world matrix. Steps (binary order):
    //   1 Reset, 2 TranslateLocal(alignOff), 3 ScaleRows(scale) [row/left scale],
    //   4 RotZ(rotZ), 5 Translate(anchor) [applies anchor.z], 6 OMITTED.
    // step 6 in the binary is Scale(1, atlas->m_Field150, 1) with m_Field150==1.0 --
    // a unit-Y no-op, so it is intentionally omitted here.
    MatrixStack& world = MatrixManager::GetInstance().GetWorldStack();
    world.Reset();
    world.TranslateLocal(alignOff);
    world.ScaleRows(scale.x, scale.y, 1.0f);
    world.RotZ(rotZ);
    world.Translate(anchor);
    MatrixManager::GetInstance().UploadModelViewOnly();

    for (size_t si = 0; si < m_Surfaces.size(); ++si) {
        BakedStringTTF_Surface* s = m_Surfaces[si];
        if (!s || !s->m_Verts || s->m_VertCount == 0) continue;
        if (s->m_DrawMode >= 0) continue;  // only single-buffer path

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, (GLuint)s->m_PageTextureID);
        glEnable(GL_TEXTURE_2D);
        // DIFFERS: v1.6.1 Mesh::DrawTriList @0x00240e34 -> port Renderer::DrawTriList
        //   (the only platform boundary). DrawTriList sets GL_MODULATE tex-env itself
        //   and consumes the baked local verts as-is; the world matrix does the
        //   transform the old CPU loop performed per vertex.
        Renderer::GetInstance()->DrawTriList(s->m_Verts, (int)s->m_VertCount);
    }
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
