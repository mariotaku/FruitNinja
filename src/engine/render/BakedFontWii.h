#ifndef FN_ENGINE_RENDER_BAKEDFONTWII_H
#define FN_ENGINE_RENDER_BAKEDFONTWII_H

// Mortar::BakedFontWii -- Wii-only runtime loader for the FreeType-prebaked
// IA8 GXTX font atlases (task #51).
//
// Port specific: Wii-only. Guarded by FRUIT_PLATFORM_WII everywhere. On host/
// web the dynamic FreeType/stb atlas (FontCacheObjectTTF + FontInterface) is
// used unchanged; this store does not exist there.
//
// WHY: stb_truetype (the Wii TTF backend) clips CJK glyphs and breaks Korean
// jamo composition. The offline baker (tools/wii/bake-fonts.py) rasterises the
// correct glyphs with FreeType at bake time into per-(language,size,page) IA8
// GX-tiled atlas pages (.gxtx) + a metrics sidecar (.idx). This store looks
// those up at runtime instead of rasterising, so CJK/Korean render correctly.
//
// LAYOUT ON DISK (see tools/wii/prebaked-font-format.md for the full spec):
//   fonts/prebaked/<lang>/<size>.idx        -- 22B FNT3 header (task #54: glyph
//                                               rects/metrics + face-level
//                                               ascender/descender/lineHeight)
//                                               + 20B*glyphCount records,
//                                               BIG-ENDIAN, sorted by codepoint.
//   fonts/prebaked/<lang>/<size>_pN.gxtx    -- GXT1 container, GX_TF_IA8, one
//                                               per atlas page.
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
// the same crisp scheme the host uses with kFontSupersample.
//
// STORE DESIGN (load / cache / evict):
//   - Lazy per (size, page): a (lang,size) .idx is parsed on first Lookup for
//     that size; a page's .gxtx is loaded + uploaded to a GL texture id on the
//     first Lookup that lands on that page.
//   - Only the ACTIVE language is kept resident. SetLanguage(flag) drops the
//     entire cache (idx + GL textures) when the flag changes, so worst-case
//     footprint is one language's set (~5MB for Chinese, acceptable).
//   - GL texture ids come from glGenTextures; pages are uploaded via
//     Wii_UploadTiledGX(GX_TF_IA8) (the same native GX upload path game
//     textures use). No FontInterface page allocation is involved -- the store
//     owns its textures directly.
//
// LOOKUP CONTRACT: Lookup(cp, requestedSize, out) fills `out` (see
// BakedGlyphInfo) on a hit and returns true; returns false on a miss (codepoint
// not baked for the snapped size). The caller (FontCacheObjectTTF::GetGlyph on
// Wii) maps a hit onto GlyphAtlasEntry; on a miss it renders no glyph at all
// (task #54: there is no stb/FreeType rasterizer left to fall back to on
// Wii). Misses are LOG_WARN'd so plan-coverage gaps are diagnosable.
//
// FACE METRICS (task #54): the .idx header (FNT3) also carries FACE-LEVEL
// ascender/descender/lineHeight (captured at bake time from freetype-py's
// Face.size -- see tools/wii/bake-fonts.py face_metrics_at), in SUPERSAMPLED
// px like every other metric field. GetAscender/GetDescender/GetLineHeight
// expose them per (lang, snapped size). This lets FontCacheObjectTTF answer
// face-metric queries (e.g. Font.cpp's DrawStringTTF ascent-based pen shift)
// WITHOUT m_Face, which is what makes dropping the runtime TTF open (and
// stb_truetype entirely) possible on Wii.

#if defined(FRUIT_PLATFORM_WII)

#include "render/gl_funcs.h"
#include <cstdint>
#include <map>
#include <vector>

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
    // per-page GL texture ids (0 until the page's .gxtx is uploaded). tried is
    // set once a load has been attempted (present==false + tried==true means a
    // confirmed miss -- do not retry the file every glyph).
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

        // Lazy raw tiled .gxtx body per page (task #51 effect path only). The
        // GL-upload path (EnsurePageTexture) frees its buffer after tiling to
        // GX, so the effect-coverage un-tiler must re-read + keep the tiled
        // body to sample individual glyph rects. Populated on first
        // GetGlyphCoverage that lands on the page; NULL/empty until then.
        // Kept only for pages that ever needed effect coverage.
        std::vector<std::vector<uint8_t> > pageRaw;      // [pageCount], tiled body (post-header)
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
    // id (0 on failure). Caches the id.
    GLuint EnsurePageTexture(SizeIndex* si, int size, int page);

    // Ensure page `page`'s raw tiled .gxtx body (past the 12-byte header) is
    // resident in si->pageRaw for effect-coverage un-tiling. Returns a pointer
    // to the tiled bytes (NULL on a read/format failure; cached either way).
    // Also outputs the page's texel dimensions via *pageDim (== atlasDim).
    const std::vector<uint8_t>* EnsurePageRaw(SizeIndex* si, int size, int page);
};

} // namespace Mortar

#endif // FRUIT_PLATFORM_WII

#endif // FN_ENGINE_RENDER_BAKEDFONTWII_H
