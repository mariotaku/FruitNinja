#ifndef FN_ENGINE_RENDER_FONTINTERFACE_H
#define FN_ENGINE_RENDER_FONTINTERFACE_H

// Mortar::FontInterface — dynamic multi-page glyph atlas backed by GL textures.
//
// Port specific: not a binary struct. The binary's TTF path uses a Samsung Bada
// framework glyph-cache API (IFont/IGlyphCache) that is not portable. This class
// provides an equivalent portable interface.
//
// DIFFERS: binary uses TextureAtlas @0x00269c9c with std::vector<TextureAtlasPage*>
//   m_Pages; 256x256 RGBA pages; port uses 512x512 pages (kFontSupersample=3).
//
// Multi-page model (faithful to binary TextureAtlas @0x00269c9c):
//   PackGlyph tries the current (last) page; if the glyph does not fit it
//   allocates a NEW page and packs into it. Glyphs are NEVER dropped.
//   Each GlyphAtlasEntry carries a pageTextureID so callers can bind the right
//   GL texture per glyph without holding a FontInterface pointer.
//
// Port specific: glyph atlas is RGBA (white + coverage-alpha) so GL_MODULATE
//   yields vertex-coloured text. Binary used Bada IFont with an RGBA atlas.
//
// Scaling constants mirror binary FontInterface ctor @ 0x002502e0 and
// Initialize @ 0x00250470:
//   m_CacheSize      = 100  (used as FT DPI in FT_Set_Char_Size; set in ctor)
//   m_FontScale      = 1.0  (super-sampling factor; set in Initialize)
//   m_InvFontScale   = 1.0  (1/m_FontScale; set in Initialize)
//   m_GlobalSizeScale = 1.0 (0.9 when game language byte == 0x13; set in Initialize)
//
// TODO: bevel/stroke/glow effects — stub no-ops for now.

#include "render/gl_funcs.h"
#include <cstdint>
#include <vector>

namespace Mortar {

// Per-page atlas state. Owned by FontInterface (vector of pointers).
// One instance per allocated atlas page; all pages share the same m_Size.
// Port specific: matches binary TextureAtlasPage role.
struct FontAtlasPage {
    uint8_t* m_Pixels;       // RGBA buffer [pageSize*pageSize*4], calloc'd
    GLuint   m_TextureID;    // GL texture object (0 until EnsureTexture is called)
    int      m_CursorX;
    int      m_CursorY;
    int      m_RowHeight;    // tallest glyph in current row
    bool     m_Dirty;
    int      m_DirtyX0, m_DirtyY0;
    int      m_DirtyX1, m_DirtyY1;
};

// One cached glyph entry in the atlas.
// All metric fields (bearingX/Y, advanceX, width, height) are in world units:
//   FT_26.6_metric * invFontScale * (1/64)
// pageTextureID: GL texture object of the atlas page that holds this glyph.
//   Callers bind this directly to GL_TEXTURE_2D before drawing the glyph quad.
struct GlyphAtlasEntry {
    float    u0, v0;         // top-left UV (0..1) within pageTextureID
    float    u1, v1;         // bottom-right UV (0..1) within pageTextureID
    float    bearingX;       // horiBearingX in world units
    float    bearingY;       // horiBearingY in world units
    float    advanceX;       // horiAdvance  in world units
    float    width;          // bitmap width in world units (logical quad size)
    float    height;         // bitmap height in world units (logical quad size)
    GLuint   pageTextureID;  // GL texture of the atlas page containing this glyph
};

class FontInterface {
public:
    // ASM-spec v1.6.1 Mortar::FontInterface::FontInterface @0x002502e0: no-arg ctor
    // (decompile confirms param_count=1, i.e. only `this` -- no atlasSize parameter in
    // the binary). Atlas page dimension is fixed at 512 internally (port DIFFERS: binary
    // uses 256x256 TextureAtlas pages -- see file header note above).
    FontInterface();
    ~FontInterface();

    // Mirrors binary Initialize @ 0x00250470: sets fontScale/invFontScale/globalSizeScale.
    // Call once after construction. languageByte is game_work+3 (0x13 = russian, scale 0.9).
    void InitialiseData(float fontScale, float globalSizeScale);

    // Pack a glyph bitmap (8-bit alpha, width x height bytes) into the atlas.
    // Allocates a new page if the current page is full. NEVER drops glyphs.
    // DIFFERS: binary TextureAtlas::AddTexture @0x00269c9c -- faithful multi-page model.
    // Fills *out including out->pageTextureID.
    // Always returns true (kept for API compatibility with old single-page callers).
    bool PackGlyph(int width, int height, const uint8_t* bitmap, GlyphAtlasEntry* out);

    // Upload dirty regions on ALL pages to their GL textures.
    void BuildPendingTextures();

    // Number of allocated atlas pages.
    int GetPageCount() const { return (int)m_Pages.size(); }

    // GL texture ID of the page at index idx. Returns 0 if idx is out of range.
    GLuint GetPageTextureID(int idx) const;

    // Returns page 0's GL texture ID (or 0 if no pages exist).
    // For draw loops that need a single texture — prefer GlyphAtlasEntry::pageTextureID
    // per-glyph instead; this is retained for backward-compat call sites.
    GLuint GetTextureID() const { return GetPageTextureID(0); }

    // Atlas page pixel dimension.
    int GetSize() const { return m_Size; }

    // Reset atlas to empty: delete all pages, their GL textures, and pixel buffers.
    void Clear();

    // Binary-derived scaling constants (read by FontCacheObjectTTF::SetFontSize).
    int   m_CacheSize;         // FT DPI: 100 (binary FontInterface ctor @ 0x002502e0)
    float m_FontScale;         // super-sampling factor: 1.0 (binary Initialize @ 0x00250470)
    float m_InvFontScale;      // 1/m_FontScale: 1.0
    float m_GlobalSizeScale;   // 1.0 normally; 0.9 for russian (lang byte 0x13)

private:
    int m_Size;                          // page dimension (e.g. 512)
    std::vector<FontAtlasPage*> m_Pages; // owned pages, grows on demand

    // Allocate, initialise, and push_back a new page. Returns the new page.
    FontAtlasPage* AllocatePage();

    // Ensure page has a GL texture (uploads the initial blank buffer).
    void EnsurePageTexture(FontAtlasPage* page);

    // Expand the page's dirty region to include (x, y, w, h).
    void MarkPageDirty(FontAtlasPage* page, int x, int y, int w, int h);
};

} // namespace Mortar

#endif // FN_ENGINE_RENDER_FONTINTERFACE_H
