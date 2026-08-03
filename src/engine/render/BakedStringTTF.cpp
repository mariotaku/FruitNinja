// BakedStringTTF — arc-text baking for MenuButton labels.
// v1.6.1 Mortar::BakedStringTTF @0x00249a5c
//
// v1.6.1 baked-bearing glyph model: FetchGlyph fills GlyphTTF straight from the
// atlas rec (bearing baked into the cell origin); ApplyFormatting_LeftJustify
// places the pen (m_RotBasis); FinishMesh emits the (cell+1)^2 quad around the
// cell origin. See BakedStringTTF.h for the struct-level contract.

#include "render/BakedStringTTF.h"
#include "render/FontCacheObjectTTF.h"
#include "render/FontInterface.h"
#include "render/Utf8StringIterator.h"
#include "render/MatrixManager.h"
#include "render/MatrixStack.h"
#include "render/Renderer.h"
#include "render/gl_funcs.h"
#include "math/Matrix44.h"
#include "math/_Vector2.h"
#include "math/_Vector3.h"
#include "math/Colour.h"
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>
#include "core/MortarTypes.h"

// Binary constants
// v1.6.1 Mortar::BakedStringTTF @0x00249a5c
static const float k_PI      = 3.14159265f;
static const float k_DEG2RAD = 0.017453292f;
static const float k_UvInset = 0.001953125f;  // 1/512 -- FinishMesh UV inset

// Winding selector for BakedStringTTF_Surface::FinishMesh.
//
// The binary reads it per glyph as FontInterface::GetInstance()[+0x14c] and picks
// one of two vertex-emission orders: ==1 emits BR,TL,BL,TR,TL,BR (counter-
// clockwise), anything else emits BR,BL,TL,TR,BR,TL (clockwise). The field is the
// integer half of a pair with +0x150 (=1.0f, the Y scale BakedStringTTF::Draw
// @0x002497a8 applies in its step 6): together they are the atlas Y orientation,
// and the winding has to flip with the Y scale to keep the triangle facing.
//
// v1.6.1 FontInterface::FontInterface @0x002502e0 stores 1 into +0x14c and NOTHING
// else in the binary writes it -- every FontInterface member was checked, plus a
// program-wide scan for `str r,[r,#0x14c]`. So it is a constant 1 for the whole
// process life and the counter-clockwise arm is the only one that ever runs.
//
// DIFFERS: original = FontInterface::GetInstance()[+0x14c] (v1.6.1
//   Mortar::BakedStringTTF_Surface::FinishMesh @0x002480a8), using a pinned 1
//   because the port's FontInterface is not a layout-faithful port of the binary
//   class -- it has neither the field nor a GetInstance() singleton. The value is
//   exact, not an approximation (see above), so this costs no fidelity.
// Winding itself is unobservable either way: Renderer::DrawTriList disables face
// culling for every 2D/text draw on both the GL and GX backends.
static const int k_FontInterfaceWinding = 1;

namespace Mortar {

// Rotate2DVector @0x00247f68: rotate v by angle (radians).
// A static member of BakedStringTTF_Surface in the binary, out-of-line, called 4x
// per glyph by FinishMesh (once per quad corner).
// ASM-spec v1.6.1 Mortar::BakedStringTTF_Surface::Rotate2DVector @0x00247f68:
//   cosf/sinf of the angle, then (c*v.x - s*v.y, c*v.y + s*v.x).
_Vector2<float> BakedStringTTF_Surface::Rotate2DVector(_Vector2<float> v, float angle)
{
    const float c = cosf(angle);
    const float s = sinf(angle);
    return _Vector2<float>(c * v.x - s * v.y, c * v.y + s * v.x);
}

// ClearVerts @0x00247774: release the baked vertex buffer.
// ASM-spec v1.6.1 Mortar::BakedStringTTF_Surface::ClearVerts @0x00247774:
//   if (m_Verts) { delete[] m_Verts; m_Verts = 0; } m_VertCount = 0;
void BakedStringTTF_Surface::ClearVerts()
{
    if (m_Verts) {
        delete[] m_Verts;
        m_Verts = 0;
    }
    m_VertCount = 0;
}

// FetchGlyph: return a fully-populated heap GlyphTTF for
// (cp, scaledHeight, radius, effect). Binary: hash -> TextureAtlas::FindItem;
// miss -> RenderGlyph; new GlyphTTF(0x44) filled from the rec. Port: the
// hash/render path is FontCacheObjectTTF::GetGlyph (FreeType boundary,
// // DIFFERS there); this does the rec -> GlyphTTF fill.
// ASM-verified: 2026-07-09 v1.6.1 Mortar::FontCacheObjectTTF::FetchGlyph @ 0x0024fa24 (asm-inspector)
GlyphTTF* FetchGlyph(FontCacheObjectTTF* fc, float scaledHeight, uint32_t cp,
                     uint32_t radius, uint8_t effect)
{
    GlyphTTF* g = new GlyphTTF();
    g->m_CharCode   = cp;
    g->m_FontSize   = scaledHeight;
    g->m_Font       = fc;
    g->m_GlyphScale = _Vector2<float>(0.0f, 0.0f);
    g->m_SurfaceKey = 0;
    g->m_UvU0 = g->m_UvV0 = g->m_UvU1 = g->m_UvV1 = 0.0f;
    g->m_QuadMin    = _Vector2<float>(0.0f, 0.0f);
    g->m_RotBasis   = _Vector2<float>(0.0f, 0.0f);
    g->m_QuadSize   = _Vector2<float>(0.0f, 0.0f);
    g->m_RotAngle   = 0.0f;

    const GlyphAtlasEntry* rec = fc
        ? fc->GetGlyph(cp, scaledHeight,
                       (FontCacheObjectTTF::FONT_EFFECT_ENUM)effect, (int)radius)
        : 0;
    if (rec) {
        g->m_GlyphScale = _Vector2<float>(rec->layoutX, rec->layoutY);
        g->m_SurfaceKey = rec->page;
        // Binary: rec[+0x08..+0x14] is a (u0, v0, u1, v1) quad copied straight into
        // GlyphTTF[+0x14..+0x20] in the same order.
        g->m_UvU0       = rec->cellU0;
        g->m_UvV0       = rec->cellV0;
        g->m_UvU1       = rec->cellU1;
        g->m_UvV1       = rec->cellV1;
        g->m_QuadMin    = _Vector2<float>(rec->cellOriginX, rec->cellOriginY);
        g->m_QuadSize   = _Vector2<float>(rec->cellW, rec->cellH);
    }
    return g;
}

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
    // m_Base contains a std::vector (gradient stops) -- init fields explicitly.
    m_Base.m_BoundsMinX = 0;
    m_Base.m_BoundsMaxY = 0;
    m_Base.m_BoundsMaxX = 0;
    m_Base.m_BoundsMinY = 0;
    m_Base.m_Alpha      = 0;
    m_Base.m_AlphaSet   = 0;
    m_Base._pad12       = 0;
    m_Base._pad13       = 0;
    m_Base.m_reserved14 = 0;
    m_Base.m_Radius     = 0.0f;
    m_Base.m_reserved34 = 0;

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
    // ASM-verified: v1.6.1 BakedStringTTF ctor @0x00249a5c + FontInterface::Initialize @0x0010e620:
    //  fc[+0x10c]=FontInterface*; +0x10=m_InvFontScale. Initialize(1.0,...) => m_InvFontScale==1.0
    //  for ALL langs, so m_Weight = alignSigned * 1.0 is EXACT (not an approximation).
    m_Base.m_Weight = (float)alignSigned * 1.0f;

    // m_ScaledHeight = fontScale * atlas->m_FontScale
    // v1.6.1 BakedStringTTF ctor @0x00249a5c: atlas[+0x14] = m_FontScale (=1.0 default)
    float atlasScale = 1.0f;
    if (fc) {
        FontInterface* atlas = fc->GetAtlas();
        if (atlas) atlasScale = atlas->m_FontScale;
    }
    m_ScaledHeight = fontScale * atlasScale;

    // AddColour(col, 0.0) -- push the base colour as gradient stop 0.
    AddColour(col, 0.0f);

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

// AddColour: append a gradient stop to the m_Base+0x18 stop vector.
// Stop 0 is the base colour (ctor); ApplyGradient_TopBottom clears + re-adds
// stops 0/1; ApplyGradientSplit appends split stops (i >= 2).
void BakedStringTTF::AddColour(Colour col, float t)
{
    GradientPoint p;
    p.m_Colour = col;
    p.m_T      = t;
    m_Base.m_GradientStops.push_back(p);
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
            s->ClearVerts();
            delete s;
        }
    }
    m_Surfaces.clear();
    m_SurfacesBuilt = false;
}

// BuildGlyphs @0x00248b28 (single pass):
// DeleteGlyphs; per codepoint: g = FetchGlyph(m_pFontCache, m_ScaledHeight, cp,
// m_FmtCount, m_Flag); m_Glyphs.push_back(g).
// ASM-spec v1.6.1 BakedStringTTF::BuildGlyphs @0x00248b28.
//
// Task #60 (Wii): one BuildGlyphs() call == one string. Bracket the codepoint
// loop in a FontCacheObjectTTF glyph run so every EFFECT glyph this string
// bakes (BLUR/STROKE/etc -- m_Base.m_Flag) lands on a single atlas page,
// never split mid-string across a page boundary. See FontCacheObjectTTF::
// BeginGlyphRun's header doc for the mechanism; a no-op for FONT_EFFECT_NONE
// strings (those never call PackGlyphCell at all -- see TryBakedGlyph).
void BakedStringTTF::BuildGlyphs()
{
    DeleteGlyphs();
    if (!m_pFontCache || !m_Text) return;

#if defined(FRUIT_PLATFORM_WII)
    int codepointCount = 0;
    for (Utf8StringIterator counter(m_Text); !counter.IsEmpty(); counter++) {
        ++codepointCount;
    }
    m_pFontCache->BeginGlyphRun(codepointCount, m_ScaledHeight,
        (FontCacheObjectTTF::FONT_EFFECT_ENUM)m_Base.m_Flag, (int)m_Base.m_FmtCount);
#endif

    Utf8StringIterator it(m_Text);
    while (!it.IsEmpty()) {
        GlyphTTF* g = FetchGlyph(m_pFontCache, m_ScaledHeight, it.m_CurrentCodepoint,
                                 m_Base.m_FmtCount, m_Base.m_Flag);
        m_Glyphs.push_back(g);
        it++;
    }

#if defined(FRUIT_PLATFORM_WII)
    m_pFontCache->EndGlyphRun();
#endif

    m_GlyphsBuilt = true;
}

// GetKerning: the baked pen step. Returns g->m_GlyphScale.x and IGNORES the
// next-glyph argument (NOT a kern delta; no FreeType kerning in pen advance).
// ASM-verified: 2026-07-09 v1.6.1 Mortar::GlyphTTF::GetKerning @ 0x0024ea78 (asm-inspector)
// body: vldr s0,[r0,#0x8]; bx lr -- returns m_GlyphScale.x, ignores the pair arg.
float BakedStringTTF::GetKerning(GlyphTTF* g, uint32_t /*nextCp -- ignored*/) const
{
    return g->m_GlyphScale.x;
}

// ApplyFormatting_LeftJustify @0x00247874: pen placement into m_RotBasis.
//   penX = 0; per glyph i: m_RotBasis = (penX, m_GlyphScale.y); m_RotAngle = 0;
//   step = GetKerning(g) + m_Weight + 1.0;
//   i < last: penX += step; i == last: m_TotalAdvance = penX + step.
// ASM-spec v1.6.1 BakedStringTTF::ApplyFormatting_LeftJustify @0x00247874.
void BakedStringTTF::ApplyFormatting_LeftJustify()
{
    m_CircleFlag = 0;

    float penX = 0.0f;
    const size_t n = m_Glyphs.size();
    for (size_t i = 0; i < n; ++i) {
        GlyphTTF* g = m_Glyphs[i];
        g->m_RotBasis = _Vector2<float>(penX, g->m_GlyphScale.y);
        g->m_RotAngle = 0.0f;

        uint32_t nextCp = (i + 1 < n) ? m_Glyphs[i + 1]->m_CharCode : 0;
        float step = GetKerning(g, nextCp) + m_Base.m_Weight + 1.0f;
        if (i + 1 < n) {
            penX += step;
        } else {
            m_TotalAdvance = penX + step;
        }
    }
}

// FindOrCreateSurface @0x00248b9c: linear-scan m_Surfaces for m_PageKey == page,
// else allocate a new surface (0x48) and push_back.
// ASM-spec v1.6.1 BakedStringTTF::FindOrCreateSurface @0x00248b9c.
// Binary mangled: ...FindOrCreateSurfaceEPNS_16TextureAtlasPageE -- TextureAtlasPage*.
BakedStringTTF_Surface* BakedStringTTF::FindOrCreateSurface(TextureAtlasPage* page)
{
    for (size_t i = 0; i < m_Surfaces.size(); ++i) {
        if (m_Surfaces[i]->m_PageKey == page) return m_Surfaces[i];
    }

    BakedStringTTF_Surface* surf = new BakedStringTTF_Surface();
    surf->m_PageKey        = page;
    surf->m_Verts          = 0;
    surf->m_VertCount      = 0;
    surf->_pad0c           = 0;
    surf->_pad10           = 0;
    surf->_pad14           = 0;
    surf->_pad18           = 0;
    surf->_pad1c           = 0;
    surf->_pad20           = 0;
    surf->m_DrawMode       = -1;   // single-buffer path
    surf->m_BoundsMinX     =  999999;
    surf->m_BoundsMaxY     = -999999;
    surf->m_BoundsMaxX     = -999999;
    surf->m_BoundsMinY     =  999999;
    surf->m_PlatformColour = 0xffffffffu;
    m_Surfaces.push_back(surf);
    return surf;
}

// AddGlyph @0x00248718: push_back into the surface glyph vector (+0x3c).
void BakedStringTTF_Surface::AddGlyph(GlyphTTF* g)
{
    m_Glyphs.push_back(g);
}

// BuildSurfaces @0x00248c14:
// if(!m_GlyphsBuilt) BuildGlyphs; if(m_SurfacesBuilt) DeleteSurfaces;
// m_SurfacesBuilt = true; per glyph: FindOrCreateSurface(g->m_SurfaceKey)
// ->AddGlyph(g). Then per surface: m_PlatformColour = PlatformColour(gradient
// stop 0); surf->FinishMesh(). Then UpdateBounds.
// ASM-spec v1.6.1 BakedStringTTF::BuildSurfaces @0x00248c14.
void BakedStringTTF::BuildSurfaces()
{
    if (!m_GlyphsBuilt) BuildGlyphs();

    // Binary order: the delete is gated on m_SurfacesBuilt and the flag is set
    // before the two loops, not after them. m_Surfaces is non-empty exactly when
    // m_SurfacesBuilt is set (DeleteSurfaces clears both), so the gate is exact.
    if (m_SurfacesBuilt) DeleteSurfaces();
    m_SurfacesBuilt = true;

    for (size_t i = 0; i < m_Glyphs.size(); ++i) {
        GlyphTTF* g = m_Glyphs[i];
        BakedStringTTF_Surface* surf = FindOrCreateSurface(g->m_SurfaceKey);
        surf->AddGlyph(g);
    }

    // Base colour = gradient stop 0 (the colour passed to the ctor's AddColour).
    const uint32_t packed = m_Base.m_GradientStops.empty()
        ? 0xffffffffu
        : m_Base.m_GradientStops[0].m_Colour.PlatformColour();

    for (size_t si = 0; si < m_Surfaces.size(); ++si) {
        BakedStringTTF_Surface* s = m_Surfaces[si];
        s->m_PlatformColour = packed;
        s->FinishMesh();
    }

    UpdateBounds();
}

// BakedStringTTF_Surface::FinishMesh @0x002480a8: build this surface's
// 6-vert/glyph tri-list.
//
// ClearVerts(); count the drawable glyphs (cell w>=1 AND h>=1); write
// m_VertCount = drawable*6 and allocate that many QUADCUSTOMVERTEX up front.
//
// Per drawable glyph the quad is (w+1) x (h+1). Local corners with (Ox,Oy) =
// m_QuadMin (cell origin, the baked bearing pad):
//   BL(-Ox,-Oy)  TL(-Ox, h+1-Oy)  BR(w+1-Ox, -Oy)  TR(w+1-Ox, h+1-Oy)
// Each corner = Rotate2DVector(corner, m_RotAngle) + m_RotBasis (pen). The binary
// makes the four calls in the order BR, TL, BL, TR.
// +Y is up; the pen (m_RotBasis.y = layout .y = ink bottom) anchors the quad
// bottom, so the top corners (larger y) sample the cell-top V (m_UvV0).
//
// UV inset applied HERE, as a uniform translate: both U -= 1/512, both V += 1/512.
//
// Winding: the binary keys two vertex orders on FontInterface[+0x14c]
// (k_FontInterfaceWinding above -- a constant 1 in v1.6.1):
//   ==1  BR, TL, BL, TR, TL, BR   -- tri0 (BR,TL,BL), tri1 (TR,TL,BR); CCW
//   else BR, BL, TL, TR, BR, TL   -- tri0 (BR,BL,TL), tri1 (TR,BR,TL); CW
// Both cover the same two triangles; only the facing differs, and face culling is
// off for every text draw, so this is an ordering match, not a visual one.
//
// A rolled 6-iteration pass then fills z=0, normal=(0,0,1) and the packed
// m_PlatformColour (+0x38) into all six verts -- every one of the 9 words of the
// 0x24-byte vertex is written, so the buffer needs no pre-zeroing.
//
// TAIL: m_Glyphs.clear() (binary: the `m_Glyphs.end = m_Glyphs.begin` store at
// +0x40 <- +0x3c). The surface's glyph list is non-owning scratch that only
// BuildSurfaces fills and only this function consumes; dropping it here is what
// makes a surface safe to re-mesh and stops it outliving the GlyphTTF objects
// BakedStringTTF::DeleteGlyphs frees.
//
// Tri-list consumed by Mesh::DrawTriList (port: Renderer::DrawTriList).
// ASM-spec v1.6.1 Mortar::BakedStringTTF_Surface::FinishMesh @0x002480a8.
void BakedStringTTF_Surface::FinishMesh()
{
    ClearVerts();

    uint32_t drawable = 0;
    for (size_t i = 0; i < m_Glyphs.size(); ++i) {
        GlyphTTF* g = m_Glyphs[i];
        if (g->m_QuadSize.x >= 1.0f && g->m_QuadSize.y >= 1.0f) drawable++;
    }

    m_VertCount = drawable * 6;
    m_Verts = new QUADCUSTOMVERTEX[m_VertCount];

    uint32_t quad = 0;
    for (size_t i = 0; i < m_Glyphs.size(); ++i) {
        GlyphTTF* g = m_Glyphs[i];
        const float w = g->m_QuadSize.x;
        const float h = g->m_QuadSize.y;
        if (w < 1.0f || h < 1.0f) continue;

        const float Ox    = g->m_QuadMin.x;
        const float Oy    = g->m_QuadMin.y;
        const float angle = g->m_RotAngle;
        const float penX  = g->m_RotBasis.x;
        const float penY  = g->m_RotBasis.y;

        // Binary call order: BR, TL, BL, TR.
        const _Vector2<float> rBR = Rotate2DVector(_Vector2<float>(w + 1.0f - Ox, -Oy), angle);
        const _Vector2<float> rTL = Rotate2DVector(_Vector2<float>(-Ox, h + 1.0f - Oy), angle);
        const _Vector2<float> rBL = Rotate2DVector(_Vector2<float>(-Ox, -Oy), angle);
        const _Vector2<float> rTR = Rotate2DVector(_Vector2<float>(w + 1.0f - Ox, h + 1.0f - Oy), angle);

        const float brX = penX + rBR.x, brY = penY + rBR.y;
        const float tlX = penX + rTL.x, tlY = penY + rTL.y;
        const float blX = penX + rBL.x, blY = penY + rBL.y;
        const float trX = penX + rTR.x, trY = penY + rTR.y;

        const float uL = g->m_UvU0 - k_UvInset;   // left   column U
        const float vT = g->m_UvV0 + k_UvInset;   // top    row    V
        const float uR = g->m_UvU1 - k_UvInset;   // right  column U
        const float vB = g->m_UvV1 + k_UvInset;   // bottom row    V

        QUADCUSTOMVERTEX* v = m_Verts + quad * 6;
        if (k_FontInterfaceWinding == 1) {
            v[0].x = brX; v[0].y = brY; v[0].u = uR; v[0].v = vB;  // BR
            v[1].x = tlX; v[1].y = tlY; v[1].u = uL; v[1].v = vT;  // TL
            v[2].x = blX; v[2].y = blY; v[2].u = uL; v[2].v = vB;  // BL
            v[3].x = trX; v[3].y = trY; v[3].u = uR; v[3].v = vT;  // TR
            v[4].x = tlX; v[4].y = tlY; v[4].u = uL; v[4].v = vT;  // TL
            v[5].x = brX; v[5].y = brY; v[5].u = uR; v[5].v = vB;  // BR
        } else {
            v[0].x = brX; v[0].y = brY; v[0].u = uR; v[0].v = vB;  // BR
            v[1].x = blX; v[1].y = blY; v[1].u = uL; v[1].v = vB;  // BL
            v[2].x = tlX; v[2].y = tlY; v[2].u = uL; v[2].v = vT;  // TL
            v[3].x = trX; v[3].y = trY; v[3].u = uR; v[3].v = vT;  // TR
            v[4].x = brX; v[4].y = brY; v[4].u = uR; v[4].v = vB;  // BR
            v[5].x = tlX; v[5].y = tlY; v[5].u = uL; v[5].v = vT;  // TL
        }

        for (int k = 0; k < 6; ++k) {
            v[k].colour = m_PlatformColour;
            v[k].z  = 0.0f;
            v[k].nx = 0.0f;
            v[k].ny = 0.0f;
            v[k].nz = 1.0f;
        }
        quad++;
    }

    // Binary tail: m_Glyphs.end = m_Glyphs.begin.
    m_Glyphs.clear();
}

// BakedStringTTF_Surface::UpdateBounds @0x00247dd4:
// Seed +/-999999, then per vert (x=v[0], y=v[1], stride 0x24) fold
// floor(x) into minX, ceil(x) into maxX, ceil(y) into maxY, floor(y) into minY.
// ASM-spec v1.6.1 BakedStringTTF_Surface::UpdateBounds @0x00247dd4.
void BakedStringTTF_Surface::UpdateBounds()
{
    m_BoundsMinX =  999999;
    m_BoundsMaxY = -999999;
    m_BoundsMaxX = -999999;
    m_BoundsMinY =  999999;

    if (!m_Verts) return;
    for (uint32_t vi = 0; vi < m_VertCount; ++vi) {
        const float x = m_Verts[vi].x;
        const float y = m_Verts[vi].y;
        const long fx = (long)floorf(x);
        const long cxl = (long)ceilf(x);
        const long fy = (long)floorf(y);
        const long cyl = (long)ceilf(y);
        if (fx  < m_BoundsMinX) m_BoundsMinX = fx;
        if (cxl > m_BoundsMaxX) m_BoundsMaxX = cxl;
        if (cyl > m_BoundsMaxY) m_BoundsMaxY = cyl;
        if (fy  < m_BoundsMinY) m_BoundsMinY = fy;
    }
}

// UpdateBounds @0x00247ed0:
// Seed {minX=999999, maxY=-999999, maxX=-999999, minY=999999}; per surface call
// BakedStringTTF_Surface::UpdateBounds @0x00247dd4 then fold the surface's
// +0x28..+0x34 extents into m_Base. Field mapping matches MortarRectangleT<long>:
//   m_BoundsMinX=left, m_BoundsMaxY=top, m_BoundsMaxX=right, m_BoundsMinY=bottom.
// ASM-spec v1.6.1 BakedStringTTF::UpdateBounds @0x00247ed0.
void BakedStringTTF::UpdateBounds()
{
    long minX =  999999;
    long maxY = -999999;
    long maxX = -999999;
    long minY =  999999;

    for (size_t si = 0; si < m_Surfaces.size(); ++si) {
        BakedStringTTF_Surface* s = m_Surfaces[si];
        if (!s) continue;
        s->UpdateBounds();
        // Empty surfaces keep their seed values; folding them is a no-op.
        if (s->m_BoundsMinX < minX) minX = s->m_BoundsMinX;
        if (s->m_BoundsMaxX > maxX) maxX = s->m_BoundsMaxX;
        if (s->m_BoundsMaxY > maxY) maxY = s->m_BoundsMaxY;
        if (s->m_BoundsMinY < minY) minY = s->m_BoundsMinY;
    }

    m_Base.m_BoundsMinX = minX;
    m_Base.m_BoundsMaxY = maxY;
    m_Base.m_BoundsMaxX = maxX;
    m_Base.m_BoundsMinY = minY;
}

// ApplyGradientSplit @0x00249bf4:
// AddColour(c, y) records the split stop, then the mesh is geometrically CSG-split
// against the plane y = (m_BoundsMaxY + m_BoundsMinY) * y (SplitMesh @0x0024940c /
// SplitTri @0x00248dd8) and only the kept (upper) side is repainted to c -- the band
// edge lands exactly on the split line rather than snapping to a tessellation row.
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

// PlaneDist @ inlined in SplitTri/SplitMesh -- signed distance of vertex v from
// the plane (N, d): N.v + d.
// ASM-spec v1.6.1 SplitMesh @0x0024940c / SplitTri @0x00248dd8.
static float PlaneDist(const float N[3], float d, const QUADCUSTOMVERTEX& v)
{
    return N[0] * v.x + N[1] * v.y + N[2] * v.z + d;
}

// Lerp3/Lerp2: plain component lerp, a + (b-a)*t. No binary-side helper is
// reused here (Vec2/Vec3 carry no Lerp method) -- inlined per spec fallback.
static void Lerp3(const float a[3], const float b[3], float t, float out[3])
{
    out[0] = a[0] + (b[0] - a[0]) * t;
    out[1] = a[1] + (b[1] - a[1]) * t;
    out[2] = a[2] + (b[2] - a[2]) * t;
}

static void Lerp2(const float a[2], const float b[2], float t, float out[2])
{
    out[0] = a[0] + (b[0] - a[0]) * t;
    out[1] = a[1] + (b[1] - a[1]) * t;
}

// LerpColourComponents: BGRA-unpack both colours to per-channel floats, lerp,
// repack. Used only when lone.colour != other.colour on a straddling edge --
// the packed-byte lerp the old port used loses precision and doesn't match
// the binary's per-channel float path (see Colour::Lerp for the analogous
// vertex-gradient case in ApplyGradient_TopBottom_Internal).
static uint32_t LerpColourComponents(uint32_t packedA, uint32_t packedB, float t)
{
    Colour a = *reinterpret_cast<const Colour*>(&packedA);
    Colour b = *reinterpret_cast<const Colour*>(&packedB);
    Colour out;
    out.r = (uint8_t)(a.r + ((float)b.r - (float)a.r) * t);
    out.g = (uint8_t)(a.g + ((float)b.g - (float)a.g) * t);
    out.b = (uint8_t)(a.b + ((float)b.b - (float)a.b) * t);
    out.a = (uint8_t)(a.a + ((float)b.a - (float)a.a) * t);
    return out.PlatformColour();
}

// SplitTri @0x00248dd8: geometric CSG split of one triangle (3 verts) against
// `plane` (N[0..2], d=plane[3]). WATERTIGHT -- every input triangle's full
// area is always represented in the output, split into an upper (dist>0)
// and lower (dist<=0) polygon. SplitMesh @0x0024940c always calls this with
// the cull bool (param_5) == false, so the early-return "drop far-side
// geometry" behaviour of an older port pass never fires in the binary; that
// pass's collectOneSide==true drops were the bug (shredded small text into
// scattered fragments). Backface culling is off for 2D text, so triangle
// winding is cosmetic -- only full-area coverage matters.
//
// Vertex selection: posCount = # verts with dist > 1e-07f.
//
// No-straddle (posCount==0 or 3): emit tri[0..2] verbatim as one triangle.
// No recolour indices are pushed -- these verts are untouched originals,
// not seam vertices.
//
// Straddle (posCount==1 or 2): split into upper/lower polygons covering the
// full original triangle area, duplicating the two edge-intersection verts
// (I_a, I_b) so upper and lower each get their own copy (required for a
// crisp colour edge -- a shared vertex would gradient-blend across the band).
//   posCount==1 (one vert upper="apex", two lower=o0,o1):
//     upper = 1 tri {apex, I_a, I_b}
//     lower = quad {I_a, o0, o1, I_b} as 2 tris: {I_a,o0,o1}, {I_a,o1,I_b}
//     -> 3 triangles / 9 verts total.
//   posCount==2 (two verts upper=u0,u1; one lower="apex"):
//     upper = quad {u0, u1, I_b, I_a} as 2 tris: {u0,u1,I_b}, {u0,I_b,I_a}
//     lower = 1 tri {apex, I_a, I_b}
//     -> 3 triangles / 9 verts total.
//   I_a = intersection on edge apex<->o0/u0, I_b = intersection on edge
//   apex<->o1/u1. t = distApex/(distApex-distOther) from the apex endpoint;
//   pos/uv interpolated, new vert normal=(0,0,1), colour lerped in
//   component space (LerpColourComponents) -- copied verbatim if endpoints
//   already match.
//
// ASM-spec v1.6.1 SplitTri @0x00248dd8: recolor list = seam (edge-intersection)
// verts only; outer verts keep the TopBottom ramp, GL interpolates -> gradient.
// Every pushed COPY of I_a/I_b (both the upper-side copies and the lower-side's
// own copies) is indexed into outIdx -- 5 pushes/indices per crossing triangle
// (3 in the posCount==1/Block-A arm's lower quad + 2 in the upper tri, or the
// mirrored count for posCount==2). Original (non-seam) triangle verts --
// apex/o0/o1/u0/u1 -- are never indexed, in either the straddle or no-straddle
// case: they keep whatever colour BuildSurfaces/ApplyGradient_TopBottom already
// wrote, and GL smooth-shades between them and the flat-recoloured seam row.
// ASM-spec v1.6.1 SplitMesh @0x0024940c / SplitTri @0x00248dd8 (watertight;
// cull bool always false, winding cosmetic -- 2D text, no backface cull).
static void SplitTri(const QUADCUSTOMVERTEX tri[3],
                      std::vector<QUADCUSTOMVERTEX>& out,
                      std::vector<int>* outIdx,
                      const float plane[4])
{
    const float* N = plane;
    float dist[3];
    int posCount = 0;
    for (int i = 0; i < 3; ++i) {
        dist[i] = PlaneDist(N, plane[3], tri[i]);
        if (dist[i] > 1e-07f) posCount++;
    }

    if (posCount == 0 || posCount == 3) {
        for (int i = 0; i < 3; ++i) {
            out.push_back(tri[i]);
        }
        return;
    }

    // Straddle. sign selector: s=+1 when posCount==1 (the pair of "other"
    // verts are non-positive), s=-1 when posCount==2 (the pair are positive).
    // Scan i=0..2 in order; the first vertex satisfying (s*dist <= s*1e-7)
    // is otherA, the second is otherB; the remaining vertex is the apex
    // ("lone" -- the minority-side vertex).
    float s = (posCount == 1) ? 1.0f : -1.0f;
    int otherA = -1, otherB = -1, loneIdx = -1;
    for (int i = 0; i < 3; ++i) {
        if (s * dist[i] <= s * 1e-07f) {
            if (otherA < 0) otherA = i; else otherB = i;
        } else {
            loneIdx = i;
        }
    }

    const QUADCUSTOMVERTEX& lone = tri[loneIdx];
    const QUADCUSTOMVERTEX& oA = tri[otherA];
    const QUADCUSTOMVERTEX& oB = tri[otherB];

    // t = distLone / (distLone - distOther).
    float t0 = dist[loneIdx] / (dist[loneIdx] - dist[otherA]);
    float t1 = dist[loneIdx] / (dist[loneIdx] - dist[otherB]);

    float lonePos[3] = { lone.x, lone.y, lone.z };
    float oAPos[3] = { oA.x, oA.y, oA.z };
    float oBPos[3] = { oB.x, oB.y, oB.z };
    float loneUv[2] = { lone.u, lone.v };
    float oAUv[2] = { oA.u, oA.v };
    float oBUv[2] = { oB.u, oB.v };

    // I_a: POS/UV lerp lone->otherA; colour lerp lone->otherA.
    QUADCUSTOMVERTEX iaBase = QUADCUSTOMVERTEX();
    {
        float outPos[3], outUv[2];
        Lerp3(lonePos, oAPos, t0, outPos);
        Lerp2(loneUv, oAUv, t0, outUv);
        iaBase.x = outPos[0]; iaBase.y = outPos[1]; iaBase.z = outPos[2];
        iaBase.u = outUv[0]; iaBase.v = outUv[1];
        iaBase.nx = 0.0f; iaBase.ny = 0.0f; iaBase.nz = 1.0f;
        iaBase.colour = (lone.colour == oA.colour) ? lone.colour
                                                    : LerpColourComponents(lone.colour, oA.colour, t0);
    }

    // I_b: POS/UV lerp lone->otherB; COLOUR lerp otherB->otherA (asymmetric,
    // matches the binary exactly).
    QUADCUSTOMVERTEX ibBase = QUADCUSTOMVERTEX();
    {
        float outPos[3], outUv[2];
        Lerp3(lonePos, oBPos, t1, outPos);
        Lerp2(loneUv, oBUv, t1, outUv);
        ibBase.x = outPos[0]; ibBase.y = outPos[1]; ibBase.z = outPos[2];
        ibBase.u = outUv[0]; ibBase.v = outUv[1];
        ibBase.nx = 0.0f; ibBase.ny = 0.0f; ibBase.nz = 1.0f;
        ibBase.colour = (oB.colour == oA.colour) ? oB.colour
                                                  : LerpColourComponents(oB.colour, oA.colour, t1);
    }

    if (posCount == 1) {
        // apex (lone) is the sole upper vert; o0=otherA, o1=otherB are lower.
        // Upper: 1 triangle {apex, I_a, I_b} -- apex is an original vert (not
        // indexed); I_a/I_b are seam copies (indexed).
        out.push_back(lone);
        out.push_back(iaBase);
        int iIaUp = (int)out.size() - 1;
        out.push_back(ibBase);
        int iIbUp = (int)out.size() - 1;
        if (outIdx) {
            outIdx->push_back(iIaUp);
            outIdx->push_back(iIbUp);
        }

        // Lower: quad {I_a, o0, o1, I_b} as 2 triangles, own copies of I_a/I_b
        // (seam -- indexed); o0/o1 are original verts (not indexed).
        out.push_back(iaBase);
        int iIaLo1 = (int)out.size() - 1;
        out.push_back(oA);
        out.push_back(oB);
        if (outIdx) {
            outIdx->push_back(iIaLo1);
        }

        out.push_back(iaBase);
        int iIaLo2 = (int)out.size() - 1;
        out.push_back(oB);
        out.push_back(ibBase);
        int iIbLo = (int)out.size() - 1;
        if (outIdx) {
            outIdx->push_back(iIaLo2);
            outIdx->push_back(iIbLo);
        }
        return;
    } else {
        // u0=otherA, u1=otherB are the two upper verts; apex (lone) is lower.
        // Upper: quad {u0, u1, I_b, I_a} as 2 triangles, own copies of I_a/I_b
        // (seam -- indexed); u0/u1 are original verts (not indexed).
        out.push_back(oA);
        out.push_back(oB);
        out.push_back(ibBase);
        int iIbUp = (int)out.size() - 1;
        if (outIdx) {
            outIdx->push_back(iIbUp);
        }

        out.push_back(oA);
        out.push_back(ibBase);
        int iIbUp2 = (int)out.size() - 1;
        out.push_back(iaBase);
        int iIaUp = (int)out.size() - 1;
        if (outIdx) {
            outIdx->push_back(iIbUp2);
            outIdx->push_back(iIaUp);
        }

        // Lower: 1 triangle {apex, I_a, I_b}, own copies. apex is an original
        // vert (not indexed); I_a/I_b are seam copies (indexed).
        out.push_back(lone);
        out.push_back(iaBase);
        int iIaLo = (int)out.size() - 1;
        out.push_back(ibBase);
        int iIbLo = (int)out.size() - 1;
        if (outIdx) {
            outIdx->push_back(iIaLo);
            outIdx->push_back(iIbLo);
        }
        return;
    }
}

// BakedStringTTF_Surface::Transform_GradientSplit @0x0024954c: geometric CSG
// split of every triangle against the horizontal plane y = (rect.top+rect.bottom)*y,
// then flat-recolours ONLY the seam (edge-intersection) verts created by the split
// to c. All other verts (the untouched triangle originals) keep whatever colour
// they already had (the TopBottom ramp, or a prior split's colour); GL smooth-shades
// between the flat seam row and those verts, which is what produces the visible
// gradient band -- there is no per-vertex lerp math in this function at all.
// ASM-spec v1.6.1 BakedStringTTF_Surface::Transform_GradientSplit @0x0024954c /
//   SplitMesh @0x0024940c / SplitTri @0x00248dd8.
void BakedStringTTF_Surface::Transform_GradientSplit(Colour c, float y, MortarRectangleT<long>& rect)
{
    uint32_t packed = c.PlatformColour();

    // plane.N = (0,1,0); plane.d = -(top+bottom)*y, so dist>0 (kept/upper side)
    // means vertY > (top+bottom)*y -- same split line as the old threshold test.
    float plane[4];
    plane[0] = 0.0f;
    plane[1] = 1.0f;
    plane[2] = 0.0f;
    plane[3] = -(float)(rect.top + rect.bottom) * y;

    std::vector<int> idx;
    std::vector<QUADCUSTOMVERTEX> newVerts;
    newVerts.reserve(m_VertCount);

    for (uint32_t tri = 0; tri + 3 <= m_VertCount; tri += 3) {
        SplitTri(&m_Verts[tri], newVerts, &idx, plane);
    }

    delete[] m_Verts;
    m_VertCount = (uint32_t)newVerts.size();
    m_Verts = new QUADCUSTOMVERTEX[m_VertCount];
    if (m_VertCount > 0) {
        memcpy(m_Verts, &newVerts[0], m_VertCount * sizeof(QUADCUSTOMVERTEX));
    }

    for (size_t k = 0; k < idx.size(); ++k) {
        m_Verts[idx[k]].colour = packed;
    }
}

// ApplyEffects @0x00249684: tail dispatch, replayed on every rebuild:
//   1. m_Radius != 0   -> ApplyFormatting_Circle_Internal(m_Radius)
//   2. stopCount > 1   -> ApplyGradient_TopBottom_Internal(stop0.col, stop1.col);
//                         per stop i>=2: ApplyGradientSplit_Internal(col, t)
//   3. m_AlphaSet != 0 -> ApplyAlpha_Internal(m_Alpha)
// ASM-spec v1.6.1 BakedStringTTF::ApplyEffects @0x00249684.
void BakedStringTTF::ApplyEffects()
{
    if (m_Base.m_Radius != 0.0f) {
        ApplyFormatting_Circle_Internal(m_Base.m_Radius);
    }

    const size_t n = m_Base.m_GradientStops.size();
    if (n > 1) {
        ApplyGradient_TopBottom_Internal(m_Base.m_GradientStops[0].m_Colour,
                                         m_Base.m_GradientStops[1].m_Colour);
        for (size_t i = 2; i < n; ++i) {
            ApplyGradientSplit_Internal(m_Base.m_GradientStops[i].m_Colour,
                                        m_Base.m_GradientStops[i].m_T);
        }
    }

    if (m_Base.m_AlphaSet != 0) {
        ApplyAlpha_Internal(m_Base.m_Alpha);
    }
}

// ApplyAlpha_Internal @0x00247d7c: alpha-override repaint dispatched by ApplyEffects
// (replay on rebuild) and ApplyAlpha (immediate). DORMANT in v1.6.1 -- no live call
// sites set m_AlphaSet -- ported faithfully per stub-don't-skip.
// ASM-spec v1.6.1 BakedStringTTF::ApplyAlpha_Internal @0x00247d7c.
void BakedStringTTF::ApplyAlpha_Internal(uint8_t alpha)
{
    if (!m_SurfacesBuilt) return;
    for (size_t si = 0; si < m_Surfaces.size(); ++si) {
        BakedStringTTF_Surface* s = m_Surfaces[si];
        if (!s) continue;
        s->Transform_SetAlpha(alpha);
    }
}

// BakedStringTTF_Surface::Transform_SetAlpha @0x00247cf0: overwrite (not multiply)
// the alpha byte of every vertex's packed colour; RGB untouched.
// ASM-spec v1.6.1 BakedStringTTF_Surface::Transform_SetAlpha @0x00247cf0.
void BakedStringTTF_Surface::Transform_SetAlpha(uint8_t alpha)
{
    if (!m_Verts) return;
    uint32_t alphaBits = (uint32_t)alpha << 24;
    for (uint32_t vi = 0; vi < m_VertCount; ++vi) {
        m_Verts[vi].colour = (m_Verts[vi].colour & 0x00FFFFFFu) | alphaBits;
    }
}

// ApplyAlpha @0x00247dc4: public alpha-override setter. Writes m_Base.m_Alpha/
// m_AlphaSet then repaints immediately (not lazy -- ApplyEffects also replays this
// on every FullInternalRebuild). DORMANT in v1.6.1 -- ported per stub-don't-skip.
// ASM-spec v1.6.1 BakedStringTTF::ApplyAlpha @0x00247dc4.
void BakedStringTTF::ApplyAlpha(uint8_t alpha)
{
    m_Base.m_Alpha = alpha;
    m_Base.m_AlphaSet = 1;
    ApplyAlpha_Internal(alpha);
}

// FullInternalRebuild @0x00249780:
// 1. BuildGlyphs
// 2. ApplyFormatting_LeftJustify
// 3. BuildSurfaces
// 4. ApplyEffects (replays circle layout / gradient stops / alpha override)
void BakedStringTTF::FullInternalRebuild()
{
    BuildGlyphs();
    ApplyFormatting_LeftJustify();
    BuildSurfaces();
    ApplyEffects();
}

// FitStringToWidth @0x00248734 (static):
// Mangled: ...FitStringToWidthEPNS_18FontCacheObjectTTFERSsS3_fliPfPb
// ASM-spec v1.6.1 BakedStringTTF::FitStringToWidth @0x00248734:
//   (FontCacheObjectTTF* fc, std::string& ioText, std::string& outRemainder,
//    float fontSize, long weight, int maxWidth, float* outWidth, bool* outTruncated)
// Word-wrap line-breaker: modifies ioText in-place to the head that fits within maxWidth,
// sets outRemainder to the overflow tail, outWidth to the measured advance of the head,
// and outTruncated when an unbreakable word overflows.
// Per-glyph accumulation mirrors ApplyFormatting_LeftJustify's pen step:
// total += GetKerning(g) [=layoutX] + weight*fc[+0x10c][+0x10] + 1.0. The only live
// caller (BakedStringBox::FitStrings @0x00246800) always passes weight=0, so the
// term is inert there but ported for fidelity.
// whitespace/0x200b/0x0a = break point.
void BakedStringTTF::FitStringToWidth(FontCacheObjectTTF* fc, std::string& ioText,
                                       std::string& outRemainder, float fontSize,
                                       long weight, int maxWidth,
                                       float* outWidth, bool* outTruncated)
{
    outRemainder.clear();
    if (outTruncated) *outTruncated = false;

    if (!fc || ioText.empty()) {
        if (outWidth) *outWidth = 0.0f;
        return;
    }

    // ASM-verified: v1.6.1 BakedStringTTF ctor @0x00249a5c + FontInterface::Initialize @0x0010e620:
    //  fc[+0x10c]=FontInterface*; +0x10=m_InvFontScale. Initialize(1.0,...) => m_InvFontScale==1.0
    //  for ALL langs, so m_Weight = alignSigned * 1.0 is EXACT (not an approximation).
    // Same fc[+0x10c][+0x10] scale as the ctor's m_Weight computation.
    float scaleB = 1.0f;
    const float weightTerm = (float)weight * scaleB;

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
        float adv = g ? g->layoutX : 0.0f;
        total += adv + weightTerm + 1.0f;   // binary: kern + weight*fc[+0x10c][+0x10] + 1.0f
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
// half = degPerUnit * m_TotalAdvance * 0.5;
// per glyph (pen = m_RotBasis after LeftJustify):
//   ang = PI/2 - (degPerUnit*pen.x - half) * DEG2RAD;
//   r   = radius + pen.y;
//   m_RotBasis = (cos(ang)*r, sin(ang)*r);
//   m_RotAngle = (PI/2 - (degPerUnit*(pen.x + m_GlyphScale.x*0.5) - half)*DEG2RAD) - PI/2;
// BuildSurfaces();
// ASM-spec v1.6.1 BakedStringTTF::ApplyFormatting_Circle_Internal @0x00248cc8
// (mid-advance rot term re-derived under the baked-bearing model: the old
//  separate-bearing spec "(x0 + adv*0.5) - bearingX" collapses to
//  penX + m_GlyphScale.x*0.5 when the pen carries no bearing).
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
        const float penX = g->m_RotBasis.x;
        const float penY = g->m_RotBasis.y;

        float ang = k_PI * 0.5f - (degPerUnit * penX - half) * k_DEG2RAD;
        float r   = radius + penY;

        g->m_RotBasis = _Vector2<float>(cosf(ang) * r, sinf(ang) * r);

        float midAdv = penX + g->m_GlyphScale.x * 0.5f;
        g->m_RotAngle = (k_PI * 0.5f - (degPerUnit * midAdv - half) * k_DEG2RAD) - k_PI * 0.5f;
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

    // Gradient stops from explicit params (not the stop vector, which the public
    // caller has already repopulated).
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
// public: clear the gradient-stop vector (binary: the "+0x1c <- +0x18" store is
// vector end = begin, i.e. clear() of the POD stops), then AddColour(top,0.0),
// AddColour(bottom,1.0), then Internal.
void BakedStringTTF::ApplyGradient_TopBottom(Colour top, Colour bottom)
{
    m_Base.m_GradientStops.clear();

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
//   M = T(anchor) * RotZ(rotZ) * Scale(scale) * TranslateLocal(alignOff)
// which is algebraically identical to the previous CPU loop
//   v' = R(rotZ) * S(scale) * (v + alignOff) + anchor
// so the on-screen text is unchanged; GL consumes the baked local verts directly.
//
// if(!m_SurfacesBuilt) FullInternalRebuild(); if 0 glyphs return.
// FontInterface::BuildPendingTextures(); world.Reset().
// Alignment (skipped when field_5e/m_CircleFlag != 0): bits0-1 horiz, bits2-3 vert.
//   refRect defaults to this (GetRefRect) -- the m_Base bounds aliased as a
//   MortarRectangleT<long>; bounds are read as long, per the binary.
// The per-surface GL texture is resolved from m_PageKey at draw time (the binary
// surface stores no texture ID -- TextureAtlasPage* only).
void BakedStringTTF::Draw(const _Vector3<float>& anchor, _Vector2<float> scale, float rotZ, ALIGNMENT_TYPE align,
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
    _Vector3<float> alignOff(0.0f, 0.0f, 0.0f);

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
    //   1 Reset, 2 TranslateLocal(alignOff), 3 Scale(scale) [row/left scale, S*M],
    //   4 RotZ(rotZ), 5 Translate(anchor) [applies anchor.z], 6 OMITTED.
    // step 6 in the binary is Scale(1, atlas->m_Field150, 1) with m_Field150==1.0 --
    // a unit-Y no-op, so it is intentionally omitted here.
    MatrixStack& world = MatrixManager::GetInstance().GetWorldStack();
    world.Reset();
    world.TranslateLocal(alignOff);
    world.Scale(_Vector3<float>(scale.x, scale.y, 1.0f));
    world.RotZ(rotZ);
    world.Translate(anchor);
    MatrixManager::GetInstance().UploadModelViewOnly();

    for (size_t si = 0; si < m_Surfaces.size(); ++si) {
        BakedStringTTF_Surface* s = m_Surfaces[si];
        if (!s || !s->m_Verts || s->m_VertCount == 0) continue;
        if (s->m_DrawMode >= 0) continue;  // only single-buffer path

        FontAtlasPage* page = s->m_PageKey;
        if (!page || !page->m_TextureID) continue;

        Renderer* renderer = Renderer::GetInstance();
        renderer->BindTexture2D(page->m_TextureID);
        // DIFFERS: v1.6.1 Mesh::DrawTriList @0x00240e34 -> port Renderer::DrawTriList
        //   (the only platform boundary). DrawTriList sets GL_MODULATE tex-env itself
        //   and consumes the baked local verts as-is; the world matrix does the
        //   transform the old CPU loop performed per vertex.
        renderer->DrawTriList(s->m_Verts, (int)s->m_VertCount);
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
