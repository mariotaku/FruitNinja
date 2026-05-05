#ifndef FN_ENGINE_RENDER_FONT_H
#define FN_ENGINE_RENDER_FONT_H

#include "util/ReferenceCounter.h"
#include "util/SmartPtr.h"
#include "asset/Texture.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "render/Utf8StringIterator.h"
#include "math/Vec2.h"
#include "math/Vec3.h"
#include "math/Colour.h"
#include <vector>

namespace Mortar {

// Binary 0x438 bytes.
// Alignment flags for DrawString (bits 0-1: horiz, bits 2-3: vert, bit 4: wrap)
enum FontAlignment {
    FONT_ALIGN_LEFT   = 0x00,
    FONT_ALIGN_CENTER = 0x01,
    FONT_ALIGN_RIGHT  = 0x02,
    FONT_ALIGN_MIDDLE = 0x04,
    FONT_ALIGN_BOTTOM = 0x08,
    FONT_ALIGN_WRAP   = 0x10
};

// Stub for clip-rect parameter (shipping callers always pass nullptr)
struct MortarRectangleDec {
    float left, top, right, bottom;
    float Width()  const { return right - left; }
    float Height() const { return bottom - top; }
};

class Font : public ReferenceCounter {
public:
    // 0x24 bytes per binary (ARM-confirmed at 0x0019a128)
    struct CharTemplate {
        uint16_t id;
        uint16_t _pad;
        float    u0;    // = atlasX  / scaleW
        float    v0;    // = atlasY  / scaleH
        float    w;     // = pxW     / lineHeight
        float    h;     // = pxH     / lineHeight
        float    xoff;  // = pxXoff  / lineHeight
        float    yoff;  // = pxYoff  / lineHeight
        float    xadv;  // = pxXadv  / lineHeight
        uint8_t  page;
        uint8_t  _pad2[3];
    };

    // 8 bytes per binary
    struct Page {
        const char*             filename;  // owned (new char[])
        SmartPtr<Mortar::Texture> texture;

        Page() : filename(nullptr) {}
        ~Page() { delete[] filename; }
    };

    // 12 bytes; parsed but unused at runtime (GetKerning stubs to 0)
    struct Kerning {
        uint32_t first;
        uint32_t second;
        float    amount;
    };

    // Binary field layout (0x438 total):
    CharTemplate*  m_Glyphs;           // +0x000  heap array
    CharTemplate*  m_GlyphLookup[256]; // +0x004  pointers into m_Glyphs by id
    int            m_GlyphCount;       // +0x404
    Page*          m_Pages;            // +0x408
    int            m_PageCount;        // +0x40c
    Kerning*       m_Kernings;         // +0x410
    int            m_KerningCount;     // +0x414
    int            _pad_0x418;         // +0x418
    int            m_ScaleW;           // +0x41c
    int            m_ScaleH;           // +0x420
    float          m_LineHeight;       // +0x424  stored as float
    float          m_BaseNorm;         // +0x428  = base / lineHeight
    std::vector<std::vector<QUADCUSTOMVERTEX>> m_PageVerts; // +0x42c

    Font();
    virtual ~Font();

    // Instance load (matches binary 0x00199e9c). Returns 1/0.
    int Load(const char* path);

    // Static factory: allocates, loads, returns SmartPtr.
    static SmartPtr<Font> Create(const char* path);

    // Matches Font_DrawString (0x00198e44).
    void DrawString(float scale, float maxWidth, float rotZ,
                    Mortar::Utf8StringIterator iter, const Vec3& pos, const Colour& colour,
                    Vec2 maxWH, int alignment, float z,
                    MortarRectangleDec* clipRect = nullptr);

    // Thin wrapper (0x00199aa0): packs x/y/z into Vec3, calls DrawString with maxWidth=1.0.
    void DrawString(float scale, float maxWidth, float z,
                    const char* text, const Vec3& pos,
                    const Colour& colour, int alignment = 0);

    // For compat with old callers that pass scale directly
    void DrawStringSized(float targetSize, float maxWidth, float z,
                         const char* text, const Vec3& pos,
                         const Colour& colour, int alignment = 0) {
        DrawString(targetSize, maxWidth, z, text, pos, colour, alignment);
    }

    // Wrap-aware variant: forwards to the full DrawString with maxWidth=1.0
    // (line pitch) and maxWH.x = wrapPx (wrap constraint). Matches the
    // binary's call shape for the description-text path.
    void DrawStringWrapped(float scale, float wrapPx, float z,
                           const char* text, const Vec3& pos,
                           const Colour& colour, int alignment) {
        Mortar::Utf8StringIterator iter(text);
        Vec2 maxWH(wrapPx, 0.0f);
        DrawString(scale, 1.0f, 0.0f, iter, pos, colour, maxWH, alignment, z, nullptr);
    }

    // Returns normalized text width in lineHeight units (multiply by scale for world units)
    float MeasureWidth(float scale, const char* text) const;
    float MeasureWidth(float scale, Mortar::Utf8StringIterator iter) const;

    // Binary @ 0x001988a8. Single-line measure: stops at newline or end.
    // Returns total xadvance in lineHeight-normalized units.
    float MeasureString(const Mortar::Utf8StringIterator& iterIn) const;
    float MeasureString(const char* str) const;

    // Line height in world units at given scale
    float GetLineHeight(float scale) const { return scale; }

    CharTemplate* GetCharTemplate(uint32_t cp) const;
    float         GetKerning(uint32_t a, uint32_t b) const { return 0.0f; }
    Page*         GetPage(int idx) const;

private:
    float         GetLineLength(Mortar::Utf8StringIterator iter, float wrapWidth, float* outSlack);
    const char*   FindAdvanceOfNextWord(Mortar::Utf8StringIterator iter, float curX, float maxX,
                                        float scale, float spacing) const;
};

} // namespace Mortar

#endif // FN_ENGINE_RENDER_FONT_H
