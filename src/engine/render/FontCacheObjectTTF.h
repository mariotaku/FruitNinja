#ifndef FN_ENGINE_RENDER_FONTCACHEOBJECTTTF_H
#define FN_ENGINE_RENDER_FONTCACHEOBJECTTTF_H

// Mortar::FontCacheObjectTTF — FreeType-backed TTF glyph cache.
//
// Port specific: not a binary struct. The binary's IFont / IGlyphCache API
// (Samsung Bada framework) is replaced by this portable FreeType wrapper.
//
// Lifecycle:
//   1. Construct with a path to a .ttf file and a pixel size.
//   2. GetGlyph(cp, pixelSize) returns a GlyphAtlasEntry* (or nullptr if the
//      codepoint is not in the font), lazily rendering the glyph and packing it
//      into the shared FontInterface atlas.
//   3. GetKerningForPair(a, b) returns the kerning advance in pixels (FreeType
//      FT_Get_Kerning). Always returns 0.0f when the font has no kern table —
//      matching the binary's GetKerning stub behaviour for .fnt fonts.
//
// Thread safety: not thread-safe; single-threaded game loop is assumed.

#include "render/FontInterface.h"
#include <cstdint>
#include <map>

// Forward-declare FreeType types to avoid including ft2build.h in the header
// (which would force it into every translation unit that includes this header).
struct FT_LibraryRec_;
struct FT_FaceRec_;
typedef FT_LibraryRec_* FT_Library;
typedef FT_FaceRec_*    FT_Face;

namespace Mortar {

// Port specific: HD font supersampling (binary bakes glyphs at device res; we oversample Nx for crisp upscaling).
// FreeType is asked to rasterize glyphs at (requestedSize * kFontSupersample); the resulting bitmap and atlas
// allocation are kFontSupersample x larger than the logical glyph. All metric fields returned to BakedStringBox
// (advanceX, bearingX/Y, width, height) are divided back by kFontSupersample so text layout is identical to N=1.
// The UV range in the atlas covers the full oversampled bitmap and maps to the logical-size quad, so the
// magnification ratio at the quad drops to 1/kFontSupersample => crisp text when the 480x320 logical viewport is
// scaled up to the display window.
static const int kFontSupersample = 3;

// Key for the glyph cache: (codepoint, scaled_size_26.6).
// scaled_size_26.6 = trunc(requestedSize * globalSizeScale * fontScale * 64.0)
// Using the FT 26.6 fixed-point value as the key means two requests that
// produce the same FT_Set_Char_Size call share the same cache entry.
struct GlyphCacheKey {
    uint32_t codepoint;
    long     charHeight26_6; // FT_F26Dot6 value passed to FT_Set_Char_Size

    bool operator<(const GlyphCacheKey& o) const {
        if (codepoint != o.codepoint) return codepoint < o.codepoint;
        return charHeight26_6 < o.charHeight26_6;
    }
};

class FontCacheObjectTTF {
public:
    // Loads the TTF face from a file path.
    // pixelSize is the default pixel height; GetGlyph accepts per-call sizes.
    FontCacheObjectTTF(FT_Library ftLib, const char* path, int defaultPixelSize);
    ~FontCacheObjectTTF();

    bool IsValid() const { return m_Face != nullptr; }

    // Returns a cached GlyphAtlasEntry for (cp, requestedSize).
    // requestedSize is the pre-scale font size (e.g. 9.0f).
    // GlyphAtlasEntry metrics are in world units (FT 26.6 / 64 * invFontScale).
    // Returns nullptr if the codepoint is absent.
    const GlyphAtlasEntry* GetGlyph(uint32_t cp, float requestedSize);

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
    FT_Library  m_FTLib;           // shared FT_Library (not owned)
    FT_Face     m_Face;            // owned FT_Face
    int         m_DefaultPixelSize;
    long        m_CurrentCharHeight; // last charHeight_26_6 passed to FT_Set_Char_Size

    FontInterface* m_Atlas;        // owned glyph atlas

    // Glyph cache: GlyphCacheKey -> GlyphAtlasEntry (metrics in world units)
    std::map<GlyphCacheKey, GlyphAtlasEntry> m_Cache;

    // Apply FT_Set_Char_Size for the given charHeight_26_6 value.
    // charHeight_26_6 = trunc(requestedSize * globalSizeScale * fontScale * 64.0).
    // Tracks m_CurrentCharHeight to avoid redundant FT calls.
    bool SetCharSize(long charHeight_26_6);
};

} // namespace Mortar

#endif // FN_ENGINE_RENDER_FONTCACHEOBJECTTTF_H
