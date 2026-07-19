#include "render/FontInterface.h"
#include "render/FontCacheObjectTTF.h"   // kFontSupersample (inter-glyph margin)
#include "render/gl_funcs.h"
#include "debug/Logger.h"
#include <cstring>
#include <cstdlib>

#ifdef FRUIT_PLATFORM_WII
// Forward-declared rather than including render/gl_funcsWii.h to avoid
// pulling in <gccore.h> (that header's declarations besides this one need
// the real GXTexObj type visible). Must match gl_funcsWii.h's plain C++
// linkage exactly (no extern "C" -- the shim's seam accessors aren't C).
extern void Wii_KeepTextureLinear(unsigned int glTexId);
#endif

namespace Mortar {

// Atlas texel layout. Host/web: RGBA8 (white RGB + alpha=coverage). Wii:
// LUMINANCE_ALPHA (2 B/texel: L=intensity, A=coverage), uploaded by the GX
// shim as GX_TF_IA8 (see gl_funcsWii.cpp's TileIA8 + glTexImage2D LA8 path).
// GL_MODULATE output is provably identical either way -- the sampled texel is
// (1,1,1,coverage) in both layouts -- so no shader/TEV change; the Wii layout
// just halves atlas memory. All alloc/write/upload sites below key on these
// two constants so the platform fork stays minimal.
#ifdef FRUIT_PLATFORM_WII
static const int    kAtlasBytesPerTexel = 2;
static const GLenum kAtlasGLFormat      = GL_LUMINANCE_ALPHA;
#else
static const int    kAtlasBytesPerTexel = 4;
static const GLenum kAtlasGLFormat      = GL_RGBA;
#endif

// ASM-verified: 2026-06-14T00:00Z v1.6.1 binary @ 0x0024f568,0x002502e0,0x00250470 (asm-inspector)
// ASM-spec v1.6.1 Mortar::FontInterface::FontInterface @0x002502e0: signature fixed to
// no-arg ctor to match the binary (decompile: param_count=1, only `this`). The port's
// atlasSize is hardcoded to 512 -- the only value the single call site ever passed.
FontInterface::FontInterface()
    : m_CacheSize(100)
    , m_FontScale(1.0f)
    , m_InvFontScale(1.0f)
    , m_GlobalSizeScale(1.0f)
    , m_Size(512)
{
    // Port specific: pages are allocated lazily on first PackGlyph (binary
    // TextureAtlas @0x00269c9c starts empty; port follows the same model).
}

// Mirrors binary Initialize @ 0x00250470.
void FontInterface::InitialiseData(float fontScale, float globalSizeScale) {
    m_FontScale      = fontScale;
    m_InvFontScale   = (fontScale != 0.0f) ? (1.0f / fontScale) : 1.0f;
    m_GlobalSizeScale = globalSizeScale;
}

FontInterface::~FontInterface() {
    Clear();
}

void FontInterface::Clear() {
    for (size_t i = 0; i < m_Pages.size(); ++i) {
        FontAtlasPage* page = m_Pages[i];
        if (page->m_TextureID) {
            glDeleteTextures(1, &page->m_TextureID);
            page->m_TextureID = 0;
        }
        free(page->m_Pixels);
        page->m_Pixels = nullptr;
        delete page;
    }
    m_Pages.clear();
}

void FontInterface::EnsurePageTexture(FontAtlasPage* page) {
    if (page->m_TextureID) return;
    glGenTextures(1, &page->m_TextureID);
    glBindTexture(GL_TEXTURE_2D, page->m_TextureID);
#ifdef FRUIT_PLATFORM_WII
    // The atlas is the only glTexSubImage2D consumer -- opt it into keeping
    // its linear CPU copy (see Wii_KeepTextureLinear's header doc). Must be
    // set before the glTexImage2D call below, which consults the flag.
    Wii_KeepTextureLinear(page->m_TextureID);
#endif
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Port specific: atlas upload in kAtlasGLFormat (RGBA8 host/web, LA8 Wii);
    // GL_UNPACK_ALIGNMENT=1 is harmless and ensures correctness when row byte
    // widths are not multiples of 4.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, kAtlasGLFormat, m_Size, m_Size, 0,
                 kAtlasGLFormat, GL_UNSIGNED_BYTE, page->m_Pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
}

FontAtlasPage* FontInterface::AllocatePage() {
    FontAtlasPage* page = new FontAtlasPage();
    page->m_Pixels   = (uint8_t*)calloc((size_t)(m_Size * m_Size * kAtlasBytesPerTexel), 1);
    page->m_TextureID = 0;
    page->m_CursorX  = 0;
    page->m_CursorY  = 0;
    page->m_RowHeight = 0;
    page->m_Dirty    = false;
    page->m_DirtyX0  = 0;
    page->m_DirtyY0  = 0;
    page->m_DirtyX1  = 0;
    page->m_DirtyY1  = 0;
    EnsurePageTexture(page);
    m_Pages.push_back(page);
    LOG_INFO("FontInterface", "allocated atlas page %d (%dx%d)",
             (int)m_Pages.size() - 1, m_Size, m_Size);
    return page;
}

GLuint FontInterface::GetPageTextureID(int idx) const {
    if (idx < 0 || idx >= (int)m_Pages.size()) return 0;
    return m_Pages[idx]->m_TextureID;
}

// DIFFERS: binary TextureAtlas::AddTexture @0x00269c9c never drops glyphs;
//   on overflow it allocates a new TextureAtlasPage (256x256) and retries.
//   Port mirrors this model with 512x512 pages (kFontSupersample: 3 under
//   FN_ENABLE_HD_ASSETS, 1 otherwise -- see FontCacheObjectTTF.h).
FontAtlasPage* FontInterface::PackGlyphCell(int width, int height,
                                            const uint8_t* bitmap,
                                            int* outX, int* outY) {
    // Ensure at least one page exists.
    if (m_Pages.empty()) {
        AllocatePage();
    }

    // Port specific: inter-glyph margin = kFontSupersample + 1 (4 in HD builds,
    // 2 at ss=1). The binary CalcUVs oversamples each cell by +1 device px
    // (+kFontSupersample texels here) on the right/bottom; the margin must
    // cover that overscan so the sampled texels are transparent, never the
    // next glyph.
    const int padX = kFontSupersample + 1, padY = kFontSupersample + 1;
    FontAtlasPage* page = m_Pages.back();

    // Advance to next row if glyph doesn't fit horizontally on current page.
    if (page->m_CursorX + width + padX > m_Size) {
        page->m_CursorX  = 0;
        page->m_CursorY += page->m_RowHeight + padY;
        page->m_RowHeight = 0;
    }

    // If the current page is vertically full, allocate a new page.
    if (page->m_CursorY + height > m_Size) {
        page = AllocatePage();
    }

    // Copy glyph bitmap into the page's CPU buffer.
    // Port specific: expand 1-byte FreeType coverage to the atlas texel layout
    // (kAtlasBytesPerTexel) -- host/web RGBA (R=G=B=255, A=coverage), Wii LA8
    // (L=255, A=coverage) -- so GL_MODULATE passes the vertex colour through
    // unchanged either way.
    if (bitmap && width > 0 && height > 0) {
        for (int row = 0; row < height; row++) {
            uint8_t* dst = page->m_Pixels
                           + ((page->m_CursorY + row) * m_Size + page->m_CursorX)
                             * kAtlasBytesPerTexel;
            const uint8_t* src = bitmap + row * width;
            for (int col = 0; col < width; col++) {
#ifdef FRUIT_PLATFORM_WII
                dst[col * 2 + 0] = 255;       // L (intensity)
                dst[col * 2 + 1] = src[col];  // A (coverage)
#else
                dst[col * 4 + 0] = 255;
                dst[col * 4 + 1] = 255;
                dst[col * 4 + 2] = 255;
                dst[col * 4 + 3] = src[col];
#endif
            }
        }
    }

    MarkPageDirty(page, page->m_CursorX, page->m_CursorY, width, height);

    if (outX) *outX = page->m_CursorX;
    if (outY) *outY = page->m_CursorY;

    page->m_CursorX += width + padX;
    if (height > page->m_RowHeight) page->m_RowHeight = height;

    return page;
}

bool FontInterface::PackGlyph(int width, int height, const uint8_t* bitmap,
                               GlyphAtlasEntry* out) {
    int x = 0, y = 0;
    FontAtlasPage* page = PackGlyphCell(width, height, bitmap, &x, &y);
    if (!page) return false;

    const float invS = 1.0f / (float)m_Size;
    out->u0 = (float)x            * invS;
    out->v0 = (float)y            * invS;
    out->u1 = (float)(x + width)  * invS;
    out->v1 = (float)(y + height) * invS;
    out->pageTextureID = page->m_TextureID;
    out->page          = page;

    return true;
}

void FontInterface::BuildPendingTextures() {
    for (size_t pi = 0; pi < m_Pages.size(); ++pi) {
        FontAtlasPage* page = m_Pages[pi];
        if (!page->m_Dirty || !page->m_Pixels || !page->m_TextureID) continue;

        glBindTexture(GL_TEXTURE_2D, page->m_TextureID);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        const int dw = page->m_DirtyX1 - page->m_DirtyX0;
        const int dh = page->m_DirtyY1 - page->m_DirtyY0;
        if (dw > 0 && dh > 0) {
            uint8_t* tmp = (uint8_t*)malloc((size_t)(dw * dh * kAtlasBytesPerTexel));
            if (tmp) {
                for (int row = 0; row < dh; row++) {
                    memcpy(tmp + row * dw * kAtlasBytesPerTexel,
                           page->m_Pixels
                               + ((page->m_DirtyY0 + row) * m_Size + page->m_DirtyX0)
                                 * kAtlasBytesPerTexel,
                           (size_t)(dw * kAtlasBytesPerTexel));
                }
                glTexSubImage2D(GL_TEXTURE_2D, 0,
                                page->m_DirtyX0, page->m_DirtyY0, dw, dh,
                                kAtlasGLFormat, GL_UNSIGNED_BYTE, tmp);
                free(tmp);
            }
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        page->m_Dirty = false;
    }
}

void FontInterface::MarkPageDirty(FontAtlasPage* page, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (!page->m_Dirty) {
        page->m_DirtyX0 = x;
        page->m_DirtyY0 = y;
        page->m_DirtyX1 = x + w;
        page->m_DirtyY1 = y + h;
        page->m_Dirty   = true;
    } else {
        if (x < page->m_DirtyX0)         page->m_DirtyX0 = x;
        if (y < page->m_DirtyY0)         page->m_DirtyY0 = y;
        if (x + w > page->m_DirtyX1)     page->m_DirtyX1 = x + w;
        if (y + h > page->m_DirtyY1)     page->m_DirtyY1 = y + h;
    }
}

} // namespace Mortar
