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
// Port specific: glyph atlas is RGBA (white + coverage-alpha) so GL_MODULATE
// yields vertex-coloured text on both desktop FFP and emscripten WebGL (which
// lacks GL_COMBINE). Binary used Bada IFont with an RGBA atlas. Each texel is
// (R=255, G=255, B=255, A=coverage) expanded from FreeType's 8-bit gray output.
//
// Scaling constants mirror binary FontInterface ctor @ 0x002502e0 and
// Initialize @ 0x00250470:
//   m_CacheSize      = 100  (used as FT DPI in FT_Set_Char_Size; set in ctor)
//   m_FontScale      = 1.0  (super-sampling factor; set in Initialize)
//   m_InvFontScale   = 1.0  (1/m_FontScale; set in Initialize)
//   m_GlobalSizeScale = 1.0 (0.9 when game language byte == 0x13; set in Initialize)
//
// TODO: bevel/stroke/glow effects — stub no-ops for now. Boot and Latin rendering
// do not need them.

#include "render/gl_funcs.h"
#include <cstdint>

namespace Mortar {

// One cached glyph entry in the atlas.
// All metric fields (bearingX/Y, advanceX, width, height) are in world units:
//   FT_26.6_metric * invFontScale * (1/64)
// With invFontScale=1.0 (the normal case) this equals FT_metric / 64.0.
struct GlyphAtlasEntry {
    float    u0, v0;    // top-left UV (0..1)
    float    u1, v1;    // bottom-right UV (0..1)
    float    bearingX;  // horiBearingX in world units
    float    bearingY;  // horiBearingY in world units
    float    advanceX;  // horiAdvance  in world units
    float    width;     // bitmap width in pixels (used for atlas packing only)
    float    height;    // bitmap height in pixels (used for atlas packing only)
};

class FontInterface {
public:
    // atlasSize: dimension of the square atlas texture (must be power-of-two).
    explicit FontInterface(int atlasSize = 512);
    ~FontInterface();

    // Mirrors binary Initialize @ 0x00250470: sets fontScale/invFontScale/globalSizeScale.
    // Call once after construction. languageByte is game_work+3 (0x13 = Korean, scale 0.9).
    void InitialiseData(float fontScale, float globalSizeScale);

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

    // Binary-derived scaling constants (read by FontCacheObjectTTF::SetFontSize).
    int   m_CacheSize;         // FT DPI: 100 (binary FontInterface ctor @ 0x002502e0)
    float m_FontScale;         // super-sampling factor: 1.0 (binary Initialize @ 0x00250470)
    float m_InvFontScale;      // 1/m_FontScale: 1.0
    float m_GlobalSizeScale;   // 1.0 normally; 0.9 for Korean (lang byte 0x13)

private:
    int      m_Size;           // atlas dimension (e.g. 512)
    uint8_t* m_Pixels;         // CPU-side RGBA buffer [size*size*4]
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
