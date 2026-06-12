#ifndef FN_ENGINE_RENDER_FONTINTERFACE_H
#define FN_ENGINE_RENDER_FONTINTERFACE_H

// Mortar::FontInterface — dynamic glyph atlas backed by a single GL texture.
//
// Port specific: not a binary struct. The binary's TTF path uses a Samsung Bada
// framework glyph-cache API (IFont/IGlyphCache) that is not portable. This class
// provides an equivalent portable interface: a power-of-two GL texture with a
// CPU-side pixel buffer; new glyphs are packed left-to-right, row-by-row; dirty
// regions are uploaded via glTexSubImage2D when BuildPendingTextures() is called.
//
// The atlas is greyscale (alpha-only, GL_ALPHA / GL_RED) on native; the shader
// interprets the single channel as alpha. Glyph bitmaps are copied in from
// FreeType's FT_BITMAP_PIXEL_MODE_GRAY output (8-bit alpha).
//
// TODO: bevel/stroke/glow effects — stub no-ops for now. Boot and Latin rendering
// do not need them.

#include "render/gl_funcs.h"
#include <cstdint>

namespace Mortar {

// One cached glyph entry in the atlas.
struct GlyphAtlasEntry {
    float    u0, v0;    // top-left UV (0..1)
    float    u1, v1;    // bottom-right UV (0..1)
    int      bearingX;  // FT_GlyphSlot->bitmap_left (pixels)
    int      bearingY;  // FT_GlyphSlot->bitmap_top  (pixels)
    int      advanceX;  // FT_GlyphSlot->advance.x >> 6 (pixels)
    int      width;     // bitmap width in pixels
    int      height;    // bitmap height in pixels
};

class FontInterface {
public:
    // atlasSize: dimension of the square atlas texture (must be power-of-two).
    explicit FontInterface(int atlasSize = 512);
    ~FontInterface();

    // Pack a glyph bitmap (8-bit alpha, width x height bytes) into the atlas.
    // Returns false if the atlas is full. Fills *out with the packed entry.
    // Caller is responsible for calling BuildPendingTextures() to upload.
    bool PackGlyph(int width, int height, const uint8_t* bitmap, GlyphAtlasEntry* out);

    // Upload any dirty region of the CPU buffer to the GL texture.
    void BuildPendingTextures();

    // Return the GL texture object ID.
    GLuint GetTextureID() const { return m_TextureID; }

    // Atlas pixel dimension.
    int GetSize() const { return m_Size; }

    // Reset atlas to empty (frees GL texture + CPU buffer).
    void Clear();

private:
    int      m_Size;           // atlas dimension (e.g. 512)
    uint8_t* m_Pixels;         // CPU-side alpha buffer [size*size]
    GLuint   m_TextureID;      // GL texture object

    // Current packing cursor.
    int      m_CursorX;
    int      m_CursorY;
    int      m_RowHeight;      // tallest glyph in current row

    // Dirty region (bounding box of unpacked glyphs since last upload).
    bool     m_Dirty;
    int      m_DirtyX0, m_DirtyY0;
    int      m_DirtyX1, m_DirtyY1;

    void MarkDirty(int x, int y, int w, int h);
    void EnsureTexture();
};

} // namespace Mortar

#endif // FN_ENGINE_RENDER_FONTINTERFACE_H
