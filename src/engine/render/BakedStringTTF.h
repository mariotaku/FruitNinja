#ifndef FN_ENGINE_RENDER_BAKEDSTRINGTTF_H
#define FN_ENGINE_RENDER_BAKEDSTRINGTTF_H

// Mortar::BakedStringTTF — curved/arc TTF text baking for MenuButton labels.
//
// Binary: v1.6.1 Mortar::BakedStringTTF @0x00249a5c (ctor)
// sizeof 0x64 (=100; op_new(100) confirmed).
//
// Pipeline (v1.6.1 baked-bearing model):
//   BuildGlyphs @0x00248b28        -- FetchGlyph per codepoint (single pass)
//   ApplyFormatting_LeftJustify @0x00247874 -- pen placement into m_RotBasis
//   BuildSurfaces @0x00248c14      -- group by atlas page, FinishMesh per surface
//   ApplyEffects @0x00249684       -- circle / gradient / alpha re-dispatch
// Bearing is BAKED into the atlas cell origin (GlyphTTF::m_QuadMin); there are
// no separate bearingX/bearingY fields anywhere in this pipeline.
//
// Cross-build portability: no lambdas, no auto, no range-for, no enum class.

#include "math/_Vector2.h"
#include "math/_Vector3.h"
#include "math/Colour.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "render/FontCacheObjectTTF.h"
#include "core/MortarTypes.h"
#include <vector>
#include <string>
#include <cstdint>

namespace Mortar {

struct GlyphAtlasEntry;
struct TextureAtlasPage;
typedef TextureAtlasPage FontAtlasPage;

// GlyphTTF -- per-baked-glyph state. sizeof 0x44 (=68).
// v1.6.1 Mortar::GlyphTTF; populated by FetchGlyph @0x0024fa24 from the atlas rec.
//
// BAKED-BEARING model: the binary never stores separate bearingX/bearingY --
// bearing is baked into the atlas cell origin (m_QuadMin). Layout metrics
// (m_GlyphScale) carry the pen step and the baseline-relative ink bottom.
struct GlyphTTF {
    uint32_t    m_CharCode;     // +0x00
    float       m_FontSize;     // +0x04
    _Vector2<float> m_GlyphScale;   // +0x08 LAYOUT METRICS: .x = floor(advance/64) - bitmap_left
                                //       (pen step, = GetKerning value); .y = (horiBearingY -
                                //       height)/64 (ink bottom, baseline-relative). Both * fontScale.
    FontAtlasPage* m_SurfaceKey; // +0x10 owning atlas page (binary: TextureAtlasPage* rec[0x40])
    float       m_UvU0;         // +0x14 cell UVs straight from the rec -- NO inset stored
    float       m_UvV0;         // +0x18 (the 1/512 inset is applied in FinishMesh)
    float       m_UvV1;         // +0x1c
    float       m_UvU1;         // +0x20
    _Vector2<float> m_QuadMin;      // +0x24 CELL ORIGIN = (padL, padT) * fontScale -- the baked
                                //       bearing pad (NOT the pen; set by FetchGlyph)
    _Vector2<float> m_RotBasis;     // +0x2c PEN (penX, penY) -- on-baseline placement, set by
                                //       ApplyFormatting_LeftJustify / _Circle_Internal
    _Vector2<float> m_QuadSize;     // +0x34 cell (w,h) * fontScale -- FinishMesh skips if w<1 or h<1
    float       m_RotAngle;     // +0x3c rotation angle in radians (set by ApplyFormatting_Circle)
    FontCacheObjectTTF* m_Font; // +0x40
};

#ifdef __bada__
static_assert(sizeof(GlyphTTF) == 0x44, "GlyphTTF sizeof mismatch");
static_assert(__builtin_offsetof(GlyphTTF, m_GlyphScale) == 0x08, "m_GlyphScale offset mismatch");
static_assert(__builtin_offsetof(GlyphTTF, m_SurfaceKey) == 0x10, "m_SurfaceKey offset mismatch");
static_assert(__builtin_offsetof(GlyphTTF, m_QuadMin)    == 0x24, "m_QuadMin offset mismatch");
static_assert(__builtin_offsetof(GlyphTTF, m_RotBasis)   == 0x2c, "m_RotBasis offset mismatch");
static_assert(__builtin_offsetof(GlyphTTF, m_QuadSize)   == 0x34, "m_QuadSize offset mismatch");
static_assert(__builtin_offsetof(GlyphTTF, m_Font)       == 0x40, "m_Font offset mismatch");
#endif

// FetchGlyph @0x0024fa24 -- returns a fully-populated, heap-allocated GlyphTTF
// for (cp, scaledHeight, radius, effect). Binary: hash -> TextureAtlas::FindItem;
// miss -> RenderGlyph; new GlyphTTF(0x44) filled from the atlas rec. Port: the
// hash/FindItem/RenderGlyph path is FontCacheObjectTTF::GetGlyph (the FreeType
// boundary, // DIFFERS there); this function does the rec -> GlyphTTF fill.
// Caller owns the returned GlyphTTF.
GlyphTTF* FetchGlyph(FontCacheObjectTTF* fc, float scaledHeight, uint32_t cp,
                     uint32_t radius, uint8_t effect);

// BakedStringTTF_Surface -- per-atlas-page draw buffer. sizeof 0x48 (=72).
// v1.6.1 Mortar::BakedStringTTF_Surface; allocated by FindOrCreateSurface @0x00248b9c.
// There is NO GL-texture-ID field -- the GL texture is resolved from m_PageKey
// at draw time (m_PageKey->m_TextureID).
struct BakedStringTTF_Surface {
    FontAtlasPage*    m_PageKey;      // +0x00 owning atlas page (binary: TextureAtlasPage*)
    QUADCUSTOMVERTEX* m_Verts;        // +0x04 heap buffer (m_VertCount * sizeof(QUADCUSTOMVERTEX))
    uint32_t          m_VertCount;    // +0x08 total verts (drawable glyphs * 6)
    uint32_t          _pad0c;         // +0x0c
    uint32_t          _pad10;         // +0x10
    uint32_t          _pad14;         // +0x14
    uint32_t          _pad18;         // +0x18
    uint32_t          _pad1c;         // +0x1c
    uint32_t          _pad20;         // +0x20
    int               m_DrawMode;     // +0x24 <0 = single-buffer path (the port's path)
    // Per-surface vertex bounds, written by UpdateBounds @0x00247dd4
    // (floor for mins, ceil for maxes; init +/-999999) and folded into the
    // owning BakedStringTTF's m_Base bounds by BakedStringTTF::UpdateBounds.
    long              m_BoundsMinX;   // +0x28
    long              m_BoundsMaxY;   // +0x2c
    long              m_BoundsMaxX;   // +0x30
    long              m_BoundsMinY;   // +0x34
    uint32_t          m_PlatformColour; // +0x38 base colour packed BGRA
    std::vector<GlyphTTF*> m_Glyphs;  // +0x3c begin/end/cap (12B on ARM32; NOT owned --
                                      //       glyphs belong to BakedStringTTF::m_Glyphs)

    // AddGlyph @0x00248718: push_back into m_Glyphs (+0x3c).
    void AddGlyph(GlyphTTF* g);

    // FinishMesh @0x002480a8: build this surface's 6-vert/glyph tri-list.
    // Per drawable glyph (skip if cell w<1 or h<1): quad = (w+1)x(h+1) with local
    // corners offset by -m_QuadMin (cell origin); each corner is rotated by
    // m_RotAngle then translated by m_RotBasis (pen). The 1/512 UV inset is
    // applied here (u0-=, v0+=, v1-=, u1+=). Winding: GLES uses the non-RT-flip
    // (ELSE) branch of the binary's FontInterface[0x14c] switch.
    // Called per surface by BakedStringTTF::BuildSurfaces @0x00248c14.
    void FinishMesh();

    // UpdateBounds @0x00247dd4: seed +/-999999, then per vert (stride 0x24,
    // x=v[0], y=v[1]) fold floor(x)/ceil(x)/ceil(y)/floor(y) into the bounds.
    void UpdateBounds();

    // Transform_GradientSplit @0x0024954c: geometric CSG split of every triangle
    // against the plane y=(rect.top+rect.bottom)*y (SplitMesh @0x0024940c /
    // SplitTri @0x00248dd8), inserting new verts exactly on the split line, then
    // flat-recolours the seam (edge-intersection) verts to c. All other verts keep
    // their pre-split colour (the TopBottom ramp laid down earlier); GL smooth-shades
    // between the flat seam row and the surrounding ramp verts, producing the
    // metallic gradient band without any port-side per-vertex lerp.
    // Reallocates m_Verts/m_VertCount in place (vertex count grows by the
    // straddling triangles' extra verts). Non-virtual -- does not change layout.
    // ASM-spec v1.6.1 BakedStringTTF_Surface::Transform_GradientSplit @0x0024954c.
    void Transform_GradientSplit(Colour c, float y, MortarRectangleT<long>& rect);

    // Transform_SetAlpha @0x00247cf0: overwrite (not multiply) the alpha byte of every
    // vertex's packed colour, RGB untouched. Dispatched by
    // BakedStringTTF::ApplyAlpha_Internal @0x00247d7c.
    // ASM-spec v1.6.1 BakedStringTTF_Surface::Transform_SetAlpha @0x00247cf0.
    void Transform_SetAlpha(uint8_t alpha);
};

#ifdef __bada__
static_assert(sizeof(BakedStringTTF_Surface) == 0x48, "BakedStringTTF_Surface sizeof mismatch");
static_assert(__builtin_offsetof(BakedStringTTF_Surface, m_BoundsMinX)     == 0x28, "surface bounds offset mismatch");
static_assert(__builtin_offsetof(BakedStringTTF_Surface, m_PlatformColour) == 0x38, "m_PlatformColour offset mismatch");
static_assert(__builtin_offsetof(BakedStringTTF_Surface, m_Glyphs)         == 0x3c, "m_Glyphs offset mismatch");
#endif

// GradientPoint -- one gradient stop, stored in the m_Base+0x18 vector.
// v1.6.1 Mortar::GradientPoint (8 bytes: u32 colour + float t).
struct GradientPoint {
    Colour      m_Colour;       // +0x00 packed BGRA byte colour
    float       m_T;            // +0x04 stop position (0.0 = top ... 1.0 = bottom;
                                //       split stops carry the split fraction)
};

#ifdef __bada__
static_assert(sizeof(GradientPoint) == 0x8, "GradientPoint sizeof mismatch");
#endif

// BakedStringEffectBase -- base object at +0x00 of BakedStringTTF. sizeof 0x38 (=56).
// v1.6.1 Mortar::BakedStringTTF @0x00249a5c
struct BakedStringEffectBase {
    // +0x00..+0x0f: glyph-space bounding box, written by UpdateBounds @0x00247ed0
    // and read by Draw @0x002497a8 for alignment offset.
    //   UpdateBounds inits {minX=999999, maxY=-big, maxX=-big, minY=999999} then
    //   folds each surface's [+0x28..+0x34] extents in; Draw computes
    //   width = maxX - minX, height = (minY - maxY)/2 - minY.
    // These are `long` in the binary (v1.6.1 BakedStringTTF::UpdateBounds @0x00247ed0
    // truncates vertex extents to integer). The layout matches MortarRectangleT<long>
    // (left/top/right/bottom) exactly, which is why GetRefRect can reinterpret `this`.
    long        m_BoundsMinX;   // +0x00 (MortarRectangleT<long>::left)
    long        m_BoundsMaxY;   // +0x04 (MortarRectangleT<long>::top)
    long        m_BoundsMaxX;   // +0x08 (MortarRectangleT<long>::right)
    long        m_BoundsMinY;   // +0x0c (MortarRectangleT<long>::bottom)

    // +0x10/+0x11: alpha override, read by ApplyEffects @0x00249684
    // (if m_Base[0x11] != 0 -> ApplyAlpha_Internal(m_Base[0x10])).
    // Set by the public setter BakedStringTTF::ApplyAlpha @0x00247dc4. DORMANT in
    // v1.6.1: BakedStringBox::SetAlpha (the only caller shape this mirrors) has no
    // v1.6.1 call sites, so no live path currently sets these -- ported faithfully
    // per stub-don't-skip, not dead code.
    uint8_t     m_Alpha;        // +0x10 alpha value passed to ApplyAlpha_Internal
    uint8_t     m_AlphaSet;     // +0x11 non-zero = alpha override active
    uint8_t     _pad12;         // +0x12
    uint8_t     _pad13;         // +0x13
    uint32_t    m_reserved14;   // +0x14 // purpose unknown (no RE'd read/write)

    // +0x18: gradient stops (AddColour push_back target). 3-pointer std::vector
    // at +0x18/+0x1c/+0x20 in the binary; the "0x1c <- 0x18" store in
    // ApplyGradient_TopBottom @0x0024863c is end = begin, i.e. clear().
    std::vector<GradientPoint> m_GradientStops; // +0x18 (12B on ARM32)

    float       m_Radius;       // +0x24 -- stored by ApplyFormatting_Circle; ApplyEffects
                                //         re-applies the circle layout when != 0
    float       m_Weight;       // +0x28 signedWeight * fc[+0x10c][+0x10]; added to the
                                //         per-glyph pen step in ApplyFormatting_LeftJustify
    uint32_t    m_FmtCount;     // +0x2c clamp [0,0x20]; effect radius passed to FetchGlyph
    uint8_t     m_Flag;         // +0x30 FONT_EFFECT_ENUM passed to FetchGlyph
    uint8_t     _pad31;         // +0x31
    uint8_t     _pad32;         // +0x32
    uint8_t     _pad33;         // +0x33
    // +0x34: BakedStringEffect::m_Field24 in the binary -- written 0 by the effect
    // ctor @0x00248448, never read on any path RE'd. Pure layout filler.
    uint32_t    m_reserved34;   // +0x34 // purpose unknown (binary m_Field24, write-only)
};

#ifdef __bada__
static_assert(sizeof(BakedStringEffectBase) == 0x38, "BakedStringEffectBase sizeof mismatch");
static_assert(__builtin_offsetof(BakedStringEffectBase, m_Alpha)         == 0x10, "m_Alpha offset mismatch");
static_assert(__builtin_offsetof(BakedStringEffectBase, m_GradientStops) == 0x18, "m_GradientStops offset mismatch");
static_assert(__builtin_offsetof(BakedStringEffectBase, m_Radius)        == 0x24, "m_Radius offset mismatch");
#endif

// BakedStringTTF -- arc-text class. sizeof 0x64 (=100).
// v1.6.1 Mortar::BakedStringTTF @0x00249a5c
class BakedStringTTF {
public:
    // ctor @0x00249a5c: (FontCacheObjectTTF* fc, const char* text, float fontScale,
    //   Colour col, long alignSigned, float effectSize, FONT_EFFECT_ENUM eff)
    // op_new(100). n = clamp(ceil(effectSize*fc[0x10c][0x0c]),0,0x20);  <- 6th float param
    // if(eff==0 && n>0) eff=1; m_FmtCount=n; m_Flag=eff.
    // m_ScaledHeight = fontScale * atlas[+0x14]. AddColour(col, 0.0).
    // m_Weight = signedToFloat(alignSigned) * fc[+0x10c][+0x10].  <- alignSigned drives weight
    // strdup(text)->m_Text. FullInternalRebuild(). FontInterface::AddStringRef(this).
    BakedStringTTF(FontCacheObjectTTF* fc,
                   const char* text,
                   float fontScale,
                   Colour col,
                   long alignSigned,
                   float effectSize,
                   FontCacheObjectTTF::FONT_EFFECT_ENUM eff);
    ~BakedStringTTF();

    // FullInternalRebuild @0x00249780: BuildGlyphs + ApplyFormatting_LeftJustify +
    //   BuildSurfaces + ApplyEffects (replays circle/gradient/alpha state).
    void FullInternalRebuild();

    // FitStringToWidth @0x00248734 (static): word-wrap line-breaker.
    // Mangled: ...FitStringToWidthEPNS_18FontCacheObjectTTFERSsS3_fliPfPb
    // ASM-spec v1.6.1 BakedStringTTF::FitStringToWidth @0x00248734:
    //   (FontCacheObjectTTF* fc, std::string& ioText, std::string& outRemainder,
    //    float fontSize, long weight, int maxWidth, float* outWidth, bool* outTruncated)
    // ioText is modified in-place to the head that fits within maxWidth;
    // outRemainder gets the overflow tail; outWidth gets the measured advance;
    // outTruncated is set when an unbreakable word overflows maxWidth.
    // Per glyph: total += GetKerning(g) + weight*m_FontScale + 1.0 (the only live
    // caller, BakedStringBox::FitStrings @0x00246800, always passes weight=0);
    // whitespace/0x200b/0xa = break point.
    static void FitStringToWidth(FontCacheObjectTTF* fc, std::string& ioText,
                                 std::string& outRemainder, float fontSize,
                                 long weight, int maxWidth,
                                 float* outWidth, bool* outTruncated);

    // ApplyFormatting_Circle @0x00248dd0: place glyphs on a circular arc.
    // public sets m_Radius=radius then calls Internal @0x00248cc8.
    void ApplyFormatting_Circle(float radius);

    // ApplyGradient_TopBottom @0x0024863c: vertical top-to-bottom gradient.
    // public: clear gradient stops (binary: end=begin store at +0x1c),
    // AddColour(top,0.0), AddColour(bottom,1.0); then Internal.
    // Internal @0x00247c54: if(m_SurfacesBuilt) per-surface per-vertex Y-lerp.
    void ApplyGradient_TopBottom(Colour top, Colour bottom);

    // Draw @0x002497a8: render all surfaces via atlas + vertex colour.
    // ASM-spec v1.6.1 BakedStringTTF::Draw @0x002497a8:
    //   (Vec3 anchor, Vec2 scale, float rotZ, ALIGNMENT_TYPE, MortarRectangleT<long>* refRect=nullptr)
    // if(!m_SurfacesBuilt) FullInternalRebuild(); if 0 glyphs return.
    // field_5e==1 (circle) skips align; else apply align bits 0-3.
    // When refRect is non-null, alignment bounds are read from refRect instead of
    // computed from this object's glyph verts (binary: FG-label bbox for glow/shadow layer
    // registration). Zero visual change for all existing callers that pass no refRect.
    void Draw(const _Vector3<float>& anchor, _Vector2<float> scale, float rotZ, ALIGNMENT_TYPE align,
              MortarRectangleT<long>* refRect = 0);

    // GetRefRect: the m_Base bounds (offset 0) ARE a MortarRectangleT<long>
    // (left=m_BoundsMinX, top=m_BoundsMaxY, right=m_BoundsMaxX, bottom=m_BoundsMinY).
    // FancyBakedString passes the main layer's refRect to every layer so glow/shadow
    // align to the FG-label bbox.
    // ASM-spec v1.6.1: FancyBakedString::Draw @0x0024b8e4 + ApplyGradientSplit_Internal @0x002495fc
    //   pass (MortarRectangleT<long>*)m_pMain; BakedStringTTF m_Base bounds live at offset 0.
    MortarRectangleT<long>* GetRefRect() { return reinterpret_cast<MortarRectangleT<long>*>(this); }

    // ApplyGradientSplit @0x00249bf4: AddColour(c,y) into the effect, then paint every
    // vertex whose Y is above the split plane (y*(m_BoundsMaxY+m_BoundsMinY)) with c.
    // Split math lifted from BakedStringBox metallic gradient (Transform_GradientSplit).
    // ASM-spec v1.6.1 BakedStringTTF::ApplyGradientSplit @0x00249bf4 / Transform_GradientSplit @0x0024954c.
    // public: AddColour(c,y) then ApplyGradientSplit_Internal(c,y).
    void ApplyGradientSplit(Colour c, float y);

    // ApplyAlpha @0x00247dc4: alpha-override setter. Writes m_Base.m_Alpha/m_AlphaSet
    // then repaints immediately via ApplyAlpha_Internal (not lazy -- ApplyEffects also
    // replays it on every FullInternalRebuild). DORMANT in v1.6.1 (no call sites; see
    // m_Alpha/m_AlphaSet field comment) -- ported per stub-don't-skip.
    // ASM-spec v1.6.1 BakedStringTTF::ApplyAlpha @0x00247dc4.
    void ApplyAlpha(uint8_t alpha);

    // Returns total advance (field_60) set by ApplyFormatting_LeftJustify.
    float GetTotalAdvance() const;

    // Returns number of baked glyphs (m_Glyphs.size()).
    int GetGlyphCount() const;

private:
    // +0x00: BakedStringEffectBase (56 bytes = 0x38)
    BakedStringEffectBase m_Base;

    // +0x38: m_ScaledHeight = requestedSize * atlas[+0x14]
    float   m_ScaledHeight;     // +0x38

    // +0x3c: m_pFontCache
    FontCacheObjectTTF* m_pFontCache; // +0x3c

    // +0x40: m_Glyphs vector<GlyphTTF*> (12B on ARM32)
    std::vector<GlyphTTF*> m_Glyphs; // +0x40

    // +0x4c: m_Surfaces vector<BakedStringTTF_Surface*> (12B on ARM32)
    std::vector<BakedStringTTF_Surface*> m_Surfaces; // +0x4c

    // +0x58: m_Text (strdup)
    char*   m_Text;             // +0x58

    // +0x5c: m_GlyphsBuilt
    bool    m_GlyphsBuilt;      // +0x5c

    // +0x5d: m_SurfacesBuilt
    bool    m_SurfacesBuilt;    // +0x5d

    // +0x5e: circle-layout flag. ApplyFormatting_LeftJustify @0x00247874 sets 0,
    // ApplyFormatting_Circle_Internal @0x00248cc8 sets 1; Draw @0x002497a8 skips
    // the alignment block when set.
    uint8_t m_CircleFlag;       // +0x5e

    // +0x5f: never written or read on any RE'd ctor/rebuild/draw path (ctor
    // @0x00249a5c leaves it; Init @0x002475e0 leaves it). Pure layout byte.
    // NOTE: the port's ctor inits this to 255; the binary does not -- harmless
    // since nothing reads it, but it is a port-side liberty (out of scope here).
    uint8_t m_reserved5f;       // +0x5f // purpose unknown (uninitialised in binary)

    // +0x60: total pen advance, set by ApplyFormatting_LeftJustify; used by
    // ApplyFormatting_Circle_Internal as the arc-centering half-width basis.
    float   m_TotalAdvance;     // +0x60

    // BuildGlyphs @0x00248b28: DeleteGlyphs; Utf8StringIterator(m_Text); per cp:
    //   g = FetchGlyph(m_pFontCache, m_ScaledHeight, cp, m_FmtCount, m_Flag);
    //   m_Glyphs.push_back(g). Single pass; FetchGlyph returns fully-populated glyphs.
    void BuildGlyphs();

    // ApplyFormatting_LeftJustify @0x00247874: pen placement into m_RotBasis.
    //   penX=0; per glyph: m_RotBasis=(penX, m_GlyphScale.y), m_RotAngle=0;
    //   pen step = GetKerning(g) + m_Weight + 1.0; last step goes to m_TotalAdvance.
    void ApplyFormatting_LeftJustify();

    // GetKerning: returns g->m_GlyphScale.x -- the baked pen step. IGNORES the
    // next-glyph argument (NOT a kern delta; no FreeType kerning in pen advance).
    // ASM-verified: 2026-07-09 v1.6.1 Mortar::GlyphTTF::GetKerning @ 0x0024ea78 (asm-inspector)
    // body: vldr s0,[r0,#0x8]; bx lr -- returns m_GlyphScale.x, ignores the pair arg.
    // (called from ApplyFormatting_LeftJustify @0x00247874 / FitStringToWidth @0x00248734).
    float GetKerning(GlyphTTF* g, uint32_t nextCp) const;

    // BuildSurfaces @0x00248c14: if(!m_GlyphsBuilt) BuildGlyphs; per glyph:
    //   FindOrCreateSurface(g->m_SurfaceKey)->AddGlyph(g). Then per surface:
    //   m_PlatformColour = PlatformColour(gradientStop0); surf->FinishMesh().
    //   Then UpdateBounds.
    void BuildSurfaces();

    // FindOrCreateSurface @0x00248b9c: linear-scan m_Surfaces for m_PageKey==page,
    // else new BakedStringTTF_Surface(0x48) + push_back.
    // Binary mangled: ...FindOrCreateSurfaceEPNS_16TextureAtlasPageE -- TextureAtlasPage*.
    BakedStringTTF_Surface* FindOrCreateSurface(TextureAtlasPage* page);

    // ApplyEffects @0x00249684: tail dispatch, replayed on every rebuild:
    //   1. m_Radius != 0        -> ApplyFormatting_Circle_Internal(m_Radius)
    //   2. stopCount > 1        -> ApplyGradient_TopBottom_Internal(stop0, stop1);
    //                              per stop i>=2: ApplyGradientSplit_Internal(col, t)
    //   3. m_AlphaSet != 0      -> ApplyAlpha_Internal(m_Alpha)
    void ApplyEffects();

    // ApplyAlpha_Internal @0x00247d7c: alpha-override repaint dispatched by ApplyEffects
    // (replay) and ApplyAlpha (immediate). If !m_SurfacesBuilt, no-op; else per surface
    // BakedStringTTF_Surface::Transform_SetAlpha(alpha).
    // ASM-spec v1.6.1 BakedStringTTF::ApplyAlpha_Internal @0x00247d7c.
    void ApplyAlpha_Internal(uint8_t alpha);

    // UpdateBounds @0x00247ed0: seed {minX=999999, maxY=-999999, maxX=-999999,
    // minY=999999}; per surface call BakedStringTTF_Surface::UpdateBounds @0x00247dd4
    // then fold the surface's +0x28..+0x34 extents into m_Base. Called at the tail
    // of BuildSurfaces (the binary's only caller). Populates the refRect the
    // FancyBakedString layers share.
    void UpdateBounds();

    // DeleteGlyphs: free m_Glyphs contents.
    void DeleteGlyphs();

    // DeleteSurfaces: free m_Surfaces contents (vertex buffers + structs).
    void DeleteSurfaces();

    // Internal of ApplyFormatting_Circle @0x00248cc8.
    void ApplyFormatting_Circle_Internal(float radius);

    // ApplyGradient_TopBottom (public, @0x0024863c) / _Internal (@0x00247c54):
    //   Sets a 2-stop top-to-bottom vertical colour gradient on all built surfaces.
    //   Contract: top and bottom colours are passed EXPLICITLY as parameters and are
    //   used directly for the lerp (the stop vector is only the record ApplyEffects
    //   replays on rebuild). _Internal scans the full Y-extent of all verts, then
    //   for each vertex: t = (yTop - y) / yRange; colour = lerp(top, bottom, t).
    //   The public wrapper clears the stop vector first (binary: end=begin store).
    // ASM-spec v1.6.1 BakedStringTTF::ApplyGradient_TopBottom_Internal @0x00247c54 /
    //   ApplyGradient_TopBottom @0x0024863c: top/bottom passed as explicit params.
    void ApplyGradient_TopBottom_Internal(Colour top, Colour bottom);

    // ApplyGradientSplit_Internal @0x002495fc: loop surfaces, dispatching each to
    // BakedStringTTF_Surface::Transform_GradientSplit with this object's refRect.
    void ApplyGradientSplit_Internal(Colour c, float y);

    // AddColour: push_back a GradientPoint{col, t} onto m_Base.m_GradientStops.
    void AddColour(Colour col, float t);

    // FancyBakedString reads m_SurfacesBuilt directly (FancyBakedString::Draw @0x0024b8e4
    // rebuilds the main layer before reading its refRect). Friend rather than a public
    // accessor keeps BakedStringTTF's symbol surface unchanged for existing callers.
    friend class FancyBakedString;

#ifdef __bada__
    // GCC 4.4 rejects offsetof on private members from namespace scope -- use the
    // canonical friend-struct layout-assert pattern (as BaseScreen/DojoScreen).
    friend struct BakedStringTTFLayoutAssert;
#endif
};

#ifdef __bada__
static_assert(sizeof(BakedStringTTF) == 0x64, "BakedStringTTF sizeof mismatch");
struct BakedStringTTFLayoutAssert {
static_assert(__builtin_offsetof(BakedStringTTF, m_pFontCache) == 0x3c, "m_pFontCache offset mismatch");
static_assert(__builtin_offsetof(BakedStringTTF, m_Glyphs) == 0x40, "m_Glyphs offset mismatch");
static_assert(__builtin_offsetof(BakedStringTTF, m_Text) == 0x58, "m_Text offset mismatch");
static_assert(__builtin_offsetof(BakedStringTTF, m_GlyphsBuilt) == 0x5c, "m_GlyphsBuilt offset mismatch");
static_assert(__builtin_offsetof(BakedStringTTF, m_SurfacesBuilt) == 0x5d, "m_SurfacesBuilt offset mismatch");
static_assert(__builtin_offsetof(BakedStringTTF, m_TotalAdvance) == 0x60, "m_TotalAdvance offset mismatch");
};
#endif

} // namespace Mortar

#endif // FN_ENGINE_RENDER_BAKEDSTRINGTTF_H
