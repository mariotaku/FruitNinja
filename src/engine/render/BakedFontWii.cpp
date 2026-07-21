// Mortar::BakedFontWii -- prebaked font atlas loader (task #51; non-Wii
// support added under FN_PREBAKED_FONTS). Port specific: no binary
// counterpart. See BakedFontWii.h for the store design + on-disk format
// (Wii: GX-tiled IA8 .gxtx pages; non-Wii: linear RGBA8888 WebP-in-.tex
// pages). Not a binary struct; the binary rasterises glyphs at runtime.

#include "render/BakedFontWii.h"

#if defined(FN_PREBAKED_FONTS)

#if defined(FRUIT_PLATFORM_WII)
#include <gccore.h>               // GX_TF_IA8; must precede gl_funcsWii.h (GXTexObj)
#include "render/gl_funcsWii.h"   // Wii_UploadTiledGX
#else
#include "asset/Texture.h"
#include "asset/TextureManager.h"
#include "webp/decode.h"          // WebPGetInfo/WebPDecodeRGBA/WebPFree -- effect-coverage re-decode
#endif

#include "render/gl_funcs.h"
#include "asset/File.h"
#include "debug/Logger.h"
#if defined(FN_BLOCK_PRELOAD)
#include "resource/ResBlock.h"
#endif

#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace Mortar {

// Canonical baked sizes (mirrors bake_plan.json "canonical_sizes"). Any runtime
// request snaps to the NEAREST of these (ties -> the larger size), which
// reproduces bake_plan.json "snap_map" exactly for its listed request sizes
// (8/9/9.9/10 -> 10, 13 -> 14, 17 -> 16, ...) and generalises to arbitrary
// floats. kFontSupersample is 1 on Wii, so these are literal pixel sizes.
static const int kCanonicalSizes[] = { 10, 12, 14, 16, 20, 22, 30, 50, 56 };
static const int kCanonicalCount   = 9;

#if defined(FRUIT_PLATFORM_WII)
// GXT1 container header (BIG-ENDIAN, Wii native), 12 bytes -- same layout the
// game-texture GXTX reader (TextureFileFormat::ReadGxtx) parses. Fields read
// via explicit byte assembly so no runtime byte-swap is needed (the file is
// already Wii-native big-endian).
static const int kGxtxHeaderSize = 12;
#endif

// .idx header: 22 bytes (FNT3, task #54 -- FNT2's 16-byte header + 3 s16 face
// metrics), BIG-ENDIAN.
static const int kIdxHeaderSize  = 22;
// .idx glyph record: 20 bytes.
static const int kIdxRecordSize  = 20;

// --- BE readers (files are Wii-native big-endian) -----------------------------
static inline uint16_t ReadBE16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}
static inline uint32_t ReadBE32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

BakedFontWii::BakedFontWii()
    : m_LanguageFlag(-1)
    , m_LangDir(NULL)
{
}

BakedFontWii::~BakedFontWii() {
    Clear();
}

// Snap requestedSize to the nearest canonical baked size (ties -> larger).
int BakedFontWii::SnapSize(float requestedSize) {
    if (requestedSize <= 0.0f) return 0;
    int best     = kCanonicalSizes[0];
    float bestAbs = requestedSize - (float)best;
    if (bestAbs < 0.0f) bestAbs = -bestAbs;
    for (int i = 1; i < kCanonicalCount; ++i) {
        int c   = kCanonicalSizes[i];
        float d = requestedSize - (float)c;
        if (d < 0.0f) d = -d;
        // "<" -> ties keep the earlier (smaller) size; use "<=" so a tie moves
        // to the LARGER size (matches snap_map: 13 -> 14, equidistant 12/14).
        if (d <= bestAbs) { bestAbs = d; best = c; }
    }
    return best;
}

// languageFlag (0..21, StringTable order) -> baker dir name (tools/wii/
// bake-fonts.py / bake_plan.json "plan" keys). Multi-word langs use the
// baker's underscore-and-no-paren spelling, which DIFFERS from
// StringTable::kLanguageSuffix ("traditional chinese" / "portuguese (pt)").
// Flags with no baked set (dutch/swedish/danish/norwegian/finnish -- Latin
// scripts stb handles correctly) return NULL so the caller falls back to stb.
const char* BakedFontWii::LangDirFromFlag(int languageFlag) {
    switch (languageFlag) {
        case 0:  return "english_us";
        case 1:  return "english_uk";
        case 2:  return "french";
        case 3:  return "spanish";
        case 4:  return "german";
        case 5:  return "italian";
        // 6 dutch, 7 swedish, 8 danish, 9 norwegian, 10 finnish: not baked.
        case 11: return "korean";
        case 12: return "japanese";
        case 13: return "chinese";
        case 14: return "traditional_chinese";
        case 15: return "latin_spanish";
        case 16: return "polish";
        case 17: return "portuguese_pt";
        case 18: return "portuguese_br";
        case 19: return "russian";
        case 20: return "arabic";
        default: return NULL;
    }
}

void BakedFontWii::SetLanguage(int languageFlag) {
    if (languageFlag == m_LanguageFlag) return;
    Clear();
    m_LanguageFlag = languageFlag;
    m_LangDir      = LangDirFromFlag(languageFlag);
}

void BakedFontWii::Clear() {
    for (std::map<int, SizeIndex>::iterator it = m_Sizes.begin();
         it != m_Sizes.end(); ++it) {
        SizeIndex& si = it->second;
#if defined(FRUIT_PLATFORM_WII)
        // Wii: this store owns the GL texture directly (glGenTextures in
        // EnsurePageTexture) -- delete it here.
        for (size_t p = 0; p < si.pageTex.size(); ++p) {
            if (si.pageTex[p]) {
                GLuint id = si.pageTex[p];
                glDeleteTextures(1, &id);
                si.pageTex[p] = 0;
            }
        }
#else
        // Non-Wii: the GL texture is owned by the Texture object
        // (TextureManager::Load), which deletes it in its own destructor --
        // just drop our SmartPtr refs, never glDeleteTextures it ourselves
        // (that would race/double-delete against Texture::~Texture).
        si.pageTexObj.clear();
        for (size_t p = 0; p < si.pageTex.size(); ++p) {
            si.pageTex[p] = 0;
        }
#endif
    }
    m_Sizes.clear();
}

// Parse fonts/prebaked/<lang>/<size>.idx into a SizeIndex. Returns the cached
// SizeIndex* on success, NULL on a confirmed miss (result is cached either way
// so a miss doesn't re-open the file every glyph).
BakedFontWii::SizeIndex* BakedFontWii::LoadSizeIndex(int size) {
    SizeIndex& si = m_Sizes[size];
    if (si.tried) return si.present ? &si : NULL;
    si.tried = true;

    if (!m_LangDir || size <= 0) return NULL;

    char path[256];
    snprintf(path, sizeof(path), "fonts/prebaked/%s/%d.idx", m_LangDir, size);

    File file(path, 0, 0);
    if (!file.Open()) {
        LOG_WARN("BakedFontWii", "no prebaked idx '%s'", path);
        return NULL;
    }
    unsigned long fileSize = file.Size();
    if (fileSize < (unsigned long)kIdxHeaderSize) {
        LOG_WARN("BakedFontWii", "idx '%s' too small (%lu)", path, fileSize);
        return NULL;
    }

    uint8_t* buf = (uint8_t*)malloc(fileSize);
    if (!buf) return NULL;
    if (!file.Read(buf, fileSize)) {
        free(buf);
        LOG_WARN("BakedFontWii", "idx '%s' read failed", path);
        return NULL;
    }

    // Header (22 bytes, FNT3 task #54): magic "FNT3", u16 atlasDim, u8
    // pageCount, u8 reserved, u32 glyphCount, u16 supersample_8_8,
    // u16 reserved2, s16 ascender, s16 descender, s16 lineHeight. Task #52
    // bumped FNT1->FNT2 (records now SUPERSAMPLED px); task #54 bumps
    // FNT2->FNT3 (adds face-level metrics, see BakedFontWii.h). Reject FNT1/
    // FNT2 -- an old-format atlas read by this loader would either render
    // 1.5x too big (FNT1) or misparse the glyph table at the wrong offset
    // (FNT2, which is 6 bytes shorter); re-bake required either way.
    if (buf[0] != 'F' || buf[1] != 'N' || buf[2] != 'T' || buf[3] != '3') {
        free(buf);
        LOG_WARN("BakedFontWii", "idx '%s' bad magic (need FNT3 -- re-bake fonts)", path);
        return NULL;
    }
    int      atlasDim   = (int)ReadBE16(buf + 4);
    int      pageCount  = (int)buf[6];
    uint32_t glyphCount = ReadBE32(buf + 8);
    // supersample: 8.8 fixed-point at offset 12 (value = round(BAKE_SS*256)).
    int      ssFixed    = (int)ReadBE16(buf + 12);
    float    supersample = (ssFixed > 0) ? ((float)ssFixed / 256.0f) : 1.0f;
    int16_t  ascender    = (int16_t)ReadBE16(buf + 16);
    int16_t  descender   = (int16_t)ReadBE16(buf + 18);
    int16_t  lineHeight  = (int16_t)ReadBE16(buf + 20);

    unsigned long need = (unsigned long)kIdxHeaderSize
                       + (unsigned long)glyphCount * (unsigned long)kIdxRecordSize;
    if (pageCount <= 0 || glyphCount == 0 || fileSize < need) {
        free(buf);
        LOG_WARN("BakedFontWii", "idx '%s' truncated (pages=%d glyphs=%u size=%lu)",
                 path, pageCount, glyphCount, fileSize);
        return NULL;
    }

    si.atlasDim    = atlasDim;
    si.pageCount   = pageCount;
    si.supersample = supersample;
    si.ascender    = ascender;
    si.descender   = descender;
    si.lineHeight  = lineHeight;
    si.pageTex.assign((size_t)pageCount, 0);
    si.pageTried.assign((size_t)pageCount, false);
#if !defined(FRUIT_PLATFORM_WII)
    si.pageTexObj.assign((size_t)pageCount, Mortar::SmartPtr<Mortar::Texture>());
#endif
    si.pageRaw.assign((size_t)pageCount, std::vector<uint8_t>());
    si.pageRawTried.assign((size_t)pageCount, false);
    si.glyphs.reserve(glyphCount);

    const uint8_t* rec = buf + kIdxHeaderSize;
    for (uint32_t g = 0; g < glyphCount; ++g, rec += kIdxRecordSize) {
        GlyphRec r;
        r.cp       = ReadBE32(rec + 0);
        r.page     = rec[4];
        // rec[5] reserved
        r.x        = ReadBE16(rec + 6);
        r.y        = ReadBE16(rec + 8);
        r.w        = ReadBE16(rec + 10);
        r.h        = ReadBE16(rec + 12);
        r.bearingX = (int16_t)ReadBE16(rec + 14);
        r.bearingY = (int16_t)ReadBE16(rec + 16);
        r.advance  = ReadBE16(rec + 18);
        si.glyphs.push_back(r);
    }
    free(buf);

    si.present = true;
#if defined(FN_BLOCK_PRELOAD)
    // Task #36 Stage 1 -- fail-loud instrumentation (log-only; no preload yet,
    // see tmp/wii/loader-blueprint.md section 6/7). Augments this pre-existing
    // load log (rather than adding a parallel [BlockLoad] line) since this IS
    // the once-per-(lang,size) disk-load point already gated by si.tried above.
    LOG_INFO("BlockLoad", "[BlockLoad] block=%s loading %s (FONT)",
             fn::wii::GetCurrentBlockName(), path);
#endif
    LOG_INFO("BakedFontWii", "loaded '%s': %u glyphs, %d page(s), atlas %d, ss %.3f",
             path, glyphCount, pageCount, atlasDim, supersample);
    return &si;
}

// Load + upload page `page` of the (m_LangDir, size) set to a GL texture.
// Returns its GL texture id (0 on failure; cached so we don't retry a bad page
// every glyph).
GLuint BakedFontWii::EnsurePageTexture(SizeIndex* si, int size, int page) {
    if (!si || page < 0 || page >= si->pageCount) return 0;
    if (si->pageTex[page]) return si->pageTex[page];
    if (si->pageTried[page]) return 0;
    si->pageTried[page] = true;

#if defined(FRUIT_PLATFORM_WII)
    char path[256];
    snprintf(path, sizeof(path), "fonts/prebaked/%s/%d_p%d.gxtx",
             m_LangDir, size, page);

    File file(path, 0, 0);
    if (!file.Open()) {
        LOG_WARN("BakedFontWii", "no prebaked page '%s'", path);
        return 0;
    }
    unsigned long fileSize = file.Size();
    if (fileSize < (unsigned long)kGxtxHeaderSize) {
        LOG_WARN("BakedFontWii", "page '%s' too small (%lu)", path, fileSize);
        return 0;
    }
    uint8_t* buf = (uint8_t*)malloc(fileSize);
    if (!buf) return 0;
    if (!file.Read(buf, fileSize)) {
        free(buf);
        LOG_WARN("BakedFontWii", "page '%s' read failed", path);
        return 0;
    }

    // GXT1 header: magic, u16be w, u16be h, u8 gxFormat (must be GX_TF_IA8),
    // u8 version=1, u16 reserved.
    if (buf[0] != 'G' || buf[1] != 'X' || buf[2] != 'T' || buf[3] != '1'
        || buf[8] != (uint8_t)GX_TF_IA8 || buf[9] != 1) {
        free(buf);
        LOG_WARN("BakedFontWii", "page '%s' bad GXT1/IA8 header", path);
        return 0;
    }
    int w = (int)ReadBE16(buf + 4);
    int h = (int)ReadBE16(buf + 6);
    if (w <= 0 || h <= 0) {
        free(buf);
        LOG_WARN("BakedFontWii", "page '%s' bad dims %dx%d", path, w, h);
        return 0;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (!tex) {
        free(buf);
        return 0;
    }
    // Direct native upload of the pre-tiled IA8 body (offset 12); no runtime
    // decode/tile -- same path game textures take (Wii_UploadTiledGX).
    Wii_UploadTiledGX(tex, buf + kGxtxHeaderSize,
                      (unsigned int)(fileSize - kGxtxHeaderSize),
                      w, h, GX_TF_IA8);
    free(buf);

    si->pageTex[page] = tex;
    return tex;
#else
    // Non-Wii: page is an ordinary lossless-WebP-in-.tex game texture --
    // TextureManager::Load handles the decode + GL upload (via the existing
    // ReadWebP reader, TextureFileFormat.cpp) exactly like any other .tex.
    // Keep the SmartPtr resident (si->pageTexObj) so the GL texture is not
    // freed once TextureManager's own cache entry would otherwise expire.
    char path[256];
    snprintf(path, sizeof(path), "fonts/prebaked/%s/%d_p%d.tex",
             m_LangDir, size, page);

    Mortar::SmartPtr<Mortar::Texture> tex = Mortar::TextureManager::GetInstance().Load(path);
    if (!tex.IsValid()) {
        LOG_WARN("BakedFontWii", "no prebaked page '%s'", path);
        return 0;
    }
    GLuint id = tex->GetTexId();
    if (!id) {
        LOG_WARN("BakedFontWii", "page '%s' loaded but has no GL texture id", path);
        return 0;
    }

    si->pageTexObj[(size_t)page] = tex;
    si->pageTex[page] = id;
    return id;
#endif
}

bool BakedFontWii::Lookup(uint32_t cp, float requestedSize, BakedGlyphInfo* out) {
    if (!out || !m_LangDir) return false;

    int size = SnapSize(requestedSize);
    if (size <= 0) return false;

    SizeIndex* si = LoadSizeIndex(size);
    if (!si) return false;

    // Binary search cp in the sorted glyph records.
    int lo = 0, hi = (int)si->glyphs.size() - 1;
    int found = -1;
    while (lo <= hi) {
        int mid = lo + ((hi - lo) >> 1);
        uint32_t mcp = si->glyphs[(size_t)mid].cp;
        if (mcp == cp)      { found = mid; break; }
        else if (mcp < cp)  lo = mid + 1;
        else                hi = mid - 1;
    }
    if (found < 0) {
        // Glyph absent from a present index = a real plan-coverage gap (the
        // subset for this size didn't include a codepoint the game renders).
        // Task #54: no stb fallback on Wii -- a miss means the glyph does not
        // draw (control chars like U+000A are expected; a visible miss means the
        // bake set needs the cp added). Bounded: GetGlyph caches the empty entry,
        // so this fires at most once per unique (cp, size), not per-frame.
        LOG_WARN("BakedFontWii", "miss cp=U+%04X size=%d lang=%s (no glyph -- add to bake if visible)",
                 (unsigned)cp, size, m_LangDir);
        return false;
    }

    const GlyphRec& r = si->glyphs[(size_t)found];

    out->atlasDim    = si->atlasDim;
    out->nativeSize  = size;
    out->supersample = si->supersample;
    out->x          = (int)r.x;
    out->y          = (int)r.y;
    out->w          = (int)r.w;
    out->h          = (int)r.h;
    out->bearingX   = (int)r.bearingX;
    out->bearingY   = (int)r.bearingY;
    out->advance    = (int)r.advance;

    if (r.w == 0 || r.h == 0) {
        // Ink-less glyph (e.g. space): valid record, no atlas sample. Advance
        // still applies. pageTextureID 0 signals "no ink".
        out->pageTextureID = 0;
        return true;
    }

    GLuint tex = EnsurePageTexture(si, size, (int)r.page);
    if (!tex) {
        // Page upload failed -- treat as a miss so the caller falls back rather
        // than sampling a zero texture.
        return false;
    }
    out->pageTextureID = tex;
    return true;
}

// Face-level metrics (task #54). Shared snap+load path with Lookup. Returned
// RAW in SUPERSAMPLED px (bg.nativeSize/bg.supersample also reported via
// *outNativeSize/*outSupersample) -- deliberately NOT pre-divided to LOGICAL
// px here, so the caller (FontCacheObjectTTF) applies the exact same
// `pxToWorld = inv * sizeScale / ss` formula it already uses for every other
// baked metric (bearing/advance/width -- see TryBakedGlyph), keeping the
// snap-scale (requestedSize/nativeSize) applied consistently in one place.
bool BakedFontWii::GetAscender(float requestedSize, int* outPx, int* outNativeSize, float* outSupersample) {
    int size = SnapSize(requestedSize);
    if (size <= 0) return false;
    SizeIndex* si = LoadSizeIndex(size);
    if (!si) return false;
    *outPx = (int)si->ascender;
    *outNativeSize = size;
    *outSupersample = (si->supersample > 0.0f) ? si->supersample : 1.0f;
    return true;
}

bool BakedFontWii::GetDescender(float requestedSize, int* outPx, int* outNativeSize, float* outSupersample) {
    int size = SnapSize(requestedSize);
    if (size <= 0) return false;
    SizeIndex* si = LoadSizeIndex(size);
    if (!si) return false;
    *outPx = (int)si->descender;
    *outNativeSize = size;
    *outSupersample = (si->supersample > 0.0f) ? si->supersample : 1.0f;
    return true;
}

bool BakedFontWii::GetLineHeight(float requestedSize, int* outPx, int* outNativeSize, float* outSupersample) {
    int size = SnapSize(requestedSize);
    if (size <= 0) return false;
    SizeIndex* si = LoadSizeIndex(size);
    if (!si) return false;
    *outPx = (int)si->lineHeight;
    *outNativeSize = size;
    *outSupersample = (si->supersample > 0.0f) ? si->supersample : 1.0f;
    return true;
}

// Re-read + cache page `page`'s raw tiled .gxtx body (past the 12-byte GXT1
// header) for effect-coverage un-tiling. EnsurePageTexture frees its buffer
// after GX upload, so the effect path can't sample from GX memory portably --
// it re-reads the file and keeps the tiled bytes. Cached per (size,page);
// tried-once so a bad page doesn't re-open every glyph.
const std::vector<uint8_t>* BakedFontWii::EnsurePageRaw(SizeIndex* si, int size,
                                                        int page) {
    if (!si || page < 0 || page >= si->pageCount) return NULL;
    if (!si->pageRaw[page].empty()) return &si->pageRaw[page];
    if (si->pageRawTried[page]) return NULL;
    si->pageRawTried[page] = true;

#if defined(FRUIT_PLATFORM_WII)
    char path[256];
    snprintf(path, sizeof(path), "fonts/prebaked/%s/%d_p%d.gxtx",
             m_LangDir, size, page);

    File file(path, 0, 0);
    if (!file.Open()) {
        LOG_WARN("BakedFontWii", "no prebaked page '%s' (effect coverage)", path);
        return NULL;
    }
    unsigned long fileSize = file.Size();
    if (fileSize <= (unsigned long)kGxtxHeaderSize) {
        LOG_WARN("BakedFontWii", "page '%s' too small for coverage (%lu)", path, fileSize);
        return NULL;
    }
    uint8_t* buf = (uint8_t*)malloc(fileSize);
    if (!buf) return NULL;
    if (!file.Read(buf, fileSize)) {
        free(buf);
        LOG_WARN("BakedFontWii", "page '%s' read failed (effect coverage)", path);
        return NULL;
    }
    if (buf[0] != 'G' || buf[1] != 'X' || buf[2] != 'T' || buf[3] != '1'
        || buf[8] != (uint8_t)GX_TF_IA8 || buf[9] != 1) {
        free(buf);
        LOG_WARN("BakedFontWii", "page '%s' bad GXT1/IA8 header (coverage)", path);
        return NULL;
    }

    si->pageRaw[page].assign(buf + kGxtxHeaderSize, buf + fileSize);
    free(buf);
    return &si->pageRaw[page];
#else
    // Non-Wii: re-decode the page's WebP-in-.tex bytes directly (WebPDecodeRGBA)
    // to get CPU-side LINEAR RGBA8888 -- TextureManager doesn't retain decoded
    // pixels after the GL upload (see Texture.cpp's UploadTex1ToGL), so this
    // store reads the file itself rather than reading back GL state. Coverage
    // is the ALPHA channel (byte 3 of each 4-byte texel) -- same convention the
    // bake writes (encode_tex_page: R=G=B=255, A=coverage).
    char path[256];
    snprintf(path, sizeof(path), "fonts/prebaked/%s/%d_p%d.tex",
             m_LangDir, size, page);

    File file(path, 0, 0);
    if (!file.Open()) {
        LOG_WARN("BakedFontWii", "no prebaked page '%s' (effect coverage)", path);
        return NULL;
    }
    unsigned long fileSize = file.Size();
    if (fileSize == 0) {
        LOG_WARN("BakedFontWii", "page '%s' empty (effect coverage)", path);
        return NULL;
    }
    uint8_t* buf = (uint8_t*)malloc(fileSize);
    if (!buf) return NULL;
    if (!file.Read(buf, fileSize)) {
        free(buf);
        LOG_WARN("BakedFontWii", "page '%s' read failed (effect coverage)", path);
        return NULL;
    }

    int w = 0, h = 0;
    if (WebPGetInfo(buf, (size_t)fileSize, &w, &h) == 0) {
        free(buf);
        LOG_WARN("BakedFontWii", "page '%s' bad WebP header (coverage)", path);
        return NULL;
    }
    uint8_t* rgba = WebPDecodeRGBA(buf, (size_t)fileSize, &w, &h);
    free(buf);
    if (!rgba) {
        LOG_WARN("BakedFontWii", "page '%s' WebP decode failed (coverage)", path);
        return NULL;
    }

    si->pageRaw[page].assign(rgba, rgba + (size_t)w * (size_t)h * 4);
    WebPFree(rgba);
    return &si->pageRaw[page];
#endif
}

// Un-tile the glyph's ink rect from the IA8 GXTX page into a linear 8-bit
// alpha bitmap (reverse of gl_funcsWii.cpp TileRegion's IA8 branch). GX_TF_IA8
// tiles the page in 4x4-texel blocks: for a page-texel (px,py),
//   tileIndex = (py/4)*tilesPerRow + (px/4),  tilesPerRow = ceil(atlasDim/4)
//   in-tile  = ((py%4)*4 + (px%4)) * 2 bytes (BE u16 = (A<<8)|I; A byte first)
// so the coverage is the FIRST byte of the texel's 2-byte pair. We read only
// the glyph's [x,x+w) x [y,y+h) rect into a w*h top-down alpha buffer.
bool BakedFontWii::GetGlyphCoverage(uint32_t cp, float requestedSize,
                                    GlyphCoverage* out) {
    if (!out || !m_LangDir) return false;

    int size = SnapSize(requestedSize);
    if (size <= 0) return false;

    SizeIndex* si = LoadSizeIndex(size);
    if (!si) return false;

    // Binary search cp (records sorted ascending) -- same as Lookup.
    int lo = 0, hi = (int)si->glyphs.size() - 1;
    int found = -1;
    while (lo <= hi) {
        int mid = lo + ((hi - lo) >> 1);
        uint32_t mcp = si->glyphs[(size_t)mid].cp;
        if (mcp == cp)      { found = mid; break; }
        else if (mcp < cp)  lo = mid + 1;
        else                hi = mid - 1;
    }
    if (found < 0) return false;   // Lookup already LOG_WARNs the coverage-gap case

    const GlyphRec& r = si->glyphs[(size_t)found];

    out->alpha.clear();
    out->nativeSize  = size;
    out->supersample = si->supersample;
    out->w          = (int)r.w;
    out->h          = (int)r.h;
    out->bearingX   = (int)r.bearingX;
    out->bearingY   = (int)r.bearingY;
    out->advance    = (int)r.advance;

    if (r.w == 0 || r.h == 0) {
        // Ink-less glyph (space): no coverage, but metrics valid.
        return true;
    }

    const std::vector<uint8_t>* raw = EnsurePageRaw(si, size, (int)r.page);
    if (!raw) return false;

    const int gw = (int)r.w, gh = (int)r.h;
    const int gx = (int)r.x, gy = (int)r.y;
    out->alpha.assign((size_t)gw * (size_t)gh, 0);

#if defined(FRUIT_PLATFORM_WII)
    const int atlasDim    = si->atlasDim;
    const int tilesPerRow = (atlasDim + 3) / 4;
    const uint8_t* body   = &(*raw)[0];
    const size_t   bodyN  = raw->size();

    for (int ry = 0; ry < gh; ++ry) {
        const int py = gy + ry;
        for (int rx = 0; rx < gw; ++rx) {
            const int px = gx + rx;
            const int tileIndex = (py / 4) * tilesPerRow + (px / 4);
            const int inTile    = ((py & 3) * 4 + (px & 3)) * 2;
            const size_t off    = (size_t)tileIndex * 32 + (size_t)inTile;
            // off+0 is the A (coverage) byte; off+1 is I (intensity).
            if (off < bodyN) {
                out->alpha[(size_t)ry * gw + rx] = body[off];
            }
        }
    }
#else
    // Non-Wii: raw is LINEAR decoded RGBA8888 (EnsurePageRaw), atlasDim texels
    // per row -- no detiling needed, just read the ALPHA (coverage) channel of
    // each texel in the glyph's rect.
    const int atlasDim   = si->atlasDim;
    const uint8_t* body  = &(*raw)[0];
    const size_t   bodyN = raw->size();

    for (int ry = 0; ry < gh; ++ry) {
        const int py = gy + ry;
        for (int rx = 0; rx < gw; ++rx) {
            const int px = gx + rx;
            const size_t off = ((size_t)py * (size_t)atlasDim + (size_t)px) * 4 + 3;
            if (off < bodyN) {
                out->alpha[(size_t)ry * gw + rx] = body[off];
            }
        }
    }
#endif
    return true;
}

} // namespace Mortar

#endif // FN_PREBAKED_FONTS
