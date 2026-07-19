#ifndef FN_ENGINE_RENDER_FONTCACHEOBJECTTTF_H
#define FN_ENGINE_RENDER_FONTCACHEOBJECTTTF_H

// Mortar::FontCacheObjectTTF — dynamic-TTF glyph cache (backend-neutral).
//
// Port specific: not a binary struct. The binary's IFont / IGlyphCache API
// (Samsung Bada framework) is replaced by this portable wrapper. The actual
// rasterizer (FreeType or stb_truetype) is selected at CMake configure time
// via FN_TTF_BACKEND and lives behind the Mortar::TtfFace seam (TtfBackend.h)
// -- this class only ever calls TtfFace's backend-neutral API, never FT/stb
// directly.
//
// Lifecycle:
//   1. Construct with a path to a .ttf file and a pixel size.
//   2. GetGlyph(cp, pixelSize) returns a GlyphAtlasEntry* (or nullptr if the
//      codepoint is not in the font), lazily rendering the glyph and packing it
//      into the shared FontInterface atlas.
//   3. GetKerningForPair(a, b) returns the kerning advance in pixels (via
//      TtfFace::GetKerning_26_6). Always returns 0.0f when the font has no
//      kern table -- matching the binary's GetKerning stub behaviour for
//      .fnt fonts.
//
// Thread safety: not thread-safe; single-threaded game loop is assumed.

#include "render/FontInterface.h"
#include <cstdint>
#include <map>

namespace Mortar {

class TtfFace;

// Port specific: HD font supersampling (binary bakes glyphs at device res; we oversample Nx for crisp upscaling).
// FreeType is asked to rasterize glyphs at (requestedSize * kFontSupersample); the resulting bitmap and atlas
// allocation are kFontSupersample x larger than the logical glyph. All metric fields returned to BakedStringBox
// (advanceX, bearingX/Y, width, height) are divided back by kFontSupersample so text layout is identical to N=1.
// The UV range in the atlas covers the full oversampled bitmap and maps to the logical-size quad, so the
// magnification ratio at the quad drops to 1/kFontSupersample => crisp text when the 480x320 logical viewport is
// scaled up to the display window.
//
// Gated on FN_ENABLE_HD_ASSETS (global add_compile_definitions, forced OFF on
// Wii -- see top-level CMakeLists.txt): HD builds oversample 3x; non-HD builds
// rasterize at 1x (native 480x320-equivalent glyphs). Every consumer
// (metrics, kerning, atlas margin, UV oversample) is parameterized on this
// constant, so ss=1 self-cancels -- layout is identical, only atlas texel
// density changes.
#if defined(FN_ENABLE_HD_ASSETS)
static const int kFontSupersample = 3;
#else
static const int kFontSupersample = 1;   // non-HD (Wii): native 480x320-equiv glyphs
#endif

// Key for the glyph cache: (codepoint, scaled_size_26.6, effect, radius).
// scaled_size_26.6 = trunc(requestedSize * globalSizeScale * fontScale * 64.0)
// Using the FT 26.6 fixed-point value as the key means two requests that
// produce the same FT_Set_Char_Size call share the same cache entry.
// effect/radius (#257): a BLUR-effect glyph is a SEPARATE rasterisation (padded +
// filtered, see GetGlyph), so it must not collide with the sharp (effect=NONE) entry
// for the same codepoint/size.
struct GlyphCacheKey {
    uint32_t codepoint;
    long     charHeight26_6; // FT_F26Dot6 value passed to FT_Set_Char_Size
    uint8_t  effect;         // FONT_EFFECT_ENUM value; 0 (NONE) for the plain sharp glyph
    uint8_t  radius;         // effect radius in LOGICAL (pre-supersample) px; 0 when effect==NONE

    bool operator<(const GlyphCacheKey& o) const {
        if (codepoint != o.codepoint) return codepoint < o.codepoint;
        if (charHeight26_6 != o.charHeight26_6) return charHeight26_6 < o.charHeight26_6;
        if (effect != o.effect) return effect < o.effect;
        return radius < o.radius;
    }
};

class FontCacheObjectTTF {
public:
    // FONT_EFFECT_ENUM -- values from binary.
    // ASM-spec v1.6.1 Mortar::RenderGlyph @0x0024f5dc: 0 NONE, 1 STROKE, 2 BLUR,
    // 3 INNER_GLOW, 4..11 BEVEL (bevel-style variants).
    // Nested per the binary's mangled name (Mortar::FontCacheObjectTTF::FONT_EFFECT_ENUM),
    // NOT a namespace-level Mortar::FONT_EFFECT.
    // NONE, BLUR (shadow, #257) and STROKE (glow/outline, #257 follow-up) are wired up.
    // STROKE rasterises via BuildStrokes (SDF outline, see FontCacheObjectTTF.cpp), reusing
    // the same pad-then-filter pattern as BLUR/BuildBlur. INNER_GLOW is RE'd but dead in
    // v1.6.1 (no call site writes its gate -- see BakedStringBox.cpp Draw() note); BEVEL
    // variants (4..11) have no port-side enumerator yet.
    enum FONT_EFFECT_ENUM {
        FONT_EFFECT_NONE       = 0,
        FONT_EFFECT_STROKE     = 1,
        FONT_EFFECT_BLUR       = 2,
        FONT_EFFECT_INNER_GLOW = 3
        // 4..11: BEVEL variants -- not ported, no port-side enumerator yet.
    };

    // Loads the TTF face from a file path.
    // pixelSize is the default pixel height; GetGlyph accepts per-call sizes.
    // Backend (FreeType or stb_truetype) is chosen at CMake configure time
    // via FN_TTF_BACKEND -- see TtfBackend.h.
    FontCacheObjectTTF(const char* path, int defaultPixelSize);
    ~FontCacheObjectTTF();

    bool IsValid() const;

    // Returns a cached GlyphAtlasEntry for (cp, requestedSize, effect, radius).
    // requestedSize is the pre-scale font size (e.g. 9.0f).
    //
    // Every glyph is rasterised as a PADDED CELL per the v1.6.1 baked-bearing
    // model (ASM-spec v1.6.1 Mortar::RenderGlyph @0x0024f5dc): the sharp bitmap
    // is blitted at (padL, padT) inside a cell padded symmetrically by-effect
    //   NONE/INNER_GLOW/default: padL=0, padT=1
    //   STROKE/BLUR:             padL=radius+1, padT=radius+2 (then filtered:
    //                            BuildBlur @0x0024f030 / BuildStrokes @0x0024edb8)
    //   BEVEL (4..11):           +4 to both pads (filter not ported)
    // radius is LOGICAL (pre-supersample) px. Ink-less glyphs (spaces) pack a
    // 1x1 transparent logical cell.
    //
    // The returned entry carries both metric contracts -- the baked-bearing cell
    // fields (cellU0..cellV1 / cellOrigin / layout / cellW/H / page) consumed by
    // the BakedStringTTF GlyphTTF pipeline, and the legacy separate-bearing
    // fields (u0..v1 / bearingX/Y / advanceX / width / height) consumed by
    // BakedStringBox and Font.cpp -- see GlyphAtlasEntry in FontInterface.h.
    // Returns nullptr if the codepoint is absent.
    const GlyphAtlasEntry* GetGlyph(uint32_t cp, float requestedSize,
                                     FONT_EFFECT_ENUM effect = FONT_EFFECT_NONE,
                                     int radius = 0);

    // Kerning advance in world units between codepoints a and b at requestedSize.
    // Returns 0.0f when no kern table is present (matches binary GetKerning stub).
    float GetKerningForPair(uint32_t a, uint32_t b, float requestedSize);

    // Access the underlying atlas for texture upload / bind.
    FontInterface* GetAtlas() { return m_Atlas; }

    int GetDefaultPixelSize() const { return m_DefaultPixelSize; }

    // Ascender, descender, and line-height in world units at requestedSize.
    // World unit = FT_metric_26.6 * (1/64) * m_InvFontScale.
    float GetAscender(float requestedSize);
    float GetDescender(float requestedSize);
    float GetLineHeight(float requestedSize);

private:
    TtfFace*    m_Face;            // owned; backend chosen at compile time (TtfBackend.h)
    int         m_DefaultPixelSize;
    long        m_CurrentCharHeight; // last charHeight_26_6 passed to m_Face->SetPixelSize

    FontInterface* m_Atlas;        // owned glyph atlas

    // Glyph cache: GlyphCacheKey -> GlyphAtlasEntry (metrics in world units)
    std::map<GlyphCacheKey, GlyphAtlasEntry> m_Cache;

    // Apply TtfFace::SetPixelSize for the given charHeight_26_6 value.
    // charHeight_26_6 = trunc(requestedSize * globalSizeScale * fontScale * 64.0).
    // Tracks m_CurrentCharHeight to avoid redundant backend calls.
    bool SetCharSize(long charHeight_26_6);
};

} // namespace Mortar

#endif // FN_ENGINE_RENDER_FONTCACHEOBJECTTTF_H
