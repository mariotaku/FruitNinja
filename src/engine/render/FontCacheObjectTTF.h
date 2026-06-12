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

// Key for the glyph cache: (codepoint, pixel_size).
struct GlyphCacheKey {
    uint32_t codepoint;
    int      pixelSize;

    bool operator<(const GlyphCacheKey& o) const {
        if (codepoint != o.codepoint) return codepoint < o.codepoint;
        return pixelSize < o.pixelSize;
    }
};

class FontCacheObjectTTF {
public:
    // Loads the TTF face from a file path.
    // pixelSize is the default pixel height; GetGlyph accepts per-call sizes.
    FontCacheObjectTTF(FT_Library ftLib, const char* path, int defaultPixelSize);
    ~FontCacheObjectTTF();

    bool IsValid() const { return m_Face != nullptr; }

    // Returns a cached GlyphAtlasEntry for (cp, pixelSize), rendering and
    // packing it on first request. Returns nullptr if the codepoint is absent.
    const GlyphAtlasEntry* GetGlyph(uint32_t cp, int pixelSize);

    // Kerning advance in pixels between codepoints a and b at pixelSize.
    // Returns 0.0f when no kern table is present (matches binary GetKerning stub).
    float GetKerningForPair(uint32_t a, uint32_t b, int pixelSize);

    // Access the underlying atlas for texture upload / bind.
    FontInterface* GetAtlas() { return m_Atlas; }

    int GetDefaultPixelSize() const { return m_DefaultPixelSize; }

    // Ascender + descender in pixels at pixelSize (from FreeType face metrics).
    int GetAscender(int pixelSize);
    int GetDescender(int pixelSize);
    int GetLineHeight(int pixelSize);

private:
    FT_Library  m_FTLib;           // shared FT_Library (not owned)
    FT_Face     m_Face;            // owned FT_Face
    int         m_DefaultPixelSize;
    int         m_CurrentSize;     // last size passed to FT_Set_Pixel_Sizes

    FontInterface* m_Atlas;        // owned glyph atlas

    // Glyph cache: GlyphCacheKey -> GlyphAtlasEntry
    std::map<GlyphCacheKey, GlyphAtlasEntry> m_Cache;

    bool SetPixelSize(int pixelSize);
};

} // namespace Mortar

#endif // FN_ENGINE_RENDER_FONTCACHEOBJECTTTF_H
