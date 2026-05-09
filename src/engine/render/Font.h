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

class Font : public Mortar::ReferenceCounter {
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
        Mortar::SmartPtr<Mortar::Texture> texture;

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

    // +0x42c per-page vertex scratch buffer. Binary stores 0x600 verts
    // per page; port keeps a single flat heap allocation of size
    // m_PageCount * 0x600 (matches binary's array-of-arrays layout
    // without the std::vector<std::vector<>> instantiation bloat).
    // Indexed as m_PageVerts[pg * 0x600 + slot]. Allocated by Load,
    // freed by ~Font.
    static const int  PAGE_VERT_CAPACITY = 0x600;
    QUADCUSTOMVERTEX* m_PageVerts;

    Font();
    virtual ~Font();

    // Instance load (matches binary 0x00199e9c). Returns 1/0.
    int Load(const char* path);

    // Static factory: allocates, loads, returns SmartPtr.
    static Mortar::SmartPtr<Font> Create(const char* path);

    // Matches Font_DrawString (0x00198e44).
    // ASM-verified: 2026-05-09 (asm-inspector) -- second float arg is the
    // Y-axis line-pitch divisor on maxWH.y, NOT a word-wrap limit. The
    // prologue does:
    //     maxWH.x /= scale;
    //     maxWH.y /= (yLineFactor * scale);
    // Wrap-or-not is gated entirely by `maxWH.x > 0` plus the alignment
    // bit 0x10. Most binary callers pass yLineFactor = 1.0; ScoreControl's
    // highscore "BEST" label passes 0.9 (DAT_0015979c). Passing 0 here
    // produces 0/0 = NaN inside the divide and corrupts vertical alignment;
    // callers must pass 1.0 (or the binary's exact value) -- never 0.
    void DrawString(float scale, float yLineFactor, float rotZ,
                    Mortar::Utf8StringIterator iter, const Vec3& pos, const Colour& colour,
                    Vec2 maxWH, int alignment, float z,
                    MortarRectangleDec* clipRect = nullptr);

    // Binary-shape wrapper @ 0x00199aa0.
    //
    // ASM-verified: 2026-05-09 (asm-inspector) -- exact arg order so the
    // hard-float ABI lays out as in the binary:
    //   r0 = this
    //   r1 = Utf8StringIterator& (by-reference; binary copies to local)
    //   r2 = const Colour&        (by-reference; binary copies to local)
    //   r3 = alignment
    //   s0 = posX, s1 = posY, s2 = posZ
    //   s3 = scale
    //   s4 = maxWHx, s5 = maxWHy
    //   s6 = rotZ
    //   [sp+0] = MortarRectangleDec*
    // Internally hardcodes yLineFactor = 1.0 when forwarding to the full
    // Font_DrawString @ 0x00198e44. Use this overload when matching binary
    // call sites byte-for-byte; for ad-hoc port-side text rendering prefer
    // the simpler `DrawString(scale, yLineFactor, z, text, ...)` wrapper.
    void DrawString(Mortar::Utf8StringIterator& iter,
                    const Colour& colour, int alignment,
                    float posX, float posY, float posZ,
                    float scale, float maxWHx, float maxWHy, float rotZ,
                    MortarRectangleDec* clip = nullptr);

    // Port-side convenience wrapper. Forwards to the binary-shape overload
    // above with maxWH = (0, 0). NOT a binary ABI match -- the second arg
    // is the full overload's `yLineFactor` (the binary wrapper hardcodes
    // 1.0; this port wrapper exposes it because some legacy callers pass
    // non-default values). Callers that want strict binary-ABI fidelity
    // should call the binary-shape overload above directly.
    void DrawString(float scale, float yLineFactor, float z,
                    const char* text, const Vec3& pos,
                    const Colour& colour, int alignment = 0);

    // For compat with old callers that pass scale directly
    void DrawStringSized(float targetSize, float yLineFactor, float z,
                         const char* text, const Vec3& pos,
                         const Colour& colour, int alignment = 0) {
        DrawString(targetSize, yLineFactor, z, text, pos, colour, alignment);
    }

    // Wrap-aware variant: forwards to the full DrawString with yLineFactor=1.0
    // and maxWH.x = wrapPx (wrap constraint). Matches the binary's call shape
    // for the description-text path.
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

    // ASM-verified: 2026-05-09 binary @ 0x00198528 (re-analyst) -- the
    // shipped Bada build's GetKerning is a 2-instruction stub:
    //     vldr.32 s0, [pc, #0x4]   ; literal 0.0f
    //     bx      lr
    // The .fnt-parsed m_Kernings array (+0x410 / +0x414) is stored but
    // never consulted at draw time. Call sites (GetLineLength,
    // GetStringHeight, FindAdvanceOfNextWord, Font_DrawString @
    // 0x00199854) add the result to the cursor advance, but it's always
    // 0. Port matches exactly; do NOT replace with a real lookup.
    float         GetKerning(uint32_t /*a*/, uint32_t /*b*/) const { return 0.0f; }
    Page*         GetPage(int idx) const;

private:
    float         GetLineLength(Mortar::Utf8StringIterator iter, float wrapWidth, float* outSlack) const;

public:
    // ---- STUBS (binary) ----
    // STUB: Font::DrawString(Utf8StringIterator,Vec3,Colour,float,Vec2,int,float,MortarRectangleDec*,float) -- binary @ 0x???? (TODO RE)
    void DrawString(Utf8StringIterator, Vec3, Colour, float, Vec2, int, float, MortarRectangleDec*, float);
    // STUB: Font::DrawString(Utf8StringIterator,float,float,float,Colour,float,float,float,int,MortarRectangleDec*,float) -- binary @ 0x???? (TODO RE)
    void DrawString(Utf8StringIterator, float, float, float, Colour, float, float, float, int, MortarRectangleDec*, float);
    // STUB: Font::FindAdvanceOfNextWord(Utf8StringIterator,float,float,float,float) -- binary @ 0x???? (TODO RE)
    float FindAdvanceOfNextWord(Utf8StringIterator, float, float, float, float);
    // STUB: Font::GetCharTemplate(long,int) -- binary @ 0x???? (TODO RE)
    CharTemplate* GetCharTemplate(long, int);
    // GetKerning(unsigned long,unsigned long) — same mangling as the
    // (uint32_t, uint32_t) overload above on ARM32 (long == int == 32-bit
    // -> both mangle as `j j`). Existing GetKerning(uint32_t, uint32_t)
    // already covers the binary symbol.
    // STUB: Font::GetStringHeight(Utf8StringIterator,float,float) -- binary @ 0x???? (TODO RE)
    void GetStringHeight(Utf8StringIterator, float, float);
    // STUB: Font::MeasureString(Utf8StringIterator) -- binary @ 0x???? (TODO RE)
    float MeasureString(Utf8StringIterator);
    // ---- end STUBS ----
};

} // namespace Mortar

#endif // FN_ENGINE_RENDER_FONT_H
