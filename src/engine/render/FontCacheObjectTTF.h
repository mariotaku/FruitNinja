#ifndef FN_ENGINE_RENDER_FONTCACHEOBJECTTTF_H
#define FN_ENGINE_RENDER_FONTCACHEOBJECTTTF_H

// Mortar::FontCacheObjectTTF — dynamic-TTF glyph cache (backend-neutral).
//
// Port specific: not a binary struct. The binary's IFont / IGlyphCache API
// (Samsung Bada framework) is replaced by this portable wrapper.
//
// FN_PREBAKED_FONTS OFF (host/web default): the actual rasterizer (FreeType
// or stb_truetype) is selected at CMake configure time via FN_TTF_BACKEND and
// lives behind the Mortar::TtfFace seam (TtfBackend.h) -- this class only
// ever calls TtfFace's backend-neutral API, never FT/stb directly. m_Face
// owns the open .ttf face.
//
// FN_PREBAKED_FONTS ON (Wii: always; host/web: opt-in, task #51/#52/#54):
// NO TtfFace / m_Face exists at all -- the constructor never opens a .ttf
// file, and neither stb_truetype nor FreeType are linked in. Every glyph AND
// every face-level metric (ascender/descender/lineHeight) comes from the
// offline-baked FNT3 atlas (BakedFontWii, tools/wii/bake-fonts.py) -- stb
// clipped CJK glyphs and broke Korean composition, so this is the SOLE glyph
// source whenever the option is on, same on host/web as on Wii (the baked
// glyph set is pruned -- #55 -- to cover every codepoint the game actually
// renders, so there is no coverage gap to fall back for):
//   - Plain (effect==NONE) glyphs: mapped straight onto a GlyphAtlasEntry
//     (TryBakedGlyph).
//   - Effect (BLUR/STROKE) glyphs: the effect filter (BuildBlur/BuildStrokes)
//     runs on the BAKED glyph's UN-TILED coverage (TryBakedEffectGlyph ->
//     BakedFontWii::GetGlyphCoverage), so the shadow/glow is computed at the
//     baked (correct) size and aligns with the NONE base layer.
//   - GetAscender/GetDescender/GetLineHeight read BakedFontWii's per-size FNT3
//     face metrics instead of TtfFace::GetXxx_26_6.
//   - GetKerningForPair always returns 0.0f (no live caller; the baked pen
//     model doesn't consume pair-kerning -- see its body comment).
// A baked MISS (glyph not in the size's baked subset, or the raw page can't
// be re-read) renders NO glyph (GetGlyph returns nullptr) -- there is no
// rasterizer left to fall back to (neither FreeType nor stb_truetype are
// compiled in when FN_PREBAKED_FONTS is ON, see src/engine/CMakeLists.txt);
// BakedFontWii::Lookup/GetGlyphCoverage LOG_WARN the coverage gap. The
// returned GlyphAtlasEntry shape is identical to the dynamic path either way,
// so BakedStringTTF / BakedStringBox / Font.cpp are untouched. The only
// difference between Wii and a non-Wii FN_PREBAKED_FONTS build is the atlas
// page container BakedFontWii loads (GX-tiled .gxtx vs. linear WebP-in-.tex)
// -- that split lives entirely inside BakedFontWii.cpp.
//
// Lifecycle:
//   1. Construct with a path to a .ttf file (ignored when FN_PREBAKED_FONTS)
//      and a pixel size.
//   2. GetGlyph(cp, pixelSize) returns a GlyphAtlasEntry* (or nullptr if the
//      codepoint is not in the font), lazily rendering the glyph and packing it
//      into the shared FontInterface atlas.
//   3. GetKerningForPair(a, b) returns the kerning advance in pixels (via
//      TtfFace::GetKerning_26_6 when FN_PREBAKED_FONTS is OFF). Always returns
//      0.0f when the font has no kern table -- matching the binary's
//      GetKerning stub behaviour for .fnt fonts.
//
// Thread safety: not thread-safe; single-threaded game loop is assumed.

#include "render/FontInterface.h"
#include <cstdint>
#include <map>

namespace Mortar {

#if defined(FN_PREBAKED_FONTS)
class BakedFontWii;   // Port specific: prebaked-atlas glyph store (task #51); SOLE glyph source when ON
#else
class TtfFace;        // dynamic FreeType/stb backend seam (TtfBackend.h) -- only when FN_PREBAKED_FONTS is OFF
#endif

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

    // Loads the TTF face from a file path (when FN_PREBAKED_FONTS is OFF).
    // pixelSize is the default pixel height; GetGlyph accepts per-call sizes.
    // Backend (FreeType or stb_truetype) is chosen at CMake configure time via
    // FN_TTF_BACKEND -- see TtfBackend.h. When FN_PREBAKED_FONTS is ON, `path`
    // is ignored (task #54): no runtime .ttf is ever opened; see the
    // class-level comment above.
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

#if defined(FRUIT_PLATFORM_WII)
    // Task #60: open/close a glyph run so every EFFECT glyph (BLUR/STROKE/etc,
    // never NONE -- NONE reads BakedFontWii's stable prebaked pages via
    // TryBakedGlyph, which never calls PackGlyphCell and cannot fragment) of a
    // single string lands on ONE atlas page. Call BeginGlyphRun once before
    // requesting a string's glyphs (e.g. BakedStringTTF::BuildGlyphs, which is
    // exactly one call = one string), and EndGlyphRun once after.
    //
    // `codepointCount` is the number of GetGlyph calls about to happen (the
    // string's codepoint count, effect-glyph ones only need counting -- NONE
    // calls in the same run are harmless no-ops for the reservation since they
    // never touch the pinned page). Computes a conservative worst-case per-cell
    // texel bound from GetLineHeight's raw supersampled metric (an upper bound
    // on any single glyph's ink extent for the active font) plus the effect's
    // pad, then forwards to FontInterface::BeginGlyphRun so the WHOLE run is
    // guaranteed to fit one page before any glyph of it is packed -- no rollback
    // needed. A no-op (does not open a run) if requestedSize/effect can't
    // produce a meaningful bound (e.g. FONT_EFFECT_NONE, m_Atlas null).
    void BeginGlyphRun(int codepointCount, float requestedSize,
                       FONT_EFFECT_ENUM effect, int radius);
    void EndGlyphRun();
#endif

private:
#if !defined(FN_PREBAKED_FONTS)
    TtfFace*    m_Face;            // owned; backend chosen at compile time (TtfBackend.h). Only when FN_PREBAKED_FONTS is OFF.
    long        m_CurrentCharHeight; // last charHeight_26_6 passed to m_Face->SetPixelSize
#endif
    int         m_DefaultPixelSize;

    FontInterface* m_Atlas;        // owned glyph atlas

    // Glyph cache: GlyphCacheKey -> GlyphAtlasEntry (metrics in world units)
    std::map<GlyphCacheKey, GlyphAtlasEntry> m_Cache;

#if defined(FN_PREBAKED_FONTS)
    // Port specific (task #51, extended #54): prebaked FreeType atlas store --
    // the SOLE glyph + face-metric source whenever this option is ON (Wii
    // always; host/web opt-in). No FreeType/stb fallback -- see GetGlyph.
    // Owns one FontAtlasPage per (size,page) so the baked GL texture flows
    // through the same GlyphAtlasEntry::page / pageTextureID binding the
    // dynamic atlas uses. Lazily allocated on first GetGlyph/GetAscender/etc
    // call; active-language-only.
    BakedFontWii* m_BakedWii;
    // Stable FontAtlasPage wrappers for baked GL textures, keyed by GL id, so
    // repeated lookups of the same page return the SAME page pointer (surface
    // grouping in BakedStringTTF keys off pointer identity).
    std::map<GLuint, FontAtlasPage*> m_BakedPages;
    FontAtlasPage* BakedPageFor(GLuint texId);
    // Fill `out` from a baked-store hit at requestedSize. Returns false on a
    // baked miss (caller then renders no glyph -- see GetGlyph). Scales the
    // baked pixel metrics (at the snapped native size) by requestedSize/nativeSize.
    bool TryBakedGlyph(uint32_t cp, float requestedSize, GlyphAtlasEntry* out);

    // Task #51 effect-layer path: build an effect (BLUR/STROKE) glyph from the
    // BAKED coverage. Un-tiles the baked glyph's coverage
    // (BakedFontWii::GetGlyphCoverage), pads it by-effect, runs the SAME
    // BuildBlur/BuildStrokes filter the non-Wii path uses, packs into the
    // dynamic atlas, and fills `out` with baked metrics (scaled by
    // requestedSize/nativeSize) plus the grown bearing shift. Returns false on
    // a baked miss (caller then renders no glyph). `radius` is LOGICAL px.
    bool TryBakedEffectGlyph(uint32_t cp, float requestedSize,
                             FONT_EFFECT_ENUM effect, int radius,
                             GlyphAtlasEntry* out);
#endif // FN_PREBAKED_FONTS

#if !defined(FN_PREBAKED_FONTS)
    // Apply TtfFace::SetPixelSize for the given charHeight_26_6 value.
    // charHeight_26_6 = trunc(requestedSize * globalSizeScale * fontScale * 64.0).
    // Tracks m_CurrentCharHeight to avoid redundant backend calls. Only when
    // FN_PREBAKED_FONTS is OFF: with it ON there is no TtfFace to set a pixel
    // size on.
    bool SetCharSize(long charHeight_26_6);
#endif
};

} // namespace Mortar

#endif // FN_ENGINE_RENDER_FONTCACHEOBJECTTTF_H
