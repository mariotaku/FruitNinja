#ifndef FN_ENGINE_RENDER_BAKEDFONTWII_H
#define FN_ENGINE_RENDER_BAKEDFONTWII_H

// Mortar::BakedFontWii -- runtime loader for the FreeType-prebaked font
// atlases (task #51; non-Wii support added under FN_PREBAKED_FONTS).
//
// Port specific: no binary counterpart. Guarded by FN_PREBAKED_FONTS
// everywhere (was FRUIT_PLATFORM_WII-only). Two container formats, selected
// by platform:
//   Wii:      GX-tiled IA8 "<size>_pN.gxtx" pages.
//   non-Wii:  linear RGBA8888 "<size>_pN.tex" pages (lossless WebP-in-.tex,
//             R=G=B=255, A=coverage), loaded through TextureManager::Load
//             like any other game texture.
// Either way this store is the SOLE glyph + face-metric source whenever
// FN_PREBAKED_FONTS is ON -- no TTF backend (FreeType/stb_truetype) is
// compiled in alongside it (see src/engine/CMakeLists.txt); there is no
// FreeType fallback on a baked miss, matching Wii's pre-existing behaviour
// exactly (the baked glyph set is pruned, task #55, to cover every codepoint
// the game actually renders, so misses are not expected in practice).
//
// WHY: stb_truetype (the Wii TTF backend) clips CJK glyphs and breaks Korean
// jamo composition. The offline baker (tools/wii/bake-fonts.py) rasterises the
// correct glyphs with FreeType at bake time into per-(language,size,page)
// atlas pages + a metrics sidecar (.idx, IDENTICAL bytes regardless of the
// page container format). This store looks those up at runtime instead of
// rasterising, so CJK/Korean render correctly on Wii; on non-Wii it is an
// opt-in (FN_PREBAKED_FONTS, default OFF) replacement for the dynamic
// FreeType/stb path, still backed by FreeType at bake time so shapes match.
//
// LAYOUT ON DISK (see tools/wii/prebaked-font-format.md for the full spec):
//   fonts/prebaked/<lang>/<size>.idx        -- 22B FNT3 header (task #54: glyph
//                                               rects/metrics + face-level
//                                               ascender/descender/lineHeight)
//                                               + 20B*glyphCount records,
//                                               BIG-ENDIAN, sorted by codepoint.
//   fonts/prebaked/<lang>/<size>_pN.gxtx    -- Wii: GXT1 container, GX_TF_IA8,
//                                               one per atlas page.
//   fonts/prebaked/<lang>/<size>_pN.tex     -- non-Wii: lossless WebP-in-.tex,
//                                               RGBA8888 (R=G=B=255, A=coverage),
//                                               one per atlas page.
// <lang> is the baker's dir key (english_us, japanese, korean,
// traditional_chinese, ...). <size> is one of the 9 canonical sizes
// {10,12,14,16,20,22,30,50,56}; a runtime size request is snapped to the
// nearest canonical size (kSnapMap, mirrors bake_plan.json "snap_map") before
// lookup. Metrics/rects are in SUPERSAMPLED PIXELS (task #52): the baker
// rasterises each canonical LOGICAL size S at S*BAKE_SS physical px (BAKE_SS=1.5,
// the Wii EFB/logical device scale) so atlas texels ~= screen pixels after the
// 480x320->640x480 EFB upscale. BakedGlyphInfo carries the BAKE_SS read from the
// .idx header; the caller (FontCacheObjectTTF) divides the METRIC px by it to
// recover LOGICAL layout while keeping the atlas RECT in supersampled texels --
// the same crisp scheme the host uses with kFontSupersample. (BAKE_SS is baked
// into the SAME .idx on non-Wii too -- the loader does not special-case it away;
// it is simply a denser-than-1x atlas there as well.)
//
// STORE DESIGN (load / cache / evict):
//   - Lazy per (size, page): a (lang,size) .idx is parsed on first Lookup for
//     that size; a page's on-disk container is loaded + uploaded to a GL
//     texture id on the first Lookup that lands on that page.
//   - Only the ACTIVE language is kept resident. SetLanguage(flag) drops the
//     entire cache (idx + GL textures) when the flag changes, so worst-case
//     footprint is one language's set (~5MB for Chinese, acceptable).
//   - Wii: GL texture ids come from glGenTextures; pages are uploaded via
//     Wii_UploadTiledGX(GX_TF_IA8) (the same native GX upload path game
//     textures use). No FontInterface page allocation is involved -- the store
//     owns its textures directly.
//   - Non-Wii: pages are loaded via TextureManager::Load (the SmartPtr<Texture>
//     is kept resident in SizeIndex::pageTexObj so the GL texture is not
//     freed); the GL id is Texture::GetTexId(). The effect-coverage path
//     additionally decodes the page's WebP bytes directly (WebPDecodeRGBA) to
//     get CPU-side linear RGBA -- TextureManager does not retain decoded
//     pixels after upload, so this store re-decodes the file itself rather
//     than reading back GL state.
//
// LOOKUP CONTRACT: Lookup(cp, requestedSize, out) fills `out` (see
// BakedGlyphInfo) on a hit and returns true; returns false on a miss (codepoint
// not baked for the snapped size). The caller (FontCacheObjectTTF::GetGlyph)
// maps a hit onto GlyphAtlasEntry; on a miss it renders no glyph at all
// (task #54: there is no stb/FreeType rasterizer left to fall back to, on
// Wii or on a non-Wii FN_PREBAKED_FONTS build). Misses are LOG_WARN'd so
// plan-coverage gaps are diagnosable.
//
// FACE METRICS (task #54): the .idx header (FNT3) also carries FACE-LEVEL
// ascender/descender/lineHeight (captured at bake time from freetype-py's
// Face.size -- see tools/wii/bake-fonts.py face_metrics_at), in SUPERSAMPLED
// px like every other metric field. GetAscender/GetDescender/GetLineHeight
// expose them per (lang, snapped size). This lets FontCacheObjectTTF answer
// face-metric queries WITHOUT m_Face (which does not exist whenever
// FN_PREBAKED_FONTS is ON).

#if defined(FN_PREBAKED_FONTS)

#include "render/gl_funcs.h"
#include <cstdint>
#include <map>
#include <vector>

#if !defined(FRUIT_PLATFORM_WII)
#include "util/SmartPtr.h"
namespace Mortar { class Texture; }
#endif

namespace Mortar {

// One glyph's baked data, in PIXELS at the baked (native) size. `nativeSize` is
// the canonical size the request snapped to; the caller scales pixel metrics by
// requestedSize/nativeSize to produce world-unit metrics matching what the
// dynamic path would have produced at requestedSize.
struct BakedGlyphInfo {
    GLuint  pageTextureID;  // GL texture of the owning .gxtx page (0 for ink-less glyphs)
    int     atlasDim;       // page width == height, texels (from .idx header)
    int     nativeSize;     // canonical LOGICAL size this glyph snapped to (px)
    float   supersample;    // BAKE_SS from the .idx header (task #52). All px fields
                            // below (x/y/w/h rect + bearing/advance metrics) are in
                            // SUPERSAMPLED px = nativeSize*supersample. The caller
                            // divides the METRIC px (bearing/advance/glyph-world-size)
                            // by supersample to recover LOGICAL layout, but keeps the
                            // atlas RECT (x/y/w/h) in supersampled texels (denser
                            // bitmap = crisp), exactly like the host kFontSupersample.
    int     x, y;           // texel rect origin within the page (supersampled texels)
    int     w, h;           // texel rect size (supersampled texels; 0,0 for ink-less)
    int     bearingX;       // FreeType bitmap_left, SUPERSAMPLED px
    int     bearingY;       // FreeType bitmap_top, SUPERSAMPLED px
    int     advance;        // FreeType advance.x >> 6, SUPERSAMPLED px
};

class BakedFontWii {
public:
    BakedFontWii();
    ~BakedFontWii();

    // Set the active language from game_work.languageFlag. Drops the whole
    // cache (idx tables + GL textures) if the language changed. Safe to call
    // every Lookup; a no-op when the flag is unchanged.
    void SetLanguage(int languageFlag);

    // Look up a glyph. requestedSize is the raw pre-snap size (e.g. 13.0f);
    // it is snapped to the nearest canonical baked size internally. On a hit,
    // fills *out and returns true. On a miss (codepoint absent from the snapped
    // size's .idx, or the size/lang has no baked data), returns false.
    bool Lookup(uint32_t cp, float requestedSize, BakedGlyphInfo* out);

    // Coverage returned by GetGlyphCoverage: a linear 8-bit alpha bitmap of the
    // glyph's ink rect at the snapped native size, plus the same pixel metrics
    // Lookup returns. `alpha` is w*h bytes (row-major, top-down), each byte the
    // FreeType 8-bit coverage of that texel (the A byte of the IA8 texel). Empty
    // (ink-less) glyphs return w==h==0 and an empty `alpha` vector but true (the
    // advance/bearing metrics still apply).
    struct GlyphCoverage {
        std::vector<uint8_t> alpha;   // w*h bytes (SUPERSAMPLED px), 8-bit coverage, top-down
        int nativeSize;               // canonical LOGICAL size the request snapped to
        float supersample;            // BAKE_SS (task #52); w/h/bearing/advance are supersampled px
        int w, h;
        int bearingX, bearingY, advance;
    };

    // Un-tile a baked glyph's coverage from its IA8 GXTX page into a linear
    // 8-bit alpha bitmap (task #51 effect-layer path). Reverses the baker's
    // GX 4x4 IA8 tiling: for each texel of the glyph rect, reads the A byte of
    // the (A<<8)|I big-endian u16 texel from the page's tiled body. Returns
    // false on a baked miss (same conditions as Lookup) OR when the page's raw
    // .gxtx body can't be re-read. Used by FontCacheObjectTTF to run the effect
    // filters (BuildBlur/BuildStrokes) on the CORRECT baked coverage instead of
    // stb's over-sized CJK raster, so the effect layer matches the NONE base
    // layer in size + alignment.
    bool GetGlyphCoverage(uint32_t cp, float requestedSize, GlyphCoverage* out);

    // Face-level metrics (task #54), RAW SUPERSAMPLED px straight from the
    // FNT3 header, at the canonical size `requestedSize` snaps to. *outPx is
    // the raw value (ascender/descender/lineHeight); *outNativeSize is the
    // snapped canonical size and *outSupersample the BAKE_SS used to bake it
    // -- the caller (FontCacheObjectTTF) combines these with `requestedSize`
    // via the SAME `pxToWorld = inv * sizeScale / ss` formula TryBakedGlyph
    // uses for bearing/advance, so the snap-scale is applied consistently in
    // one place rather than divided out here. Returns false on a miss (no
    // baked idx for the active language/size) -- caller falls back to a
    // neutral default (matching the pre-existing no-m_Face guard behaviour)
    // rather than a stb rasterizer call, since Wii no longer links stb.
    // Outputs are left unchanged on a miss.
    bool GetAscender(float requestedSize, int* outPx, int* outNativeSize, float* outSupersample);
    bool GetDescender(float requestedSize, int* outPx, int* outNativeSize, float* outSupersample);
    bool GetLineHeight(float requestedSize, int* outPx, int* outNativeSize, float* outSupersample);

    // Free all GL textures + parsed idx tables (called on language change and
    // at teardown). Public so a font-cache Clear() can cascade if needed.
    void Clear();

private:
    // 20-byte .idx glyph record, decoded to host ints (big-endian on disk).
    struct GlyphRec {
        uint32_t cp;
        uint8_t  page;
        uint16_t x, y, w, h;
        int16_t  bearingX, bearingY;
        uint16_t advance;
    };

    // A parsed (lang,size) index: header fields + sorted glyph records + lazy
    // per-page GL texture ids (0 until the page's on-disk container is
    // uploaded). tried is set once a load has been attempted (present==false +
    // tried==true means a confirmed miss -- do not retry the file every glyph).
    struct SizeIndex {
        bool                  present;   // idx parsed OK
        bool                  tried;     // load attempted (avoid re-open on miss)
        int                   atlasDim;  // page dimension, texels
        int                   pageCount;
        float                 supersample; // BAKE_SS from the .idx header (task #52)
        // Face-level metrics (task #54), SUPERSAMPLED px, straight from the
        // FNT3 header (see tools/wii/bake-fonts.py face_metrics_at). Divided
        // by `supersample` on read in GetAscender/GetDescender/GetLineHeight.
        int16_t               ascender, descender, lineHeight;
        std::vector<GlyphRec> glyphs;    // sorted by cp
        std::vector<GLuint>   pageTex;   // [pageCount], 0 until uploaded
        std::vector<bool>     pageTried; // [pageCount], upload attempted

#if !defined(FRUIT_PLATFORM_WII)
        // Non-Wii: SmartPtr<Texture> holder per page, keeps the GL texture
        // TextureManager::Load returned alive (pageTex[p] == that texture's
        // GetTexId()). Parallel to pageTex/pageTried, [pageCount] each.
        std::vector<Mortar::SmartPtr<Mortar::Texture> > pageTexObj;
#endif

        // Lazy raw decoded pixel body per page (task #51 effect path only).
        // Wii: post-header TILED GX body (re-read because EnsurePageTexture's
        // GL upload path frees its buffer after tiling to GX). Non-Wii: LINEAR
        // decoded RGBA8888 body (WebPDecodeRGBA output; TextureManager does not
        // retain decoded pixels after upload, so this store decodes the file
        // itself). Populated on first GetGlyphCoverage that lands on the page;
        // NULL/empty until then. Kept only for pages that ever needed effect
        // coverage.
        std::vector<std::vector<uint8_t> > pageRaw;      // [pageCount], see above per-platform layout
        std::vector<bool>                  pageRawTried; // [pageCount], read attempted

        SizeIndex() : present(false), tried(false), atlasDim(0), pageCount(0),
                      supersample(1.0f), ascender(0), descender(0), lineHeight(0) {}
    };

    int m_LanguageFlag;                     // active flag; -1 = none set yet
    const char* m_LangDir;                  // baker dir name for m_LanguageFlag, or NULL
    std::map<int, SizeIndex> m_Sizes;       // canonical size -> parsed index

    // Snap requestedSize to a canonical baked size. Mirrors bake_plan.json
    // "snap_map"; returns 0 when the request is below/above the baked range in
    // a way the map doesn't cover (never happens for in-game sizes).
    static int SnapSize(float requestedSize);

    // Map a languageFlag (0..21) to the baker's dir name, or NULL if the flag
    // has no baked language set.
    static const char* LangDirFromFlag(int languageFlag);

    // Ensure the (m_LangDir, size) .idx is parsed. Returns the SizeIndex* (with
    // present=true) on success, NULL on a confirmed miss. Caches the result.
    SizeIndex* LoadSizeIndex(int size);

    // Ensure page `p` of `si` (size `size`) is uploaded; returns its GL texture
    // id (0 on failure). Caches the id. Wii: uploads the tiled GXT1 body via
    // Wii_UploadTiledGX. Non-Wii: TextureManager::Load's the "*_pN.tex"
    // WebP-in-.tex page and keeps the SmartPtr<Texture> in si->pageTexObj[page]
    // so the GL texture is not freed.
    GLuint EnsurePageTexture(SizeIndex* si, int size, int page);

    // Ensure page `page`'s raw decoded pixel body (see SizeIndex::pageRaw doc)
    // is resident for effect-coverage sampling. Returns a pointer to the bytes
    // (NULL on a read/format failure; cached either way). Wii: tiled GXT1 body
    // past the 12-byte header. Non-Wii: linear RGBA8888 (WebPDecodeRGBA).
    const std::vector<uint8_t>* EnsurePageRaw(SizeIndex* si, int size, int page);
};

} // namespace Mortar

#endif // FN_PREBAKED_FONTS

#endif // FN_ENGINE_RENDER_BAKEDFONTWII_H
