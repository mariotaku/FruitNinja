#ifndef FN_ENGINE_RENDER_FONT_H
#define FN_ENGINE_RENDER_FONT_H

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

// Non-polymorphic: no vtable (binary ctor @ 0x00198534 writes no PTR_/vptr).
// No base class: binary ctor calls no base-class ctor on this+0.
// Ref-counted via inline AddRef/Release (non-virtual) for SmartPtr<Font> compat;
// ref-count stored at m_RefCount (+0x418). Binary total size = 0x438 bytes.
class Font {
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

    // Binary field layout (0x438 total, base-class-free, non-polymorphic):
    // Binary ctor @ 0x00198534: zeroes m_Glyphs (separate), then loop zeros
    // m_GlyphLookup[0..255] (offsets 0x004..0x400), then zeros 0x404..0x414.
    CharTemplate*  m_Glyphs;           // +0x000  heap array (separate ctor-zero)
    CharTemplate*  m_GlyphLookup[256]; // +0x004  pointers into m_Glyphs, loop-zeroed
    int            m_GlyphCount;       // +0x404
    Page*          m_Pages;            // +0x408
    int            m_PageCount;        // +0x40c
    Kerning*       m_Kernings;         // +0x410
    int            m_KerningCount;     // +0x414
    // +0x418: binary field, ctor-zero. Port uses as non-virtual ref count for
    // SmartPtr<Font> compatibility. Initial value 0 matches SmartPtr semantics.
    int            m_RefCount;         // +0x418
    int            m_ScaleW;           // +0x41c
    int            m_ScaleH;           // +0x420
    float          m_LineHeight;       // +0x424  stored as float
    float          m_BaseNorm;         // +0x428  = base / lineHeight

    // +0x42c: per-page vertex scratch. Binary stores std::vector<QUADCUSTOMVERTEX>
    // per page in an outer std::vector (ctor @ Font::Font, member at this+0x42c).
    // std::vector<std::vector<QUADCUSTOMVERTEX>> = 12 bytes on ARM32.
    // Binary total 0x42c + 12 = 0x438. Page capacity 0x600 verts (binary).
    static const int  PAGE_VERT_CAPACITY = 0x600;
    std::vector< std::vector<QUADCUSTOMVERTEX> > m_PageVerts; // +0x42c

    Font();
    ~Font();

    // Non-virtual ref-count methods for SmartPtr<Font> compatibility.
    // Binary Font is non-polymorphic; SmartPtr<T> calls T::AddRef()/Release()
    // which must exist. Ref count stored at m_RefCount (+0x418).
    void AddRef()    { m_RefCount++; }
    void Release()   { if (--m_RefCount <= 0) delete this; }
    int  GetRefCount() const { return m_RefCount; }

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
    // and maxWH = (wrapPx, scale).  maxWH.y = scale so after the in-DrawString
    // division (maxWH.y /= yLineFactor*scale = 1.0*scale) the normalized per-line
    // pitch is 1.0f, matching binary callers that supply lineHeight as maxWH.y.
    // maxWH.y=0 was the prior value; it made the vertical-alignment formula
    // compute translateY = N/2 instead of the correct (N-1)/2, shifting wrapped
    // blocks off-centre (and in degenerate cases, off-screen -> blank).
    void DrawStringWrapped(float scale, float wrapPx, float z,
                           const char* text, const Vec3& pos,
                           const Colour& colour, int alignment) {
        Mortar::Utf8StringIterator iter(text);
        Vec2 maxWH(wrapPx, scale);  // maxWH.y=scale -> after /= (1*scale) = 1.0 per-line pitch
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
    // ---- Binary-shape ABI overloads (forward to the canonical overloads) ----
    // Binary @ 0x00198e44 -- packed Vec3/Vec2 ABI shape of the full Font_DrawString;
    // forwards to DrawString(scale,yLineFactor,rotZ,iter,pos,colour,maxWH,alignment,
    // z,clipRect) with yLineFactor pinned to 1.0 (binary @ 0x00199b1c).
    void DrawString(Utf8StringIterator iter, Vec3 pos, Colour colour, float scale,
                    Vec2 maxWH, int alignment, float rotZ, MortarRectangleDec* clipRect, float z);
    // Binary @ 0x00199aa0 -- by-value-arg ABI shape of the binary DrawString wrapper;
    // forwards to DrawString(iter&,colour&,alignment,posX,posY,posZ,scale,maxWHx,
    // maxWHy,rotZ,clip).
    void DrawString(Utf8StringIterator iter, float posX, float posY, float posZ,
                    Colour colour, float scale, float maxWHx, float maxWHy,
                    int alignment, MortarRectangleDec* clip, float rotZ);
    // TODO: 0x001985b0 -- word-advance helper for word-wrap. BLOCKED on the
    // Mortar::WordWrap subsystem (CanBreakLineAt @ 0x0019acc4 + East-Asian
    // line-break tables); not called by gameplay code. Returns 0 until ported.
    float FindAdvanceOfNextWord(Utf8StringIterator, float, float, float, float);
    // Binary @ 0x001984e8 -- canonical single-codepoint glyph lookup (1st arg is
    // `this`, typed `long` by Ghidra; 2nd is the codepoint). Falls back to a
    // linear id-search even for cp < 256 when the lookup slot is null.
    CharTemplate* GetCharTemplate(long, int);
    // GetKerning(unsigned long,unsigned long) — same mangling as the
    // (uint32_t, uint32_t) overload above on ARM32 (long == int == 32-bit
    // -> both mangle as `j j`). Existing GetKerning(uint32_t, uint32_t)
    // already covers the binary symbol.
    // Binary @ 0x001988f0 (asm-inspector / re-analyst). Returns the total
    // rendered height of a multi-line string at lineHeight `lineH` wrapped
    // to `maxWidth`. When `maxWidth <= 0`: walks the string counting '\n'
    // and returns `lineH + n*lineH`. When `maxWidth > 0`: word-wrap path
    // using FindAdvanceOfNextWord.
    float GetStringHeight(Utf8StringIterator iter, float lineH, float maxWidth);
    // Binary @ 0x001988a8 -- by-value-iter ABI shape of MeasureString (same binary
    // symbol as the const-ref overload above); forwards to GetLineLength(iter,0,NULL).
    float MeasureString(Utf8StringIterator);
    // ---- end unported overloads ----
};

} // namespace Mortar

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
#include <cstddef>
static_assert(sizeof(Mortar::Font) == 0x438,
              "Mortar::Font size mismatch (binary @ 0x00198534, operator-new not found, size derived from ctor)");
static_assert(offsetof(Mortar::Font, m_GlyphLookup) == 0x004,
              "Mortar::Font::m_GlyphLookup offset");
static_assert(offsetof(Mortar::Font, m_GlyphCount)  == 0x404,
              "Mortar::Font::m_GlyphCount offset");
static_assert(offsetof(Mortar::Font, m_PageVerts)   == 0x42c,
              "Mortar::Font::m_PageVerts offset");
#endif

#endif // FN_ENGINE_RENDER_FONT_H
