#ifndef FN_ENGINE_RENDER_TTFBACKEND_H
#define FN_ENGINE_RENDER_TTFBACKEND_H

// Mortar::TtfFace — compile-time swappable dynamic-TTF rasterizer seam.
//
// Port specific: not a binary struct. FontCacheObjectTTF used to call
// FreeType directly; that dependency is now behind this interface so a
// second backend (stb_truetype) can be selected at CMake configure time via
// FN_TTF_BACKEND (freetype|stb) without touching FontCacheObjectTTF at all.
// Exactly ONE of TtfBackendFreetype.cpp / TtfBackendStb.cpp is compiled per
// build (mirrors the *SDL.cpp / *Win32.cpp platform-file convention: one
// implementation file per backend, selected by the build, not #ifdef'd
// inside a shared TU).
//
// All metrics are FreeType-convention 26.6 fixed point (1 unit = 1/64 px),
// Y-up (bearingY positive = above baseline; descender negative). The stb
// backend maps stb's Y-down box coordinates onto this convention internally
// so FontCacheObjectTTF never needs to know which backend is active.
//
// Lifecycle: Open() heap-allocates and owns the font file bytes (both
// backends need the buffer to stay alive for the face's lifetime); the
// returned TtfFace also owns a single reusable glyph bitmap buffer, so the
// pointer returned in TtfRasterGlyph::bitmap is only valid until the next
// RasterizeGlyph() call or face destruction — copy it out before that.

#include <stdint.h>

namespace Mortar {

struct TtfRasterGlyph {
    const uint8_t* bitmap;      // 8-bit coverage, pitch == width; valid until next RasterizeGlyph/face destroy
    int  width, height;
    int  bitmapLeft;
    long advanceX_26_6, bearingX_26_6, bearingY_26_6, inkHeight_26_6;
};

class TtfFace {
public:
    // Returns nullptr on failure (bad path / unparsable font). Owns the font
    // file bytes for the lifetime of the returned face.
    static TtfFace* Open(const char* path, int pixelSize);
    ~TtfFace();

    bool IsValid() const;

    // Sets the rasterization size. charHeight_26_6 is a LITERAL pixel height
    // at standard 72dpi -- the CALLER (FontCacheObjectTTF::SetCharSize) has
    // already folded in both the kFontSupersample multiplier AND the
    // binary's m_CacheSize=100 DPI factor (100/72) before calling here, so
    // this seam does not know about supersampling or DPI at all; it's a
    // literal "rasterize at this many pixels tall" request for both backends.
    void SetPixelSize(long charHeight_26_6);

    // Returns 0 if the codepoint has no glyph in this face (matches
    // FT_Get_Char_Index's "not found" contract).
    unsigned GetGlyphIndex(uint32_t cp) const;

    // Rasterizes glyphIndex at the current pixel size (FT_LOAD_RENDER
    // equivalent). Returns false only on a hard failure; an ink-less glyph
    // (e.g. space) returns true with width==0 || height==0.
    bool RasterizeGlyph(unsigned glyphIndex, TtfRasterGlyph& out);

    // Returns 0 if the font has no kerning data for the pair (matches the
    // binary's GetKerning stub behaviour for .fnt fonts — see
    // FontCacheObjectTTF::GetKerningForPair).
    long GetKerning_26_6(uint32_t a, uint32_t b);

    long GetAscender_26_6()  const; // at current pixel size
    long GetDescender_26_6() const; // at current pixel size (negative, below baseline)
    long GetLineHeight_26_6() const;

private:
    TtfFace();
    // Non-copyable.
    TtfFace(const TtfFace&);
    TtfFace& operator=(const TtfFace&);

    struct Impl;
    Impl* m_p; // pimpl -- keeps FT/stb types out of this header
};

} // namespace Mortar

#endif // FN_ENGINE_RENDER_TTFBACKEND_H
