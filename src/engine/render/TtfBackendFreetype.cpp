// FreeType implementation of the Mortar::TtfFace seam (TtfBackend.h).
//
// Moved out of FontCacheObjectTTF.cpp / FontTTFRegistry.cpp: this TU now
// owns the process-wide FT_Library (lazy static init, no explicit teardown
// -- see the GetLibrary() comment below, matching the previous
// FontTTFRegistry "never destroyed" rationale).
//
// Selected when FN_TTF_BACKEND=freetype (default, all platforms except Wii).

#include "render/TtfBackend.h"
#include "debug/Logger.h"
#include <cstring>

#include <ft2build.h>
#include FT_FREETYPE_H

namespace Mortar {

// Lazily-initialised, process-wide, never explicitly torn down.
//
// Port specific: mirrors the prior FontTTFRegistry::GetFTLibrary rationale.
// FontCacheObjectTTF instances can outlive arbitrary static-deinit ordering
// (e.g. FN::s_DebugFont); tearing down FT_Library at a static dtor risks
// running after some TtfFace has already been destroyed in an
// unpredictable order, or being torn down BEFORE a still-live face's own
// destructor calls FT_Done_Face. Leaking the library is the standard fix;
// the OS reclaims the memory at process exit.
static FT_Library GetLibrary() {
    static FT_Library s_lib = NULL;
    if (!s_lib) {
        FT_Error err = FT_Init_FreeType(&s_lib);
        if (err) {
            LOG_ERROR("TtfBackendFreetype", "FT_Init_FreeType failed (err %d)", err);
            s_lib = NULL;
        }
    }
    return s_lib;
}

struct TtfFace::Impl {
    FT_Face face;
    long    currentCharHeight; // last charHeight_26_6 passed to FT_Set_Char_Size; -1 = unset
};

TtfFace::TtfFace() : m_p(NULL) {}

TtfFace* TtfFace::Open(const char* path, int /*pixelSize*/) {
    FT_Library lib = GetLibrary();
    if (!lib) return NULL;

    FT_Face face = NULL;
    FT_Error err = FT_New_Face(lib, path, 0, &face);
    if (err) {
        LOG_ERROR("TtfBackendFreetype", "FT_New_Face failed for '%s' (err %d)", path, err);
        return NULL;
    }

    TtfFace* self = new TtfFace();
    self->m_p = new Impl();
    self->m_p->face = face;
    self->m_p->currentCharHeight = -1;
    return self;
}

TtfFace::~TtfFace() {
    if (m_p) {
        if (m_p->face) FT_Done_Face(m_p->face);
        delete m_p;
    }
}

bool TtfFace::IsValid() const {
    return m_p != NULL && m_p->face != NULL;
}

void TtfFace::SetPixelSize(long charHeight_26_6) {
    if (!IsValid()) return;
    if (charHeight_26_6 == m_p->currentCharHeight) return;
    // Port specific: charHeight_26_6 is a LITERAL pixel height at standard
    // 72dpi -- the caller (FontCacheObjectTTF::SetCharSize) has already
    // folded the binary's m_CacheSize=100 DPI factor (100/72) into the value
    // it passes here, so this seam stays backend-neutral (stb_truetype has
    // no horz_res/vert_res concept). vert_res=72 below is therefore a no-op
    // scale (FreeType's default reference dpi), reproducing the exact
    // effective pixel size the pre-refactor code got from
    // FT_Set_Char_Size(0, charHeight_26_6_raw, 0, 100).
    FT_Error err = FT_Set_Char_Size(m_p->face,
                                    /*char_width*/0,
                                    (FT_F26Dot6)charHeight_26_6,
                                    /*horz_res*/0,
                                    /*vert_res*/72);
    if (err) {
        LOG_ERROR("TtfBackendFreetype",
                  "FT_Set_Char_Size(0,%ld,0,72) failed (err %d)",
                  charHeight_26_6, err);
        return;
    }
    m_p->currentCharHeight = charHeight_26_6;
}

unsigned TtfFace::GetGlyphIndex(uint32_t cp) const {
    if (!IsValid()) return 0;
    return (unsigned)FT_Get_Char_Index(m_p->face, (FT_ULong)cp);
}

bool TtfFace::RasterizeGlyph(unsigned glyphIndex, TtfRasterGlyph& out) {
    if (!IsValid()) return false;

    // FT_Set_Transform is IDENTITY with zero delta in the binary (matrix
    // {0x10000,0,0,0x10000}) -- the glyph is rasterised at its natural
    // position and bearing lives in the metrics only. The port never calls
    // FT_Set_Transform, which is the same thing.
    FT_Error err = FT_Load_Glyph(m_p->face, (FT_UInt)glyphIndex,
                                  FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL);
    if (err) {
        LOG_ERROR("TtfBackendFreetype", "FT_Load_Glyph gi=%u err=%d", glyphIndex, err);
        return false;
    }

    FT_GlyphSlot slot = m_p->face->glyph;
    FT_Bitmap&   bm   = slot->bitmap;

    out.bitmap      = bm.buffer;
    out.width        = (int)bm.width;
    out.height       = (int)bm.rows;
    out.bitmapLeft   = slot->bitmap_left;
    out.advanceX_26_6  = (long)slot->advance.x;
    out.bearingX_26_6  = (long)slot->metrics.horiBearingX;
    out.bearingY_26_6  = (long)slot->metrics.horiBearingY;
    out.inkHeight_26_6 = (long)slot->metrics.height;

    // FT bitmap rows are tightly packed only when bm.pitch == bm.width; the
    // pitch can be larger (word-aligned). FontCacheObjectTTF's caller copies
    // row-by-row using bm.pitch directly today; to present a single flat
    // "pitch == width" buffer through this seam (per TtfRasterGlyph's
    // contract), repack into an owned buffer when the pitch differs.
    if (bm.rows > 0 && (unsigned)bm.pitch != bm.width) {
        // Reuse a scratch buffer sized to the bitmap. Single-threaded game
        // loop (see FontCacheObjectTTF.h "Thread safety" note), so a plain
        // function-local static is safe -- no thread_local needed (keeps
        // this cross-build-safe for GCC 4.4.1).
        static unsigned char* s_scratch = NULL; // never freed; process lifetime scratch
        static size_t s_scratchCap = 0;
        size_t need = (size_t)bm.width * (size_t)bm.rows;
        if (s_scratchCap < need) {
            delete[] s_scratch;
            s_scratch = new unsigned char[need];
            s_scratchCap = need;
        }
        for (unsigned int row = 0; row < bm.rows; row++) {
            memcpy(s_scratch + (size_t)row * bm.width, bm.buffer + (size_t)row * bm.pitch, bm.width);
        }
        out.bitmap = s_scratch;
    }

    return true;
}

long TtfFace::GetKerning_26_6(uint32_t a, uint32_t b) {
    if (!IsValid()) return 0;
    if (!FT_HAS_KERNING(m_p->face)) return 0;

    FT_UInt idxA = FT_Get_Char_Index(m_p->face, (FT_ULong)a);
    FT_UInt idxB = FT_Get_Char_Index(m_p->face, (FT_ULong)b);
    if (idxA == 0 || idxB == 0) return 0;

    FT_Vector kern;
    FT_Error err = FT_Get_Kerning(m_p->face, idxA, idxB, FT_KERNING_DEFAULT, &kern);
    if (err) return 0;
    return (long)kern.x;
}

long TtfFace::GetAscender_26_6() const {
    if (!IsValid()) return 0;
    return (long)m_p->face->size->metrics.ascender;
}

long TtfFace::GetDescender_26_6() const {
    if (!IsValid()) return 0;
    return (long)m_p->face->size->metrics.descender;
}

long TtfFace::GetLineHeight_26_6() const {
    if (!IsValid()) return 0;
    return (long)m_p->face->size->metrics.height;
}

} // namespace Mortar
