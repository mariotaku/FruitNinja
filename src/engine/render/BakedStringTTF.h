#ifndef FN_ENGINE_RENDER_BAKEDSTRINGTTF_H
#define FN_ENGINE_RENDER_BAKEDSTRINGTTF_H

// Mortar::BakedStringTTF — curved/arc TTF text baking for MenuButton labels.
//
// Binary: v1.6.1 Mortar::BakedStringTTF @0x00249a5c (ctor)
// sizeof 0x64 (=100; op_new(100) confirmed).
//
// Reuses:
//   FontCacheObjectTTF::GetGlyph    -- per-glyph atlas metrics
//   FontInterface::GetInstance()    -- single atlas GL texture
//   BakedStringBox Draw/BakeGradient pattern -- vertex emit + draw path
//
// New pieces vs BakedStringBox:
//   (a) Per-glyph rotation: Rotate2DVector(corner, m_RotAngle) on 4 quad corners
//   (b) ApplyFormatting_Circle: arc transform placing glyphs on a circle
//   (c) m_FmtCount/m_Flag effect params (bold/outline count, effect type)
//
// Cross-build portability: no lambdas, no auto, no range-for, no enum class.

#include "math/Vec2.h"
#include "math/Vec3.h"
#include "math/Colour.h"
#include "render/QUADCUSTOMVERTEX.h"
#include <vector>
#include <cstdint>

namespace Mortar {

class FontCacheObjectTTF;
struct GlyphAtlasEntry;

// FONT_EFFECT type -- values from binary (v1.6.1 BakedStringTTF ctor @0x00249a5c).
// 0 = no effect, 1 = bold/outline driven by m_FmtCount.
enum FONT_EFFECT {
    FONT_EFFECT_NONE     = 0,
    FONT_EFFECT_BOLD     = 1
};

// GlyphTTF -- per-baked-glyph state. sizeof 0x44 (=68).
// v1.6.1 Mortar::GlyphTTF @0x00248b28 (BuildGlyphs allocation site).
struct GlyphTTF {
    uint32_t    m_CharCode;     // +0x00
    float       m_FontSize;     // +0x04
    Vec2        m_GlyphScale;   // +0x08
    void*       m_SurfaceKey;   // +0x10 atlas-page key (Surface*); port: always FontInterface atlas
    float       m_UvU0;         // +0x14
    float       m_UvV0;         // +0x18
    float       m_UvV1;         // +0x1c
    float       m_UvU1;         // +0x20
    Vec2        m_QuadMin;      // +0x24 pen-space min x,y (set by ApplyFormatting_LeftJustify)
    Vec2        m_RotBasis;     // +0x2c rotation basis (unused in port single-surface path)
    Vec2        m_QuadSize;     // +0x34 w,h -- skip glyph if w<1 or h<1 (whitespace)
    float       m_RotAngle;     // +0x3c rotation angle in radians (set by ApplyFormatting_Circle)
    FontCacheObjectTTF* m_Font; // +0x40
};

#ifdef __bada__
static_assert(sizeof(GlyphTTF) == 0x44, "GlyphTTF sizeof mismatch");
#endif

// BakedStringTTF_Surface -- per-surface draw buffer. sizeof 0x48 (=72).
// v1.6.1 Mortar::BakedStringTTF_Surface @0x00248c14 (BuildSurfaces allocation site).
struct BakedStringTTF_Surface {
    void*             m_PageKey;      // +0x00 atlas page key (Surface*); port: FontInterface atlas
    QUADCUSTOMVERTEX* m_Verts;        // +0x04 heap buffer (m_VertCount * sizeof(QUADCUSTOMVERTEX))
    uint32_t          m_VertCount;    // +0x08 total verts (glyphs * 6)
    uint32_t          _pad0c;         // +0x0c
    uint32_t          _pad10;         // +0x10
    uint32_t          _pad14;         // +0x14
    uint32_t          _pad18;         // +0x18
    uint32_t          _pad1c;         // +0x1c
    uint32_t          _pad20;         // +0x20
    int               m_DrawMode;     // +0x24 <0 = single-buffer path (the port's path)
    uint32_t          _pad28;         // +0x28
    uint32_t          _pad2c;         // +0x2c
    uint32_t          _pad30;         // +0x30
    uint32_t          _pad34;         // +0x34
    uint32_t          m_PlatformColour; // +0x38 base colour packed BGRA
    GlyphTTF*         m_GlyphsBegin;  // +0x3c
    GlyphTTF*         m_GlyphsEnd;    // +0x40
    uint32_t          _pad44;         // +0x44
};

#ifdef __bada__
static_assert(sizeof(BakedStringTTF_Surface) == 0x48, "BakedStringTTF_Surface sizeof mismatch");
#endif

// BakedStringEffect -- gradient stop container embedded in BakedStringEffectBase.
// Occupies +0x10 within BakedStringEffectBase (i.e. at BakedStringTTF offsets 0x10..0x1f).
// v1.6.1 layout inferred from BakedStringTTF @0x00249a5c ctor (AddColour calls).
//
// Note: BakedStringTTF spec says "gradient cache (0x1c<-0x18 in ApplyGradient)" at
// global offsets 0x18/0x1c -- these ARE within m_Effect (+0x08 = m_Col1, +0x0c = m_Tc).
// The "save" in ApplyGradient_TopBottom (@0x0024863c) copies offset 0x1c <- 0x18 within
// BakedStringTTF, which is m_Effect.m_Tc <- *(float*)&m_Effect.m_Col1 (colour reused as float).
struct BakedStringEffect {
    Colour      m_Col0;         // +0x00 (BakedStringTTF +0x10) first stop colour
    float       m_T0;           // +0x04 (BakedStringTTF +0x14) first stop t (=0.0)
    Colour      m_Col1;         // +0x08 (BakedStringTTF +0x18) second stop colour / gradient cache 0
    float       m_Tc;           // +0x0c (BakedStringTTF +0x1c) second stop t   / gradient cache 1
};

// BakedStringEffectBase -- base object at +0x00 of BakedStringTTF. sizeof 0x38 (=56).
// v1.6.1 Mortar::BakedStringTTF @0x00249a5c
struct BakedStringEffectBase {
    // +0x00..+0x0f: glyph-space bounding box, written by UpdateBounds @0x00247ed0
    // and read by Draw @0x002497a8 for alignment offset.
    //   UpdateBounds inits {minX=999999, maxY=-big, maxX=-big, minY=999999} then
    //   folds each surface's [+0x28..+0x34] extents in; Draw computes
    //   width = maxX - minX, height = (minY - maxY)/2 - minY.
    float       m_BoundsMinX;   // +0x00 (Draw: -X for right/centre align)
    float       m_BoundsMaxY;   // +0x04
    float       m_BoundsMaxX;   // +0x08 (Draw: width = MaxX - MinX)
    float       m_BoundsMinY;   // +0x0c (Draw: height basis)

    // +0x10: gradient stop container (AddColour targets).
    // Spans +0x10..+0x1f (global BakedStringTTF offsets).
    BakedStringEffect m_Effect; // +0x10 (16 bytes, ends at +0x20)

    // +0x20: gradient-stop list "cap"/end-of-capacity pointer in the binary
    // (Mortar::GradientPoint*; binary BakedStringEffect at m_Base+0x10 keeps the
    // gradient stops as a 3-pointer vector at +0x18/+0x1c/+0x20). The port models
    // the stops inline in m_Effect instead, so this slot is layout-only here.
    uint32_t    m_GradientCap;  // +0x20 (GradientPoint* in binary; unused in port path)
    float       m_Radius;       // +0x24 -- stored by ApplyFormatting_Circle

    float       m_Weight;       // +0x28 signedWeight * fc[+0x10c][+0x10]
    uint32_t    m_FmtCount;     // +0x2c clamp [0,0x20]; bold/outline pass count
    uint8_t     m_Flag;         // +0x30 FONT_EFFECT (1 = bold)
    uint8_t     _pad31;         // +0x31
    uint8_t     _pad32;         // +0x32
    uint8_t     _pad33;         // +0x33
    // +0x34: BakedStringEffect::m_Field24 in the binary -- written 0 by the effect
    // ctor @0x00248448, never read on any path RE'd. Pure layout filler.
    uint32_t    m_reserved34;   // +0x34 // purpose unknown (binary m_Field24, write-only)
};

#ifdef __bada__
static_assert(sizeof(BakedStringEffectBase) == 0x38, "BakedStringEffectBase sizeof mismatch");
#endif

// BakedStringTTF -- arc-text class. sizeof 0x64 (=100).
// v1.6.1 Mortar::BakedStringTTF @0x00249a5c
class BakedStringTTF {
public:
    // ctor @0x00249a5c: (FontCacheObjectTTF* fc, const char* text, float fontScale,
    //   Colour col, long alignSigned, float, FONT_EFFECT eff)
    // op_new(100). weight = clamp(ceil(alignSigned*fc[0x10c][0x0c]),0,0x20);
    // if(eff==0 && n>0) eff=1; m_FmtCount=n; m_Flag=eff.
    // m_ScaledHeight = fontScale * atlas[+0x14]. AddColour(col, 0.0).
    // m_Weight = signedToFloat(alignSigned) * fc[+0x10c][+0x10].
    // strdup(text)->m_Text. FullInternalRebuild(). FontInterface::AddStringRef(this).
    BakedStringTTF(FontCacheObjectTTF* fc,
                   const char* text,
                   float fontScale,
                   Colour col,
                   long alignSigned,
                   float,
                   FONT_EFFECT eff);
    ~BakedStringTTF();

    // FullInternalRebuild @0x00249780: BuildGlyphs + ApplyFormatting_LeftJustify +
    //   BuildSurfaces + ApplyEffects (no-op stub).
    void FullInternalRebuild();

    // FitStringToWidth @0x00248734 (static): pen-advance width measure.
    // Returns the pen-advance total width of text at fontScale using fc.
    // +1.0 inter-glyph gap; whitespace/0x200b/0xa = break point.
    static float FitStringToWidth(FontCacheObjectTTF* fc, const char* text,
                                  float fontScale, float maxWidth, float* outWidth);

    // ApplyFormatting_Circle @0x00248dd0: place glyphs on a circular arc.
    // public sets m_Radius=radius then calls Internal @0x00248cc8.
    void ApplyFormatting_Circle(float radius);

    // ApplyGradient_TopBottom @0x0024863c: vertical top-to-bottom gradient.
    // public: save cache, AddColour(top,0.0), AddColour(bottom,1.0); then Internal.
    // Internal @0x00247c54: if(m_SurfacesBuilt) per-surface per-vertex Y-lerp.
    void ApplyGradient_TopBottom(Colour top, Colour bottom);

    // Draw @0x002497a8: render all surfaces via atlas + vertex colour.
    // (Vec3& anchor, Vec2 scale, float rotZ, uint align, Rect* clip)
    // if(!m_SurfacesBuilt) FullInternalRebuild(); if 0 glyphs return.
    // field_5e==1 (circle) skips align; else apply align bits 0-3.
    void Draw(const Vec3& anchor, Vec2 scale, float rotZ, uint32_t align);

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

    // BuildGlyphs @0x00248b28: DeleteGlyphs; Utf8StringIterator(m_Text);
    //   per cp: g=fc->GetGlyph(cp, m_ScaledHeight); GlyphTTF alloc; m_Glyphs.push_back.
    void BuildGlyphs();

    // ApplyFormatting_LeftJustify @0x00247874: pen-advance per-glyph position.
    //   +1.0 inter-glyph gap; sets field_60 = total advance.
    void ApplyFormatting_LeftJustify();

    // BuildSurfaces @0x00248c14: group glyphs -> one Surface per atlas page
    //   (port: ONE surface). FinishMesh builds 6-vert/glyph buffer with rotation + colour.
    void BuildSurfaces();

    // ApplyEffects @0x001049b0: tail-branch dispatch; no-op in this port pass.
    void ApplyEffects();

    // DeleteGlyphs: free m_Glyphs contents.
    void DeleteGlyphs();

    // DeleteSurfaces: free m_Surfaces contents (vertex buffers + structs).
    void DeleteSurfaces();

    // Internal of ApplyFormatting_Circle @0x00248cc8.
    void ApplyFormatting_Circle_Internal(float radius);

    // ApplyGradient_TopBottom (public, @0x0024863c) / _Internal (@0x00247c54):
    //   Sets a 2-stop top-to-bottom vertical colour gradient on all built surfaces.
    //   Contract: top and bottom colours are passed EXPLICITLY as parameters and are
    //   used directly for the lerp; the method does NOT read m_Base.m_Effect.m_Col0/1.
    //   _Internal scans the full Y-extent of all verts, then for each vertex:
    //     t = (yTop - y) / yRange; colour = lerp(top, bottom, t).
    //   Caller (ApplyGradient_TopBottom) additionally saves+restores gradient cache
    //   in m_Base.m_Effect (m_Col1->m_Tc) before calling AddColour to populate stops.
    // ASM-spec v1.6.1 BakedStringTTF::ApplyGradient_TopBottom_Internal @0x00247c54 /
    //   ApplyGradient_TopBottom @0x0024863c: top/bottom passed as explicit params.
    void ApplyGradient_TopBottom_Internal(Colour top, Colour bottom);

    // AddColour: store into m_Base.m_Effect colour stops.
    void AddColour(Colour col, float t);

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
