#ifndef FN_ENGINE_RENDER_FONT_H
#define FN_ENGINE_RENDER_FONT_H

#include "util/SmartPtr.h"
#include "asset/Texture.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "render/Utf8StringIterator.h"
#include "math/_Vector2.h"
#include "math/_Vector3.h"
#include "math/Colour.h"
#include "core/MortarTypes.h"
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
                    Mortar::Utf8StringIterator iter, const _Vector3<float>& pos, const Colour& colour,
                    _Vector2<float> maxWH, int alignment, float z,
                    Mortar::MortarRectangleT<float>* clipRect = nullptr);

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
    //   [sp+0] = Mortar::MortarRectangleT<float>*
    // Internally hardcodes yLineFactor = 1.0 when forwarding to the full
    // Font_DrawString @ 0x00198e44. Use this overload when matching binary
    // call sites byte-for-byte; for ad-hoc port-side text rendering prefer
    // the simpler `DrawString(scale, yLineFactor, z, text, ...)` wrapper.
    void DrawString(Mortar::Utf8StringIterator& iter,
                    const Colour& colour, int alignment,
                    float posX, float posY, float posZ,
                    float scale, float maxWHx, float maxWHy, float rotZ,
                    Mortar::MortarRectangleT<float>* clip = nullptr);

    // Port-side convenience wrapper. Forwards to the binary-shape overload
    // above with maxWH = (0, 0). NOT a binary ABI match -- the second arg
    // is the full overload's `yLineFactor` (the binary wrapper hardcodes
    // 1.0; this port wrapper exposes it because some legacy callers pass
    // non-default values). Callers that want strict binary-ABI fidelity
    // should call the binary-shape overload above directly.
    void DrawString(float scale, float yLineFactor, float z,
                    const char* text, const _Vector3<float>& pos,
                    const Colour& colour, int alignment = 0);

    // For compat with old callers that pass scale directly
    void DrawStringSized(float targetSize, float yLineFactor, float z,
                         const char* text, const _Vector3<float>& pos,
                         const Colour& colour, int alignment = 0) {
        DrawString(targetSize, yLineFactor, z, text, pos, colour, alignment);
    }

    // Wrap-aware variant: forwards to the full DrawString with yLineFactor=1.0
    // and maxWH = (wrapPx, 0). Binary inner Font_DrawString @0x0024c7f0 passes
    // maxWH.y=0 from its callers; per-line pitch steps by yLineFactor (not
    // maxWH.y). Vertical-align formula: translateY = (-0 - cursorY - yLineFactor)*0.5
    // = (N*yLineFactor)*0.5 -> centres the N-line block on posY.
    void DrawStringWrapped(float scale, float wrapPx, float z,
                           const char* text, const _Vector3<float>& pos,
                           const Colour& colour, int alignment) {
        Mortar::Utf8StringIterator iter(text);
        _Vector2<float> maxWH(wrapPx, 0.0f);
        DrawString(scale, 1.0f, 0.0f, iter, pos, colour, maxWH, alignment, z, nullptr);
    }

    // Returns normalized text width in lineHeight units (multiply by scale for world units)
    float MeasureWidth(float scale, const char* text) const;
    float MeasureWidth(float scale, Mortar::Utf8StringIterator iter) const;

    // Binary @ 0x0024c794 (v1.6.1; stale 0x001988a8 v1.5.x). Single-line measure: stops at newline or end.
    // Returns total xadvance in lineHeight-normalized units.
    float MeasureString(const Mortar::Utf8StringIterator& iterIn) const;
    float MeasureString(const char* str) const;

    // Line height in world units at given scale
    float GetLineHeight(float scale) const { return scale; }

    CharTemplate* GetCharTemplate(uint32_t cp) const;

    // ASM-verified: 2026-05-09 v1.6.1 binary @ 0x00198528 (re-analyst) -- the
    // shipped Bada build's GetKerning is a 2-instruction stub:
    //     vldr.32 s0, [pc, #0x4]   ; literal 0.0f
    //     bx      lr
    // The .fnt-parsed m_Kernings array (+0x410 / +0x414) is stored but
    // never consulted at draw time. Call sites (GetLineLength,
    // GetStringHeight, FindAdvanceOfNextWord, Font_DrawString @
    // 0x00199854) add the result to the cursor advance, but it's always
    // 0. Port matches exactly; do NOT replace with a real lookup.
    float         GetKerning(uint32_t /*a*/, uint32_t /*b*/) const { return 0.0f; }
    Page*         GetPage(unsigned long idx) const;

private:
    float         GetLineLength(Mortar::Utf8StringIterator iter, float wrapWidth, float* outSlack) const;
    // Port specific: TTF sub-path of Load(). Delegates to FontCacheObjectTTF
    // via FontTTFRegistry (side-table keeps Font layout at binary 0x438 bytes).
    int           LoadTTF(const char* path);

public:
    // ---- Binary-shape ABI overloads (forward to the canonical overloads) ----
    // Binary @ 0x0024c7f0 (v1.6.1; stale 0x00198e44 v1.5.x) -- packed Vec3/Vec2 ABI shape of the full Font_DrawString;
    // forwards to DrawString(scale,yLineFactor,rotZ,iter,pos,colour,maxWH,alignment,
    // z,clipRect) with yLineFactor pinned to 1.0 (binary @ 0x00199b1c).
    void DrawString(Utf8StringIterator iter, _Vector3<float> pos, Colour colour, float scale,
                    _Vector2<float> maxWH, int alignment, float rotZ, Mortar::MortarRectangleT<float>* clipRect, float z);
    // Binary @ 0x0024d6b8 (v1.6.1; stale 0x00199aa0 v1.5.x) -- by-value-arg ABI shape of the binary DrawString wrapper;
    // forwards to DrawString(iter&,colour&,alignment,posX,posY,posZ,scale,maxWHx,
    // maxWHy,rotZ,clip).
    void DrawString(Utf8StringIterator iter, float posX, float posY, float posZ,
                    Colour colour, float scale, float maxWHx, float maxWHy,
                    int alignment, Mortar::MortarRectangleT<float>* clip, float rotZ);
    // TODO: v1.6.1 Font::FindAdvanceOfNextWord @0x0024c2a0 -- word-advance helper for word-wrap. BLOCKED on the
    // Mortar::WordWrap subsystem (CanBreakLineAt + East-Asian line-break tables).
    // Binary return type is Utf8StringIterator/char* (start iter if word fits,
    // NULL if line must break) -- port's float return is a Ghidra mis-decode;
    // correct when WordWrap lands. Returns 0 until ported.
    float FindAdvanceOfNextWord(Utf8StringIterator, float, float, float, float);
    // Binary @ 0x0024c228 -- canonical single-codepoint glyph lookup (1st arg is
    // `this`, typed `long` by Ghidra; 2nd is the codepoint). Falls back to a
    // linear id-search even for cp < 256 when the lookup slot is null.
    CharTemplate* GetCharTemplate(long, int);
    // GetKerning(unsigned long,unsigned long) — same mangling as the
    // (uint32_t, uint32_t) overload above on ARM32 (long == int == 32-bit
    // -> both mangle as `j j`). Existing GetKerning(uint32_t, uint32_t)
    // already covers the binary symbol.
    // Binary @ 0x0024c45c (v1.6.1). Returns the total rendered height of a
    // multi-line string at lineHeight `lineH` wrapped to `maxWidth`.
    // When `maxWidth <= 0`: walks the string counting '\n' and returns
    // `lineH + n*lineH`. When `maxWidth > 0`: word-wrap path using
    // FindAdvanceOfNextWord (DIFFERS: space-heuristic until WordWrap lands).
    float GetStringHeight(Utf8StringIterator iter, float lineH, float maxWidth);
    // Binary @ 0x0024c794 (v1.6.1; stale 0x001988a8 v1.5.x) -- by-value-iter ABI shape of MeasureString (same binary
    // symbol as the const-ref overload above); forwards to GetLineLength(iter,0,NULL).
    float MeasureString(Utf8StringIterator);
    // ---- end unported overloads ----
};

// ---------------------------------------------------------------------------
// Binary .fnt text format parser helpers — free functions in namespace Mortar.
// These produce symbols for asm-verify coverage; Font::Load continues to use
// the static ParseFntInt/ParseFntString helpers inline.
//
// ASM-spec v1.6.1:
//   Mortar::Next_Word_Is    @0x0024bdf4
//   Mortar::Get_Next_Value  @0x0024bb54
//   Mortar::Parse_Char      @0x0024be44
//   Mortar::Parse_Page      @0x0024d744
//   Mortar::Parse_Kerning   @0x0024c0b0
// ---------------------------------------------------------------------------

// Compare key[0..strlen(word)) vs word; true if fully matched up to word length.
// Stops on space within key. Used by Parse_Char/Parse_Page/Parse_Kerning to
// identify which attribute was just scanned by Get_Next_Value.
bool Next_Word_Is(char* key, const char* word);

// Scan one key=value token from line.
// keyBuf: 32-byte buffer filled with the key name (leading/trailing spaces stripped).
// intOut: receives the parsed signed integer (sentinel -0xaabe = not set).
// strHeapOut: receives heap-allocated char* for quoted-string values (else nullptr).
// Returns: positive N = consumed N bytes (caller advances cursor by N);
//          negative  = end-of-line or parse error (caller stops loop).
int Get_Next_Value(char* line, char* keyBuf, int* intOut, char** strHeapOut);

// Parse a "char " .fnt line into a CharTemplate, calling Get_Next_Value in a loop.
// Stores raw int-as-float values; caller must normalize (divide by scaleW/H, lineHeight).
// Returns bytes consumed from line.
int Parse_Char(char* line, Font::CharTemplate* out, int lineLen);

// Parse a "page " .fnt line into a Page (zero-inits filename and texture first).
// Heap-allocates page->filename (truncated at '.' i.e. no extension) via Get_Next_Value.
// Returns bytes consumed from line.
int Parse_Page(char* line, Font::Page* out, int lineLen);

// Parse a "kerning " .fnt line into a Kerning (zeroes all 12 bytes first).
// first/second as int, amount as (float)intVal.
// Returns bytes consumed from line.
int Parse_Kerning(char* line, Font::Kerning* out, int lineLen);

} // namespace Mortar

#if defined(__bada__)
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
