#ifndef FN_ENGINE_RENDER_FONTINTERFACE_H
#define FN_ENGINE_RENDER_FONTINTERFACE_H

// Mortar::FontInterface — dynamic multi-page glyph atlas backed by GL textures.
//
// Port specific: not a binary struct. The binary's TTF path uses a Samsung Bada
// framework glyph-cache API (IFont/IGlyphCache) that is not portable. This class
// provides an equivalent portable interface.
//
// DIFFERS: binary uses TextureAtlas @0x00269c9c with std::vector<TextureAtlasPage*>
//   m_Pages; 256x256 RGBA pages; port uses 512x512 pages (kFontSupersample is
//   HD-gated: 3 under FN_ENABLE_HD_ASSETS, 1 otherwise -- see FontCacheObjectTTF.h).
//
// Multi-page model (faithful to binary TextureAtlas @0x00269c9c):
//   PackGlyphCell packs onto the CURRENT page (m_Pages.back()) and allocates a
//   NEW page only when that page overflows -- no cross-page best-fit search.
//   Glyphs are NEVER dropped. Each GlyphAtlasEntry carries a pageTextureID so
//   callers can bind the right GL texture per glyph without holding a
//   FontInterface pointer.
//
// Wii-only (FRUIT_PLATFORM_WII, task #60): PackGlyphCell instead best-fits
//   across ALL existing pages (backfilling whatever room earlier strings left
//   behind) before allocating a new one, and a BeginGlyphRun/EndGlyphRun scope
//   pins packing to a single reserved page for the run's duration so one
//   string's glyphs never straddle a page boundary (fixed a visible
//   stroke/blur discontinuity mid-string on Wii's software TTF path). This is
//   a Wii-specific behavior change, NOT part of the binary-faithful baseline
//   -- host/web/asm-verify keep the original last-page-only logic above.
//
// Port specific: glyph atlas is RGBA (white + coverage-alpha) so GL_MODULATE
//   yields vertex-coloured text. Binary used Bada IFont with an RGBA atlas.
//   On Wii (FRUIT_PLATFORM_WII) the atlas is instead LUMINANCE_ALPHA (2 B/texel:
//   L=255, A=coverage), uploaded as GX_TF_IA8 -- GL_MODULATE output is identical
//   ((1,1,1,coverage) either way) at half the memory. See FontInterface.cpp's
//   kAtlasBytesPerTexel / kAtlasGLFormat.
//
// Scaling constants mirror binary FontInterface ctor @ 0x002502e0 and
// Initialize @ 0x00250470:
//   m_CacheSize      = 100  (used as FT DPI in FT_Set_Char_Size; set in ctor)
//   m_FontScale      = 1.0  (super-sampling factor; set in Initialize)
//   m_InvFontScale   = 1.0  (1/m_FontScale; set in Initialize)
//   m_GlobalSizeScale = 1.0 (0.9 when game language byte == 0x13; set in Initialize)
//
// TODO: bevel/stroke/glow effects — stub no-ops for now.

#include "render/gl_funcs.h"
#include <cstdint>
#include <vector>

namespace Mortar {

// Per-page atlas state. Owned by FontInterface (vector of pointers).
// One instance per allocated atlas page; all pages share the same m_Size.
// v1.6.1 binary entity: Mortar::TextureAtlasPage (confirmed via
// BakedStringTTF::FindOrCreateSurface @0x00248b9c mangled signature, which takes
// TextureAtlasPage* -- same type Mesh.h forward-declares as an opaque atlas pointer
// for Mesh::DrawTriList/DrawTriStrip/DrawTris). FontAtlasPage is a back-compat alias.
struct TextureAtlasPage {
    uint8_t* m_Pixels;       // texel buffer [pageSize*pageSize*bytesPerTexel], calloc'd
                             // (RGBA8 host/web; LA8 on Wii -- see FontInterface.cpp)
    GLuint   m_TextureID;    // GL texture object (0 until EnsureTexture is called)
    int      m_CursorX;
    int      m_CursorY;
    int      m_RowHeight;    // tallest glyph in current row
    bool     m_Dirty;
    int      m_DirtyX0, m_DirtyY0;
    int      m_DirtyX1, m_DirtyY1;
};

// Back-compat alias -- pre-existing call sites use FontAtlasPage.
typedef TextureAtlasPage FontAtlasPage;

// One cached glyph entry in the atlas. Carries TWO metric contracts:
//
// 1. Legacy contract (BakedStringBox / Font.cpp consumers) -- separate-bearing
//    model. u0..v1 / bearingX/Y / advanceX / width / height keep their original
//    semantics: tight ink rect + FreeType bearings, all in world units
//    (FT_26.6_metric * invFontScale * (1/64)). For effect glyphs (BLUR/STROKE)
//    the legacy rect/bearings are grown by the effect pad exactly as before.
//
// 2. Baked-bearing contract (BakedStringTTF pipeline, v1.6.1 GlyphTTF model) --
//    bearing is baked into the atlas cell origin, no separate bearing fields:
//      cellU0..cellV1  UVs of the padded CELL per binary CalcUVs (span covers
//                      cellW+1 x cellH+1 device px; NO inset -- FinishMesh
//                      applies the 1/512 inset at mesh-build time)
//      cellOriginX/Y   (padL, padT) baked-bearing pad, logical device px
//      layoutX/Y       layout metrics: x = floor(advance/64) - bitmap_left,
//                      y = (horiBearingY - height)/64 (bottom-of-ink, baseline-
//                      relative), logical device px
//      cellW/cellH     padded cell size, logical device px
//      page            owning atlas page (binary: TextureAtlasPage* rec[0x40])
//
// pageTextureID: GL texture of the page (legacy binding). 0 for ink-less glyphs
//   (spaces) under the legacy contract; `page` is still valid for those.
struct GlyphAtlasEntry {
    float    u0, v0;         // legacy: top-left UV of the tight ink rect
    float    u1, v1;         // legacy: bottom-right UV of the tight ink rect
    float    bearingX;       // legacy: horiBearingX in world units
    float    bearingY;       // legacy: horiBearingY in world units
    float    advanceX;       // legacy: horiAdvance  in world units
    float    width;          // legacy: ink width in world units
    float    height;         // legacy: ink height in world units
    GLuint   pageTextureID;  // legacy: GL texture of the page (0 if no ink)

    // Baked-bearing cell contract (v1.6.1 GlyphTTF model; see header comment).
    float    cellU0, cellV0; // cell UV origin (no inset)
    float    cellU1, cellV1; // cell UV extent = origin + (cell+1 device px)/pageSize
    float    cellOriginX;    // padL, logical px (GlyphTTF::m_QuadMin.x)
    float    cellOriginY;    // padT, logical px (GlyphTTF::m_QuadMin.y)
    float    layoutX;        // floor(adv/64) - bitmap_left (GlyphTTF::m_GlyphScale.x)
    float    layoutY;        // (horiBearingY - height)/64  (GlyphTTF::m_GlyphScale.y)
    float    cellW, cellH;   // padded cell size, logical px (GlyphTTF::m_QuadSize)
    FontAtlasPage* page;     // owning page (GlyphTTF::m_SurfaceKey)
};

class FontInterface {
public:
    // ASM-spec v1.6.1 Mortar::FontInterface::FontInterface @0x002502e0: no-arg ctor
    // (decompile confirms param_count=1, i.e. only `this` -- no atlasSize parameter in
    // the binary). Atlas page dimension is fixed at 512 internally (port DIFFERS: binary
    // uses 256x256 TextureAtlas pages -- see file header note above).
    FontInterface();
    ~FontInterface();

    // Mirrors binary Initialize @ 0x00250470: sets fontScale/invFontScale/globalSizeScale.
    // Call once after construction. languageByte is game_work+3 (0x13 = russian, scale 0.9).
    void InitialiseData(float fontScale, float globalSizeScale);

    // Pack a glyph cell bitmap (8-bit coverage, width x height bytes) into the
    // atlas. Returns the owning page and writes the packed texel position to
    // *outX / *outY. UV computation is the CALLER's job (FontCacheObjectTTF
    // CalcUVs) -- this keeps the binary's TextureAtlas::AddTexture boundary:
    // the atlas packs, the font cache derives UVs from the packed rec.
    // DIFFERS: binary TextureAtlas::AddTexture @0x00269c9c -- faithful multi-page model.
    //
    // Binary-faithful (host/web/asm-verify): packs onto the CURRENT page
    // (m_Pages.back()) only; allocates a new page on overflow. NEVER drops
    // glyphs.
    //
    // Wii-only (FRUIT_PLATFORM_WII, task #60): best-fits across ALL existing
    // pages (first page whose shelf cursor -- wrapping a row if needed -- has
    // room) before allocating a new one, so free space left behind by earlier
    // strings/pages is backfilled rather than stranded. While a glyph run is
    // open (see BeginGlyphRun), packs ONLY onto the page BeginGlyphRun
    // reserved -- the best-fit search is skipped so every cell of the run
    // lands on the SAME page (a single BakedStringTTF's effect glyphs must
    // never straddle an atlas page boundary, which previously showed up as a
    // visible stroke/blur discontinuity mid-string, e.g. "ボーナス").
    FontAtlasPage* PackGlyphCell(int width, int height, const uint8_t* bitmap,
                                 int* outX, int* outY);

#if defined(FRUIT_PLATFORM_WII)
    // Wii-only (task #60): begin a glyph run: reserve ONE page that is
    // guaranteed to hold `cellCount` cells of up to `maxCellW` x `maxCellH`
    // texels each (shelf-packed, same padding rule as PackGlyphCell), and pin
    // subsequent PackGlyphCell calls to that page until EndGlyphRun. Tries
    // existing pages first (backfill -- reuses whatever room earlier strings
    // left behind); allocates a new page only if no existing page has enough
    // room. `maxCellW`/`maxCellH` is a caller-supplied upper bound (the run's
    // actual per-glyph cells may be smaller; the reservation is a worst case
    // so no mid-run overflow -> no split is possible). Runs do not nest --
    // call EndGlyphRun before starting another. Not part of the binary-
    // faithful baseline -- host/web never call this (compiled out).
    void BeginGlyphRun(int cellCount, int maxCellW, int maxCellH);

    // Wii-only (task #60): end the current glyph run, resume normal best-fit
    // packing.
    void EndGlyphRun();
#endif

    // Upload dirty regions on ALL pages to their GL textures. Idempotent /
    // safe to call every frame: a page's m_Dirty is cleared ONLY once its
    // sub-upload is confirmed to have reached the GPU. On Wii, a bailed
    // glTexSubImage2D (invalid rect, texture not ready, etc. -- see
    // gl_funcsWii.cpp) or a host malloc failure leaves m_Dirty set so the
    // page's pending glyphs are retried on the next call rather than being
    // silently dropped forever (bug #47: glyphs first rasterized mid-draw,
    // e.g. About screen title/CREDITS, could go permanently invisible).
    void BuildPendingTextures();

    // Number of allocated atlas pages.
    int GetPageCount() const { return (int)m_Pages.size(); }

    // GL texture ID of the page at index idx. Returns 0 if idx is out of range.
    GLuint GetPageTextureID(int idx) const;

    // Returns page 0's GL texture ID (or 0 if no pages exist).
    // For draw loops that need a single texture — prefer GlyphAtlasEntry::pageTextureID
    // per-glyph instead; this is retained for backward-compat call sites.
    GLuint GetTextureID() const { return GetPageTextureID(0); }

    // Atlas page pixel dimension.
    int GetSize() const { return m_Size; }

    // Reset atlas to empty: delete all pages, their GL textures, and pixel buffers.
    void Clear();

    // Binary-derived scaling constants (read by FontCacheObjectTTF::SetFontSize).
    int   m_CacheSize;         // binary Bada IFont cache-slot constant: 100 (binary FontInterface ctor
                               // @ 0x002502e0). NOT an FT dpi -- FontCacheObjectTTF::SetCharSize does
                               // NOT pass this to FT_Set_Char_Size (that call uses 0/0 = 72dpi 1:1).
    float m_FontScale;         // super-sampling factor: 1.0 (binary Initialize @ 0x00250470)
    float m_InvFontScale;      // 1/m_FontScale: 1.0
    float m_GlobalSizeScale;   // 1.0 normally; 0.9 for russian (lang byte 0x13)

private:
    int m_Size;                          // page dimension (e.g. 512)
    std::vector<FontAtlasPage*> m_Pages; // owned pages, grows on demand

#if defined(FRUIT_PLATFORM_WII)
    // Wii-only (task #60): glyph-run pin, non-null while a BeginGlyphRun/
    // EndGlyphRun scope is open. PackGlyphCell packs exclusively onto this
    // page. Not part of the binary-faithful baseline.
    FontAtlasPage* m_RunPage;
#endif

    // Allocate, initialise, and push_back a new page. Returns the new page.
    FontAtlasPage* AllocatePage();

    // Ensure page has a GL texture (uploads the initial blank buffer).
    void EnsurePageTexture(FontAtlasPage* page);

    // Expand the page's dirty region to include (x, y, w, h).
    void MarkPageDirty(FontAtlasPage* page, int x, int y, int w, int h);

#if defined(FRUIT_PLATFORM_WII)
    // Wii-only (task #60): shared shelf-fit check -- does `page`'s cursor
    // (after the same wrap rule PackGlyphCell uses) have room for a width x
    // height cell? Used both by PackGlyphCell's best-fit search and
    // BeginGlyphRun's reservation search. Not part of the binary-faithful
    // baseline.
    bool PageFits(const FontAtlasPage* page, int width, int height) const;
#endif
};

} // namespace Mortar

#endif // FN_ENGINE_RENDER_FONTINTERFACE_H
