// test_cjk_grid.cpp -- Latin + CJK glyph-clip visual regression grid.
//
// Reproduces the Wii tall-glyph clip (stb TTF backend, kFontSupersample=1)
// on host: renders real in-game strings (pulled live from the shipped
// stringtables/translations_*.str via Mortar::StringTable, same file the
// game loads) at the game's real BakedStringBox font sizes, with a visible
// per-cell grid + baseline + ascent line so any glyph whose ink pokes above
// the cell top (kanji with tall radicals: "開", "解", "識", ...) is obvious.
//
// Build with -DFN_TTF_BACKEND=stb -DFN_ENABLE_HD_ASSETS=OFF to match the
// Wii config (SS=1) that exhibits the clip; the freetype/HD-asset host
// default may not reproduce it.
//
// Rows = one per (language, string) pair; cols = 3 font sizes (14 / 22 / 30,
// the DojoScreen ring-label / GameModeScreen+MainScreen instruction / Dojo
// title sizes respectively -- see src/screens/DojoScreen.cpp SetFontSize(14)
// and BakedStringBox ctor calls, src/screens/GameModeScreen.cpp fontSize=22).
//
// Output (--screenshot mode):
//   tmp/test/screenshots/cjk_grid/grid.png  (via FN_TEST_SCREENSHOT_DIR)
//
// Row label (left column) rendered in Verdana (C:\Windows\Fonts\verdana.ttf)
// via FontCacheObjectTTF + BakedStringBox, matching test_text_render.cpp's
// caption convention; falls back to pFontMain (bitmap .fnt) when absent.
//
// No lambdas, no auto, no range-for, no enum class (cross-build GCC 4.4.1 safe).

#include "test_harness.h"
#include "render/BakedStringBox.h"
#include "render/FontCacheObjectTTF.h"
#include "render/FontTTFRegistry.h"
#include "render/Font.h"
#include "render/MatrixManager.h"
#include "render/MatrixStack.h"
#include "render/gl_funcs.h"
#include "game/GameWork.h"
#include "util/StringTable.h"
#include "util/Localisation.h"
#include "math/_Vector2.h"
#include "math/_Vector3.h"
#include "math/Colour.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <SDL_image.h>

// ---------------------------------------------------------------------------
// GL extension function pointers (FBO + readpixels) -- same set as test_text_render.
// ---------------------------------------------------------------------------

typedef void  (APIENTRYP PFN_CG_glGenFramebuffers)(GLsizei, GLuint*);
typedef void  (APIENTRYP PFN_CG_glBindFramebuffer)(GLenum, GLuint);
typedef void  (APIENTRYP PFN_CG_glGenRenderbuffers)(GLsizei, GLuint*);
typedef void  (APIENTRYP PFN_CG_glBindRenderbuffer)(GLenum, GLuint);
typedef void  (APIENTRYP PFN_CG_glRenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
typedef void  (APIENTRYP PFN_CG_glFramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
typedef void  (APIENTRYP PFN_CG_glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum(APIENTRYP PFN_CG_glCheckFramebufferStatus)(GLenum);
typedef void  (APIENTRYP PFN_CG_glDeleteFramebuffers)(GLsizei, const GLuint*);
typedef void  (APIENTRYP PFN_CG_glDeleteRenderbuffers)(GLsizei, const GLuint*);
typedef void  (APIENTRYP PFN_CG_glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, GLvoid*);

static PFN_CG_glGenFramebuffers       cg_glGenFramebuffers       = NULL;
static PFN_CG_glBindFramebuffer       cg_glBindFramebuffer       = NULL;
static PFN_CG_glGenRenderbuffers      cg_glGenRenderbuffers      = NULL;
static PFN_CG_glBindRenderbuffer      cg_glBindRenderbuffer      = NULL;
static PFN_CG_glRenderbufferStorage   cg_glRenderbufferStorage   = NULL;
static PFN_CG_glFramebufferRenderbuffer cg_glFramebufferRenderbuffer = NULL;
static PFN_CG_glFramebufferTexture2D  cg_glFramebufferTexture2D  = NULL;
static PFN_CG_glCheckFramebufferStatus cg_glCheckFramebufferStatus = NULL;
static PFN_CG_glDeleteFramebuffers    cg_glDeleteFramebuffers    = NULL;
static PFN_CG_glDeleteRenderbuffers   cg_glDeleteRenderbuffers   = NULL;
static PFN_CG_glReadPixels            cg_glReadPixels            = NULL;

#define CG_FB_           0x8D40u
#define CG_RB_           0x8D41u
#define CG_COLOR0_       0x8CE0u
#define CG_DEPTH_ATTACH_ 0x8D00u
#define CG_DEPTH16_      0x81A5u
#define CG_FB_COMPLETE_  0x8CD5u
#define CG_RGBA8_        0x8058u

static bool CG_LoadFBO() {
#define CG_LOAD(name, T) cg_##name = (T)SDL_GL_GetProcAddress(#name); \
    if (!cg_##name) { fprintf(stderr, "SKIP: " #name " unavailable\n"); return false; }
    CG_LOAD(glGenFramebuffers,       PFN_CG_glGenFramebuffers)
    CG_LOAD(glBindFramebuffer,       PFN_CG_glBindFramebuffer)
    CG_LOAD(glGenRenderbuffers,      PFN_CG_glGenRenderbuffers)
    CG_LOAD(glBindRenderbuffer,      PFN_CG_glBindRenderbuffer)
    CG_LOAD(glRenderbufferStorage,   PFN_CG_glRenderbufferStorage)
    CG_LOAD(glFramebufferRenderbuffer, PFN_CG_glFramebufferRenderbuffer)
    CG_LOAD(glFramebufferTexture2D,  PFN_CG_glFramebufferTexture2D)
    CG_LOAD(glCheckFramebufferStatus, PFN_CG_glCheckFramebufferStatus)
    CG_LOAD(glDeleteFramebuffers,    PFN_CG_glDeleteFramebuffers)
    CG_LOAD(glDeleteRenderbuffers,   PFN_CG_glDeleteRenderbuffers)
#undef CG_LOAD
    cg_glReadPixels = (PFN_CG_glReadPixels)SDL_GL_GetProcAddress("glReadPixels");
    if (!cg_glReadPixels) {
        fprintf(stderr, "SKIP: glReadPixels unavailable\n");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Cell FBO -- taller than test_text_render's since we need room above the
// text for ascent-clip to be visible without immediately hitting the top edge.
// ---------------------------------------------------------------------------

static const int LABEL_W = 130; // left label strip width (language/key/size caption)
static const int CELL_W  = 260;
static const int CELL_H  = 56;

struct CellFBO {
    GLuint fbo;
    GLuint colorTex;
    GLuint depthRbo;
    int w, h;

    CellFBO() : fbo(0), colorTex(0), depthRbo(0), w(0), h(0) {}

    bool Create(int width, int height) {
        w = width; h = height;
        glGenTextures(1, &colorTex);
        glBindTexture(GL_TEXTURE_2D, colorTex);
        glTexImage2D(GL_TEXTURE_2D, 0, (GLint)CG_RGBA8_, w, h,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);

        cg_glGenRenderbuffers(1, &depthRbo);
        cg_glBindRenderbuffer(CG_RB_, depthRbo);
        cg_glRenderbufferStorage(CG_RB_, CG_DEPTH16_, w, h);
        cg_glBindRenderbuffer(CG_RB_, 0);

        cg_glGenFramebuffers(1, &fbo);
        cg_glBindFramebuffer(CG_FB_, fbo);
        cg_glFramebufferTexture2D(CG_FB_, CG_COLOR0_, GL_TEXTURE_2D, colorTex, 0);
        cg_glFramebufferRenderbuffer(CG_FB_, CG_DEPTH_ATTACH_, CG_RB_, depthRbo);

        GLenum status = cg_glCheckFramebufferStatus(CG_FB_);
        if (status != CG_FB_COMPLETE_) {
            fprintf(stderr, "SKIP: CellFBO incomplete (0x%x)\n", (unsigned)status);
            cg_glBindFramebuffer(CG_FB_, 0);
            return false;
        }
        return true;
    }

    void Bind() {
        cg_glBindFramebuffer(CG_FB_, fbo);
        glViewport(0, 0, w, h);
    }

    void Unbind() {
        cg_glBindFramebuffer(CG_FB_, 0);
    }

    void ReadRGBA(unsigned char* buf) {
        // Stage-2 2D batching: drain pending 2D draws before the readback.
        Renderer::GetInstance()->Flush2D();
        cg_glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    }

    void Destroy() {
        if (fbo)      { cg_glDeleteFramebuffers(1, &fbo); fbo = 0; }
        if (colorTex) { glDeleteTextures(1, &colorTex); colorTex = 0; }
        if (depthRbo) { cg_glDeleteRenderbuffers(1, &depthRbo); depthRbo = 0; }
    }
};

// Pixel-space ortho: top=h, bottom=0, left=0, right=w. Y=0 bottom (GL default).
static void CG_SetupCellOrtho(int w, int h) {
    MatrixManager& mm = MatrixManager::GetInstance();
    mm.SetupOrtho((float)h, 0.0f, 0.0f, (float)w, 1.0f, -1.0f);
    mm.GetViewStack().Reset();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();
}

// ---------------------------------------------------------------------------
// Font sizes -- real BakedStringBox sizes used in-game.
//   14: DojoScreen ring-label captions (btn->m_pLabelBox->SetFontSize(14.0f),
//       src/screens/DojoScreen.cpp lines ~158/186)
//   22: GameModeScreen title/instruction boxes (fontSize=22, w=200, h=22,
//       src/screens/GameModeScreen.cpp lines ~284/301)
//   30: DojoScreen "DOJO" title (fontSize=30, src/screens/DojoScreen.cpp line ~122)
// ---------------------------------------------------------------------------

static const int SIZE_COUNT = 3;
static const float kFontSizes[SIZE_COUNT] = { 14.0f, 22.0f, 30.0f };
static const char* const kSizeLabels[SIZE_COUNT] = { "14px", "22px", "30px" };

// ---------------------------------------------------------------------------
// String rows -- pulled live from the shipped translations_*.str files
// (same StringTable the game loads; see src/engine/util/StringTable.cpp)
// rather than hand-transcribed UTF-8 literals, so the grid tracks whatever
// text actually ships. Each row names the key + a short English gloss for
// the caption; the runtime text is looked up per-language at grid-build time.
//
// Keys chosen to cover: kanji-heavy multi-line Japanese (the known Wii clip
// case -- tall radicals in "開始"/"解除"/"獲得"), Simplified + Traditional
// Chinese hanzi, Korean hangul, and an English/Latin control row.
// ---------------------------------------------------------------------------

struct RowSpec {
    const char* key;   // stringtable key (ASCII)
    const char* gloss; // short English gloss for the row caption
};

static const int ROW_COUNT = 6;
static const RowSpec kRows[ROW_COUNT] = {
    { "GEN_OK",           "OK"        },
    { "MENU_TEXTURE_13",  "slice-to-begin (multi-line)" },
    { "GAME_TEXTURE_02",  "new-best banner"  },
    { "DOJO_TEXT_00",     "blade name"       },
    { "DOJO_TEXT_02",     "blade desc (multi-line)" },
    { "DOJO_TEXT_08",     "blade desc (long, multi-line)" },
};

struct LangSpec {
    const char* code;  // short tag for filenames/captions
    const char* name;  // full name passed to LanguageFlagFromName via ParseLanguageArg spelling
};

static const int LANG_COUNT = 5;
static const LangSpec kLangs[LANG_COUNT] = {
    { "EN", "english_us"          },
    { "ZH", "chinese"             },
    { "ZHT","traditional chinese" },
    { "JA", "japanese"            },
    { "KO", "korean"              },
};

// ---------------------------------------------------------------------------
// Glyph presence scan (same threshold convention as test_text_render.cpp)
// ---------------------------------------------------------------------------

static bool CG_HasGlyphs(const unsigned char* rgba, int w, int h) {
    const int N = w * h * 4;
    for (int i = 0; i < N; i += 4) {
        if (rgba[i] > 0x30 || rgba[i+1] > 0x30 || rgba[i+2] > 0x30) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Grid overlay (border + baseline + ascent line): stamped as a CPU pixel
// write into the read-back RGBA buffer after glReadPixels, rather than a
// GL draw pass -- the ES2 pipeline has no fixed-function line primitive
// and a 1px-line shader pass is unnecessary complexity for a debug grid.
//
// Layout (pixel space, Y=0 bottom):
//   bottom margin 6px -> baseline
//   ascent line at baseline + ascentPx (approx 0.8 * fontSize -- typical
//   TTF hhea ascent fraction; drawn as a reference line so any glyph ink
//   above it at a given fontSize is flagged as "may clip a shorter cell").
//   Cell border drawn as a 1px rect around the whole w x h area.
// ---------------------------------------------------------------------------

// Stamps the grid border + baseline + ascent line directly into the
// CPU-side RGBA buffer after glReadPixels. Buffer is bottom-up GL order
// (row 0 = bottom). Colours chosen to stand out against both dark
// background and light glyph ink.
static void CG_StampGridOverlay(unsigned char* rgba, int w, int h, float fontSize) {
    const int baselineY   = 6;                                   // px from bottom
    const int ascentY     = baselineY + (int)(fontSize * 0.8f);  // approx cap/ascent line

    // Border colour: muted blue. Baseline: green. Ascent: red (clip marker).
    const unsigned char borderC[4]   = { 0x50, 0x60, 0x80, 0xFF };
    const unsigned char baselineC[4] = { 0x30, 0xC0, 0x30, 0xFF };
    const unsigned char ascentC[4]   = { 0xE0, 0x30, 0x30, 0xFF };

    for (int x = 0; x < w; x++) {
        // Top + bottom border.
        unsigned char* pTop = rgba + ((size_t)(h - 1) * w + x) * 4;
        std::memcpy(pTop, borderC, 4);
        unsigned char* pBot = rgba + ((size_t)0 * w + x) * 4;
        std::memcpy(pBot, borderC, 4);

        // Baseline (green, faint blend not needed -- solid is fine, thin cell).
        if (baselineY >= 0 && baselineY < h) {
            unsigned char* pBase = rgba + ((size_t)baselineY * w + x) * 4;
            std::memcpy(pBase, baselineC, 4);
        }
        // Ascent reference line (red) -- glyph ink above this at the row's
        // fontSize is the "may clip in a shorter cell" signal.
        if (ascentY >= 0 && ascentY < h) {
            unsigned char* pAsc = rgba + ((size_t)ascentY * w + x) * 4;
            std::memcpy(pAsc, ascentC, 4);
        }
    }
    for (int y = 0; y < h; y++) {
        unsigned char* pLeft = rgba + ((size_t)y * w + 0) * 4;
        std::memcpy(pLeft, borderC, 4);
        unsigned char* pRight = rgba + ((size_t)y * w + (w - 1)) * 4;
        std::memcpy(pRight, borderC, 4);
    }
}

// Render one text cell into cfbo, read back pixels, stamp the grid overlay.
static bool CG_RenderTextCell(
    CellFBO& cfbo,
    Mortar::FontCacheObjectTTF* ttfFont,
    const char* text,
    float fontSize,
    unsigned char* outPixels)
{
    cfbo.Bind();

    glClearColor(0.09f, 0.09f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    CG_SetupCellOrtho(cfbo.w, cfbo.h);

    bool hasGlyphs = false;

    if (ttfFont && text) {
        const int boxW = cfbo.w - 8;
        const int boxH = cfbo.h - 4;

        // Anchor near the bottom of the cell so tall/multi-line ink has
        // room to grow upward -- clip (if any) shows against the top border.
        const float anchorX = (float)cfbo.w * 0.5f;
        const float anchorY = 6.0f + fontSize * 0.5f;

        // align=0x0f: centre-H + centre-V. flag=1 pre-shifts like IngamePopup.
        Mortar::BakedStringBox box(ttfFont, fontSize, boxW, boxH, (Mortar::ALIGNMENT_TYPE)0x0f, 3, 2);
        box.SetText(text);
        box.SetColour(Colour(255, 255, 255, 255), 0);
        _Vector3<float> pos(anchorX, anchorY, 0.0f);
        box.SetTranslation(pos, 1);
        _Vector2<float> sc(1.0f, 1.0f);
        box.Draw(sc, 0.0f, 1);
    }

    cfbo.ReadRGBA(outPixels);
    hasGlyphs = CG_HasGlyphs(outPixels, cfbo.w, cfbo.h);

    CG_StampGridOverlay(outPixels, cfbo.w, cfbo.h, fontSize);

    cfbo.Unbind();
    return hasGlyphs;
}

// Render the left-column row label (language + key + gloss) via Verdana.
static void CG_RenderLabelCell(
    CellFBO& cfbo,
    Mortar::FontCacheObjectTTF* verdanaFont,
    Mortar::Font* fallbackFont,
    const char* text,
    unsigned char* outPixels)
{
    cfbo.Bind();
    glClearColor(0.14f, 0.14f, 0.16f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    CG_SetupCellOrtho(cfbo.w, cfbo.h);

    if (verdanaFont) {
        const int boxW = cfbo.w - 6;
        const int boxH = cfbo.h - 4;
        Mortar::BakedStringBox box(verdanaFont, 10.0f, boxW, boxH, (Mortar::ALIGNMENT_TYPE)0x0f, 3, 1);
        box.SetText(text);
        box.SetColour(Colour(220, 220, 220, 255), 0);
        _Vector3<float> pos((float)cfbo.w * 0.5f, (float)cfbo.h * 0.5f, 0.0f);
        box.SetTranslation(pos, 1);
        _Vector2<float> sc(1.0f, 1.0f);
        box.Draw(sc, 0.0f, 1);
    } else if (fallbackFont) {
        const float scale = 5.0f;
        _Vector3<float> pos(4.0f, (float)(cfbo.h / 2), 0.0f);
        Colour col(200, 200, 200, 255);
        fallbackFont->DrawString(scale, 1.0f, 0.0f, text, pos, col, 0x0);
    }

    cfbo.ReadRGBA(outPixels);
    // Thin border only (no baseline/ascent markers -- this is the label strip).
    const unsigned char borderC[4] = { 0x40, 0x40, 0x44, 0xFF };
    for (int x = 0; x < cfbo.w; x++) {
        std::memcpy(outPixels + ((size_t)(cfbo.h - 1) * cfbo.w + x) * 4, borderC, 4);
        std::memcpy(outPixels + ((size_t)0 * cfbo.w + x) * 4, borderC, 4);
    }
    for (int y = 0; y < cfbo.h; y++) {
        std::memcpy(outPixels + ((size_t)y * cfbo.w + 0) * 4, borderC, 4);
        std::memcpy(outPixels + ((size_t)y * cfbo.w + (cfbo.w - 1)) * 4, borderC, 4);
    }
    cfbo.Unbind();
}

// ---------------------------------------------------------------------------
// Composite canvas + save PNG
// ---------------------------------------------------------------------------

static bool CG_SaveGrid(
    const unsigned char* const* labelCells, // [ROW_COUNT * LANG_COUNT], LABEL_W x CELL_H
    const unsigned char* const* textCells,  // [ROW_COUNT * LANG_COUNT * SIZE_COUNT], CELL_W x CELL_H
    fn::TestHarness& h,
    const char* name)
{
    const int totalRows = ROW_COUNT * LANG_COUNT;
    const int totalW = LABEL_W + SIZE_COUNT * CELL_W;
    const int totalH = totalRows * CELL_H;
    const size_t canvasBytes = (size_t)totalW * (size_t)totalH * 4;

    unsigned char* canvas = (unsigned char*)std::malloc(canvasBytes);
    if (!canvas) {
        fprintf(stderr, "[cjk_grid] out of memory for grid canvas\n");
        return false;
    }
    for (size_t i = 0; i < canvasBytes; i += 4) {
        canvas[i+0] = 0x12; canvas[i+1] = 0x12; canvas[i+2] = 0x12; canvas[i+3] = 0xFF;
    }

    for (int row = 0; row < totalRows; row++) {
        int dstRowY = row * CELL_H;

        // Label cell (LABEL_W x CELL_H) at x=0.
        const unsigned char* lsrc = labelCells[row];
        if (lsrc) {
            for (int cy = 0; cy < CELL_H; cy++) {
                int glY = CELL_H - 1 - cy;
                const unsigned char* srcRow = lsrc + (size_t)glY * LABEL_W * 4;
                unsigned char* dstRow = canvas + ((size_t)(dstRowY + cy) * totalW + 0) * 4;
                std::memcpy(dstRow, srcRow, (size_t)LABEL_W * 4);
            }
        }

        // Text cells (CELL_W x CELL_H) at x = LABEL_W + sizeIdx*CELL_W.
        for (int sizeIdx = 0; sizeIdx < SIZE_COUNT; sizeIdx++) {
            const unsigned char* src = textCells[row * SIZE_COUNT + sizeIdx];
            if (!src) continue;
            int dstCellX = LABEL_W + sizeIdx * CELL_W;
            for (int cy = 0; cy < CELL_H; cy++) {
                int glY = CELL_H - 1 - cy;
                const unsigned char* srcRow = src + (size_t)glY * CELL_W * 4;
                unsigned char* dstRow = canvas + ((size_t)(dstRowY + cy) * totalW + dstCellX) * 4;
                std::memcpy(dstRow, srcRow, (size_t)CELL_W * 4);
            }
        }
    }

    SDL_Surface* surf = SDL_CreateRGBSurfaceFrom(
        canvas, totalW, totalH, 32, totalW * 4,
        0x000000FFu, 0x0000FF00u, 0x00FF0000u, 0xFF000000u);
    if (!surf) {
        fprintf(stderr, "[cjk_grid] SDL_CreateRGBSurfaceFrom failed: %s\n", SDL_GetError());
        std::free(canvas);
        return false;
    }
    bool ok = h.SavePng(surf, name);
    SDL_FreeSurface(surf);
    std::free(canvas);
    if (!ok) return false;
    printf("[cjk_grid] grid %dx%d (%d rows x %d sizes)\n", totalW, totalH, totalRows, SIZE_COUNT);
    return true;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "cjk_grid");
    // Burn-in: allow GameInitialise + MainScreen to load gangofchinese.ttf
    // (lazily loaded on first MenuButton/BSButton creation), matching
    // test_text_render.cpp.
    h.SetInitFrames(120);
    if (!h.ParseFlags()) return 1;
    if (!h.InitComponent()) {
        fprintf(stderr, "SKIP: InitComponent failed\n");
        return 77;
    }

    if (!CG_LoadFBO()) {
        fprintf(stderr, "SKIP: FBO extensions unavailable\n");
        return 77;
    }

    // Shared CJK TTF font (gangofchinese.ttf covers Latin/Chinese/Japanese/Korean).
    static Mortar::SmartPtr<Mortar::Font> s_TTFFont =
        Mortar::Font::Create("fontstruetype/gangofchinese.ttf");
    Mortar::FontCacheObjectTTF* ttfFont = NULL;
    if (s_TTFFont.IsValid()) {
        ttfFont = Mortar::FontTTFRegistry::GetInstance().Lookup(s_TTFFont.Get());
    }
    if (!ttfFont) {
        fprintf(stderr, "FAIL: could not obtain FontCacheObjectTTF for gangofchinese.ttf\n");
        return 1;
    }
    printf("[cjk_grid] TTF font: gangofchinese.ttf OK\n");

    // Verdana for row-label captions (legible at small size); pFontMain fallback.
    Mortar::FontCacheObjectTTF* verdanaFont = NULL;
    {
        const char* verdanaPath = "C:\\Windows\\Fonts\\verdana.ttf";
        verdanaFont = new Mortar::FontCacheObjectTTF(verdanaPath, 12);
        if (!verdanaFont->IsValid()) {
            delete verdanaFont;
            verdanaFont = NULL;
            printf("[cjk_grid] WARN: verdana.ttf not found -- using bitmap fallback\n");
        } else {
            printf("[cjk_grid] Verdana label font: OK\n");
        }
    }
    Mortar::Font* labelFallbackFont = NULL;
    if (game_work.pFontMain.IsValid()) {
        labelFallbackFont = game_work.pFontMain.Get();
    }

    CellFBO textFbo;
    if (!textFbo.Create(CELL_W, CELL_H)) {
        fprintf(stderr, "SKIP: text CellFBO create failed\n");
        return 77;
    }
    CellFBO labelFbo;
    if (!labelFbo.Create(LABEL_W, CELL_H)) {
        fprintf(stderr, "SKIP: label CellFBO create failed\n");
        textFbo.Destroy();
        return 77;
    }

    const int totalRows = ROW_COUNT * LANG_COUNT;
    const size_t textCellBytes  = (size_t)CELL_W  * CELL_H * 4;
    const size_t labelCellBytes = (size_t)LABEL_W * CELL_H * 4;

    unsigned char** labelCells = (unsigned char**)std::calloc((size_t)totalRows, sizeof(unsigned char*));
    unsigned char** textCells  = (unsigned char**)std::calloc((size_t)(totalRows * SIZE_COUNT), sizeof(unsigned char*));
    if (!labelCells || !textCells) {
        fprintf(stderr, "FAIL: out of memory for cell pixel arrays\n");
        textFbo.Destroy();
        labelFbo.Destroy();
        return 1;
    }
    for (int i = 0; i < totalRows; i++) {
        labelCells[i] = (unsigned char*)std::malloc(labelCellBytes);
        std::memset(labelCells[i], 0x12, labelCellBytes);
    }
    for (int i = 0; i < totalRows * SIZE_COUNT; i++) {
        textCells[i] = (unsigned char*)std::malloc(textCellBytes);
        std::memset(textCells[i], 0x12, textCellBytes);
    }

    const char* dataDir = h.game.data_dir.c_str();

    int noGlyphCount = 0;
    int row = 0;
    for (int langIdx = 0; langIdx < LANG_COUNT; langIdx++) {
        const LangSpec& lang = kLangs[langIdx];
        int flag = Mortar::StringTable::LanguageFlagFromName(lang.name);
        if (flag < 0) {
            fprintf(stderr, "WARN: unknown language name '%s', skipping\n", lang.name);
            row += ROW_COUNT;
            continue;
        }
        Localisation::Load(dataDir, flag);
        if (!Localisation::IsLoaded()) {
            fprintf(stderr, "WARN: Localisation::Load failed for '%s'\n", lang.name);
        }

        for (int rowIdx = 0; rowIdx < ROW_COUNT; rowIdx++, row++) {
            const RowSpec& rs = kRows[rowIdx];
            const char* text = Localisation::Get(rs.key);

            char label[96];
            std::snprintf(label, sizeof(label), "%s %s\n%s", lang.code, rs.key, rs.gloss);
            CG_RenderLabelCell(labelFbo, verdanaFont, labelFallbackFont, label, labelCells[row]);

            for (int sizeIdx = 0; sizeIdx < SIZE_COUNT; sizeIdx++) {
                unsigned char* buf = textCells[row * SIZE_COUNT + sizeIdx];
                bool hasGlyphs = CG_RenderTextCell(textFbo, ttfFont, text, kFontSizes[sizeIdx], buf);
                printf("[cjk_grid] %-4s %-18s @%s: '%s' -> %s\n",
                       lang.code, rs.key, kSizeLabels[sizeIdx], text ? text : "(null)",
                       hasGlyphs ? "glyphs OK" : "NO GLYPHS");
                if (!hasGlyphs) noGlyphCount++;
            }
        }
    }

    printf("[cjk_grid] Cells with no glyphs: %d / %d\n", noGlyphCount, totalRows * SIZE_COUNT);
    printf("[cjk_grid] Grid overlay: blue border=cell bounds, green line=baseline,\n");
    printf("           red line=ascent reference (~0.8*fontSize above baseline).\n");
    printf("           Glyph ink crossing/above the top blue border = CLIP.\n");

    bool saved = false;
    if (h.IsScreenshot()) {
        const unsigned char* const* constLabels = (const unsigned char* const*)labelCells;
        const unsigned char* const* constText   = (const unsigned char* const*)textCells;
        saved = CG_SaveGrid(constLabels, constText, h, "cjk_grid/grid");
        if (!saved) fprintf(stderr, "FAIL: could not save grid PNG\n");
    }

    for (int i = 0; i < totalRows; i++) std::free(labelCells[i]);
    std::free(labelCells);
    for (int i = 0; i < totalRows * SIZE_COUNT; i++) std::free(textCells[i]);
    std::free(textCells);

    textFbo.Destroy();
    labelFbo.Destroy();

    delete verdanaFont;
    verdanaFont = NULL;

    Localisation::Unload();

    if (h.IsScreenshot() && !saved) {
        h.Shutdown();
        return 1;
    }

    printf("PASS: test_cjk_grid complete\n");
    return h.Shutdown();
}
