// stb_truetype implementation of the Mortar::TtfFace seam (TtfBackend.h).
//
// Selected when FN_TTF_BACKEND=stb (forced for FRUIT_PLATFORM_WII; opt-in
// elsewhere). Reproduces FreeType 26.6-metric semantics (see TtfBackend.h)
// on top of stb_truetype's pixel-space API so FontCacheObjectTTF's math is
// unchanged regardless of which backend is compiled.
//
// STB_TRUETYPE_IMPLEMENTATION is defined in EXACTLY this one TU.

#include "render/TtfBackend.h"
#include "debug/Logger.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#include "third_party/stb/stb_truetype.h"

namespace Mortar {

struct TtfFace::Impl {
    unsigned char*    fontData;    // owned, heap; must outlive `font`
    stbtt_fontinfo    font;
    float             scale;       // stbtt_ScaleForPixelHeight() at current pixel size
    std::vector<unsigned char> bitmapBuf; // reused RasterizeGlyph output
};

TtfFace::TtfFace() : m_p(NULL) {}

TtfFace* TtfFace::Open(const char* path, int /*pixelSize*/) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        LOG_ERROR("TtfBackendStb", "fopen failed for '%s'", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        LOG_ERROR("TtfBackendStb", "empty/unreadable font file '%s'", path);
        return NULL;
    }

    unsigned char* data = new unsigned char[(size_t)size];
    size_t nread = fread(data, 1, (size_t)size, f);
    fclose(f);
    if (nread != (size_t)size) {
        delete[] data;
        LOG_ERROR("TtfBackendStb", "short read for '%s'", path);
        return NULL;
    }

    TtfFace* self = new TtfFace();
    self->m_p = new Impl();
    self->m_p->fontData = data;
    self->m_p->scale = 0.0f;

    int offset = stbtt_GetFontOffsetForIndex(data, 0);
    if (offset < 0 || !stbtt_InitFont(&self->m_p->font, data, offset)) {
        LOG_ERROR("TtfBackendStb", "stbtt_InitFont failed for '%s'", path);
        delete self->m_p;
        delete self;
        return NULL;
    }

    return self;
}

TtfFace::~TtfFace() {
    if (m_p) {
        delete[] m_p->fontData;
        delete m_p;
    }
}

bool TtfFace::IsValid() const {
    return m_p != NULL && m_p->fontData != NULL;
}

void TtfFace::SetPixelSize(long charHeight_26_6) {
    if (!IsValid()) return;
    float pixelSize = (float)charHeight_26_6 / 64.0f;
    m_p->scale = stbtt_ScaleForPixelHeight(&m_p->font, pixelSize);
}

unsigned TtfFace::GetGlyphIndex(uint32_t cp) const {
    if (!IsValid()) return 0;
    return (unsigned)stbtt_FindGlyphIndex(&m_p->font, (int)cp);
}

bool TtfFace::RasterizeGlyph(unsigned glyphIndex, TtfRasterGlyph& out) {
    if (!IsValid()) return false;

    const float scale = m_p->scale;
    int gi = (int)glyphIndex;

    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBox(&m_p->font, gi, scale, scale, &x0, &y0, &x1, &y1);
    int w = x1 - x0;
    int h = y1 - y0;

    int adv, lsb;
    stbtt_GetGlyphHMetrics(&m_p->font, gi, &adv, &lsb);

    // advanceX/bearingX/inkHeight follow directly from stb outputs; sign
    // mapping for bearingY is the one risky spot (see file header + spec):
    // stb's (x0,y0,x1,y1) box is in Y-DOWN pixel space with y0 = top of ink.
    // FreeType's horiBearingY is Y-UP (distance from baseline to ink TOP,
    // positive). Since y0 is already "distance from baseline to ink top"
    // measured downward, negating it gives the FT Y-up convention.
    out.advanceX_26_6  = (long)std::lround((double)((float)adv * scale * 64.0f));
    out.bearingX_26_6  = (long)std::lround((double)((float)x0 * 64.0f));
    out.bearingY_26_6  = (long)std::lround((double)((float)(-y0) * 64.0f));
    out.inkHeight_26_6 = (long)std::lround((double)((float)h * 64.0f));
    out.bitmapLeft     = x0;
    out.width          = w;
    out.height         = h;

    if (w <= 0 || h <= 0) {
        out.bitmap = NULL;
        out.width  = 0;
        out.height = 0;
        return true; // ink-less glyph (e.g. space) -- caller handles the empty-cell branch
    }

    m_p->bitmapBuf.resize((size_t)w * (size_t)h);
    stbtt_MakeGlyphBitmap(&m_p->font, &m_p->bitmapBuf[0], w, h, /*stride*/w, scale, scale, gi);
    out.bitmap = &m_p->bitmapBuf[0];

    return true;
}

long TtfFace::GetKerning_26_6(uint32_t a, uint32_t b) {
    if (!IsValid()) return 0;
    int ia = stbtt_FindGlyphIndex(&m_p->font, (int)a);
    int ib = stbtt_FindGlyphIndex(&m_p->font, (int)b);
    if (ia == 0 || ib == 0) return 0;
    int kern = stbtt_GetGlyphKernAdvance(&m_p->font, ia, ib);
    if (kern == 0) return 0; // matches these fonts having no kern/GPOS table
    return (long)std::lround((double)((float)kern * m_p->scale * 64.0f));
}

long TtfFace::GetAscender_26_6() const {
    if (!IsValid()) return 0;
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&m_p->font, &asc, &desc, &gap);
    return (long)std::lround((double)((float)asc * m_p->scale * 64.0f));
}

long TtfFace::GetDescender_26_6() const {
    if (!IsValid()) return 0;
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&m_p->font, &asc, &desc, &gap);
    // stb's descent is already negative (below baseline), matching FT's sign.
    return (long)std::lround((double)((float)desc * m_p->scale * 64.0f));
}

long TtfFace::GetLineHeight_26_6() const {
    if (!IsValid()) return 0;
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&m_p->font, &asc, &desc, &gap);
    return (long)std::lround((double)((float)(asc - desc + gap) * m_p->scale * 64.0f));
}

} // namespace Mortar
