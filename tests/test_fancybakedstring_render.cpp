// test_fancybakedstring_render.cpp -- Mortar::FancyBakedString layer-stack screenshot grid.
//
// FancyBakedString (src/engine/render/FancyBakedString.{h,cpp}) composes up to six
// BakedStringTTF layers (shadow/glow/main/stroke/extra1/extra2) drawn back-to-front
// from a single call. The whole point of the class vs a plain BakedStringTTF label is
// the layer stack, so this test exercises combinations of it directly (not through any
// screen/HUD control) and regression-guards that the layers actually draw.
//
// Grid (3 cols x 2 rows), same "PLAY" text + font size in every cell so cells are
// pixel-comparable:
//   Row 0: main only / main+shadow (BLUR) / main+stroke (INNER_GLOW, currently unwired)
//   Row 1: main+glow (STROKE) / main+shadow+stroke (combo) / main + 3-stop ApplyGradientSplit
//
// NOTE on naming: FancyBakedString's ctor arg "glowSize/glowCol" builds m_pGlow with
// FONT_EFFECT_STROKE (the padded SDF outline that IS wired up in FontCacheObjectTTF::GetGlyph),
// while "strokeSize/strokeCol" builds m_pStroke with FONT_EFFECT_INNER_GLOW, which
// FontCacheObjectTTF::GetGlyph does not special-case (falls through to the plain sharp-glyph
// path, see FontCacheObjectTTF.cpp:311). So the "main+stroke" cell renders as a same-footprint
// colour swap (m_pStroke drawn last, opaque, over m_pMain), not a visible outline -- that is
// the port's actual current behaviour, not a test bug.
//
// Hard assertions (real regression guards, not just a smoke test):
//   1. Every cell must draw non-background pixels in its text area.
//   2. The shadow cell (main+shadow) must contain at least one foreground pixel at a
//      text-area position where the plain cell (main only) has none -- proves the BLUR
//      layer's padded rasterisation actually grows the drawn footprint beyond m_pMain's.
//
// Output: tmp/test/screenshots/fancybakedstring/grid.png
//
// No lambdas, no auto, no range-for, no enum class (cross-build GCC 4.4.1 safe).

#include "test_harness.h"
#include "render/FancyBakedString.h"
#include "render/BakedStringBox.h"
#include "render/FontCacheObjectTTF.h"
#include "render/FontTTFRegistry.h"
#include "render/Font.h"
#include "render/MatrixManager.h"
#include "render/MatrixStack.h"
#include "render/gl_funcs.h"
#include "game/GameWork.h"
#include "math/_Vector2.h"
#include "math/_Vector3.h"
#include "math/Colour.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <SDL_image.h>

// ---------------------------------------------------------------------------
// GL extension function pointers (FBO + readpixels) -- same pattern as
// test_text_effects.cpp.
// ---------------------------------------------------------------------------

typedef void   (APIENTRYP PFN_FB_glGenFramebuffers)(GLsizei, GLuint*);
typedef void   (APIENTRYP PFN_FB_glBindFramebuffer)(GLenum, GLuint);
typedef void   (APIENTRYP PFN_FB_glGenRenderbuffers)(GLsizei, GLuint*);
typedef void   (APIENTRYP PFN_FB_glBindRenderbuffer)(GLenum, GLuint);
typedef void   (APIENTRYP PFN_FB_glRenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
typedef void   (APIENTRYP PFN_FB_glFramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
typedef void   (APIENTRYP PFN_FB_glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (APIENTRYP PFN_FB_glCheckFramebufferStatus)(GLenum);
typedef void   (APIENTRYP PFN_FB_glDeleteFramebuffers)(GLsizei, const GLuint*);
typedef void   (APIENTRYP PFN_FB_glDeleteRenderbuffers)(GLsizei, const GLuint*);
typedef void   (APIENTRYP PFN_FB_glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, GLvoid*);

static PFN_FB_glGenFramebuffers        fb_glGenFramebuffers       = NULL;
static PFN_FB_glBindFramebuffer        fb_glBindFramebuffer       = NULL;
static PFN_FB_glGenRenderbuffers       fb_glGenRenderbuffers      = NULL;
static PFN_FB_glBindRenderbuffer       fb_glBindRenderbuffer      = NULL;
static PFN_FB_glRenderbufferStorage    fb_glRenderbufferStorage   = NULL;
static PFN_FB_glFramebufferRenderbuffer fb_glFramebufferRenderbuffer = NULL;
static PFN_FB_glFramebufferTexture2D   fb_glFramebufferTexture2D  = NULL;
static PFN_FB_glCheckFramebufferStatus fb_glCheckFramebufferStatus = NULL;
static PFN_FB_glDeleteFramebuffers     fb_glDeleteFramebuffers    = NULL;
static PFN_FB_glDeleteRenderbuffers    fb_glDeleteRenderbuffers   = NULL;
static PFN_FB_glReadPixels             fb_glReadPixels            = NULL;

#define FB_FB_           0x8D40u
#define FB_RB_           0x8D41u
#define FB_COLOR0_       0x8CE0u
#define FB_DEPTH_ATTACH_ 0x8D00u
#define FB_DEPTH16_      0x81A5u
#define FB_FB_COMPLETE_  0x8CD5u
#define FB_RGBA8_        0x8058u

static bool FB_LoadFBO() {
#define FB_LOAD(name, T) fb_##name = (T)SDL_GL_GetProcAddress(#name); \
    if (!fb_##name) { fprintf(stderr, "SKIP: " #name " unavailable\n"); return false; }
    FB_LOAD(glGenFramebuffers,        PFN_FB_glGenFramebuffers)
    FB_LOAD(glBindFramebuffer,        PFN_FB_glBindFramebuffer)
    FB_LOAD(glGenRenderbuffers,       PFN_FB_glGenRenderbuffers)
    FB_LOAD(glBindRenderbuffer,       PFN_FB_glBindRenderbuffer)
    FB_LOAD(glRenderbufferStorage,    PFN_FB_glRenderbufferStorage)
    FB_LOAD(glFramebufferRenderbuffer, PFN_FB_glFramebufferRenderbuffer)
    FB_LOAD(glFramebufferTexture2D,   PFN_FB_glFramebufferTexture2D)
    FB_LOAD(glCheckFramebufferStatus, PFN_FB_glCheckFramebufferStatus)
    FB_LOAD(glDeleteFramebuffers,     PFN_FB_glDeleteFramebuffers)
    FB_LOAD(glDeleteRenderbuffers,    PFN_FB_glDeleteRenderbuffers)
#undef FB_LOAD
    fb_glReadPixels = (PFN_FB_glReadPixels)SDL_GL_GetProcAddress("glReadPixels");
    if (!fb_glReadPixels) {
        fprintf(stderr, "SKIP: glReadPixels unavailable\n");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Cell FBO
// ---------------------------------------------------------------------------

static const int FB_CELL_W  = 220;
static const int FB_CELL_H  = 96;
static const int FB_LABEL_H = 14;   // bottom label-strip height in pixels

struct FB_CellFBO {
    GLuint fbo;
    GLuint colorTex;
    GLuint depthRbo;

    FB_CellFBO() : fbo(0), colorTex(0), depthRbo(0) {}

    bool Create() {
        glGenTextures(1, &colorTex);
        glBindTexture(GL_TEXTURE_2D, colorTex);
        glTexImage2D(GL_TEXTURE_2D, 0, (GLint)FB_RGBA8_, FB_CELL_W, FB_CELL_H,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);

        fb_glGenRenderbuffers(1, &depthRbo);
        fb_glBindRenderbuffer(FB_RB_, depthRbo);
        fb_glRenderbufferStorage(FB_RB_, FB_DEPTH16_, FB_CELL_W, FB_CELL_H);
        fb_glBindRenderbuffer(FB_RB_, 0);

        fb_glGenFramebuffers(1, &fbo);
        fb_glBindFramebuffer(FB_FB_, fbo);
        fb_glFramebufferTexture2D(FB_FB_, FB_COLOR0_, GL_TEXTURE_2D, colorTex, 0);
        fb_glFramebufferRenderbuffer(FB_FB_, FB_DEPTH_ATTACH_, FB_RB_, depthRbo);

        GLenum status = fb_glCheckFramebufferStatus(FB_FB_);
        if (status != FB_FB_COMPLETE_) {
            fprintf(stderr, "SKIP: FB_CellFBO incomplete (0x%x)\n", (unsigned)status);
            fb_glBindFramebuffer(FB_FB_, 0);
            return false;
        }
        return true;
    }

    void Bind() {
        fb_glBindFramebuffer(FB_FB_, fbo);
        glViewport(0, 0, FB_CELL_W, FB_CELL_H);
    }

    void Unbind() {
        fb_glBindFramebuffer(FB_FB_, 0);
    }

    void ReadRGBA(unsigned char* buf) {
        // Stage-2 2D batching: drain pending 2D draws before the readback.
        Renderer::GetInstance()->Flush2D();
        fb_glReadPixels(0, 0, FB_CELL_W, FB_CELL_H, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    }

    void Destroy() {
        if (fbo)      { fb_glDeleteFramebuffers(1, &fbo); fbo = 0; }
        if (colorTex) { glDeleteTextures(1, &colorTex); colorTex = 0; }
        if (depthRbo) { fb_glDeleteRenderbuffers(1, &depthRbo); depthRbo = 0; }
    }
};

// Pixel-space ortho: top=FB_CELL_H, bottom=0, left=0, right=FB_CELL_W.
// Y=0 at bottom, Y=FB_CELL_H at top (standard GL origin) -- glReadPixels rows are
// bottom-up, so raw row index == world/pixel Y directly.
static void FB_SetupCellOrtho() {
    MatrixManager& mm = MatrixManager::GetInstance();
    mm.SetupOrtho((float)FB_CELL_H, 0.0f, 0.0f, (float)FB_CELL_W, 1.0f, -1.0f);
    mm.GetViewStack().Reset();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();
}

// Background is mid-grey (0.50,0.50,0.50) so dark shadow layers are visible against it
// (matches the #257 text_effects fix -- near-black hid dark drop-shadows).
static bool FB_IsFg(const unsigned char* px) {
    int dr = (int)px[0] - 128; if (dr < 0) dr = -dr;
    int dg = (int)px[1] - 128; if (dg < 0) dg = -dg;
    int db = (int)px[2] - 128; if (db < 0) db = -db;
    return dr > 40 || dg > 40 || db > 40;
}

// Scans only the text-area rows (excludes the bottom label strip) so the Verdana
// caption never counts as "glyphs drew" for the component under test.
static bool FB_HasGlyphsInTextArea(const unsigned char* rgba) {
    for (int row = FB_LABEL_H; row < FB_CELL_H; ++row) {
        const unsigned char* rowPtr = rgba + (size_t)row * FB_CELL_W * 4;
        for (int col = 0; col < FB_CELL_W; ++col) {
            if (FB_IsFg(rowPtr + (size_t)col * 4)) return true;
        }
    }
    return false;
}

// Counts text-area pixels that are foreground in 'testRgba' but background in
// 'baselineRgba' at the SAME local (row,col). Used to prove the shadow layer's
// padded BLUR rasterisation paints pixels the plain main-only layer does not.
static int FB_CountExtraFgPixels(const unsigned char* testRgba, const unsigned char* baselineRgba) {
    int extra = 0;
    for (int row = FB_LABEL_H; row < FB_CELL_H; ++row) {
        const unsigned char* tRow = testRgba     + (size_t)row * FB_CELL_W * 4;
        const unsigned char* bRow = baselineRgba + (size_t)row * FB_CELL_W * 4;
        for (int col = 0; col < FB_CELL_W; ++col) {
            bool tFg = FB_IsFg(tRow + (size_t)col * 4);
            bool bFg = FB_IsFg(bRow + (size_t)col * 4);
            if (tFg && !bFg) ++extra;
        }
    }
    return extra;
}

// ---------------------------------------------------------------------------
// Cell descriptor
// ---------------------------------------------------------------------------

static const char* const FB_SAMPLE_TEXT = "PLAY";  // matches FancyBakedString.h usage example
static const float       FB_FONT_SIZE   = 30.0f;

struct FB_CellDesc {
    const char* caption;
    Colour      mainCol;
    float       shadowSize;  Colour shadowCol; // -> m_pShadow (BLUR, wired)
    float       glowSize;    Colour glowCol;   // -> m_pGlow   (STROKE, wired)
    float       strokeSize;  Colour strokeCol; // -> m_pStroke (INNER_GLOW, unwired -- colour swap only)
    bool        gradientSplit; // apply the 3-stop ApplyGradientSplit sequence after construction
};

static const int FB_NCOLS  = 3;
static const int FB_NROWS  = 2;
static const int FB_NCELLS = FB_NCOLS * FB_NROWS;

static const Colour kWhite (255, 255, 255, 255);
static const Colour kNavy  (0,   0,   90,  220);  // shadow colour
static const Colour kCyan  (40,  220, 255, 255);  // glow colour
static const Colour kRed   (255, 40,  40,  255);  // stroke colour
static const Colour kGradBase (80, 80, 220, 255); // base colour peeking through an incomplete split

static const FB_CellDesc s_Cells[FB_NCELLS] = {
    // Row 0
    { "Main only",        kWhite,    0.0f, kNavy, 0.0f, kCyan, 0.0f, kRed, false },
    { "Main+Shadow(blur)",kWhite,    4.0f, kNavy, 0.0f, kCyan, 0.0f, kRed, false },
    { "Main+Stroke",      kWhite,    0.0f, kNavy, 0.0f, kCyan, 3.0f, kRed, false },
    // Row 1
    { "Main+Glow(stroke)",kWhite,    0.0f, kNavy, 3.0f, kCyan, 0.0f, kRed, false },
    { "Shadow+Stroke",    kWhite,    4.0f, kNavy, 0.0f, kCyan, 3.0f, kRed, false },
    { "GradientSplit x3", kGradBase, 0.0f, kNavy, 0.0f, kCyan, 0.0f, kRed, true  },
};

// ---------------------------------------------------------------------------
// Render one cell into the shared FBO; read back RGBA pixels.
// outPixels must be FB_CELL_W*FB_CELL_H*4 bytes.
// ---------------------------------------------------------------------------
static void FB_RenderCell(
    FB_CellFBO& cfbo,
    Mortar::FontCacheObjectTTF* font,
    Mortar::Font* bitmapLabel,
    Mortar::FontCacheObjectTTF* verdanaFont,
    const FB_CellDesc& desc,
    unsigned char* outPixels)
{
    cfbo.Bind();

    glClearColor(0.50f, 0.50f, 0.50f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    FB_SetupCellOrtho();

    if (font) {
        const float textCentreX = (float)FB_CELL_W * 0.5f;
        const float textCentreY = (float)FB_LABEL_H + (float)(FB_CELL_H - FB_LABEL_H) * 0.5f;

        Mortar::FancyBakedString* fbs = new Mortar::FancyBakedString(
            font, FB_SAMPLE_TEXT, FB_FONT_SIZE, desc.mainCol, /*p5*/0, /*circleRadius*/0.0f,
            desc.glowSize,   desc.glowCol,
            desc.shadowSize, desc.shadowCol,
            desc.strokeSize, desc.strokeCol,
            /*shadowMode*/0, /*extraSize*/0.0f, /*p15*/0,
            Colour(0, 0, 0, 255), Colour(0, 0, 0, 255));

        if (desc.gradientSplit) {
            // Mirrors the finale ChangeText 3-stop split sequence (top -> mid -> lower band).
            fbs->ApplyGradientSplit(Colour(255, 80,  40,  255), 0.55f);
            fbs->ApplyGradientSplit(Colour(255, 215, 60,  255), 0.5f);
            fbs->ApplyGradientSplit(Colour(255, 255, 255, 255), 0.0f);
        }

        _Vector3<float> pos(textCentreX, textCentreY, 0.0f);
        fbs->Draw(pos, _Vector2<float>(1.0f, 1.0f), 0.0f, Mortar::ALIGN_CENTRE);
        delete fbs; // Shutdown() frees the up-to-6 layers
    }

    // Draw the Verdana caption in the bottom label strip (never counted as "glyphs
    // drew" by the text-area-only scan helpers above).
    if (verdanaFont) {
        const int lblBoxW = FB_CELL_W - 4;
        const int lblBoxH = FB_LABEL_H;
        const float lblSize = 9.0f;

        Mortar::BakedStringBox lblBox(verdanaFont, lblSize, lblBoxW, lblBoxH, (Mortar::ALIGNMENT_TYPE)0x0f, 1, 0);
        lblBox.SetText(desc.caption);
        lblBox.SetColour(Colour(210, 210, 210, 255), 0);

        _Vector3<float> lblPos((float)(FB_CELL_W / 2), (float)(FB_LABEL_H / 2), 0.0f);
        lblBox.SetTranslation(lblPos, 1);
        lblBox.Draw(_Vector2<float>(1.0f, 1.0f), 0.0f, 1);
    } else if (bitmapLabel) {
        const float lblScale = 5.0f;
        _Vector3<float> lblPos(4.0f, 7.0f, 0.0f);
        Colour lblCol(200, 200, 200, 255);
        bitmapLabel->DrawString(lblScale, 1.0f, 0.0f, desc.caption, lblPos, lblCol, 0x0);
    }

    cfbo.ReadRGBA(outPixels);
    cfbo.Unbind();
}

// ---------------------------------------------------------------------------
// Assemble grid canvas and write PNG
// ---------------------------------------------------------------------------

static bool FB_SaveGrid(
    const unsigned char* const* cells, // [FB_NCELLS] row-major, each FB_CELL_W*FB_CELL_H*4 (bottom-up GL)
    fn::TestHarness& h,
    const char* name)
{
    const int totalW = FB_NCOLS * FB_CELL_W;
    const int totalH = FB_NROWS * FB_CELL_H;
    const size_t canvasBytes = (size_t)totalW * totalH * 4;

    unsigned char* canvas = (unsigned char*)std::malloc(canvasBytes);
    if (!canvas) {
        fprintf(stderr, "[fancybakedstring_render] out of memory for grid canvas\n");
        return false;
    }

    for (size_t i = 0; i < canvasBytes; i += 4) {
        canvas[i+0] = 0x12;
        canvas[i+1] = 0x12;
        canvas[i+2] = 0x12;
        canvas[i+3] = 0xFF;
    }

    for (int row = 0; row < FB_NROWS; row++) {
        for (int col = 0; col < FB_NCOLS; col++) {
            const unsigned char* src = cells[row * FB_NCOLS + col];
            if (!src) continue;

            const int dstCellX = col * FB_CELL_W;
            const int dstCellY = row * FB_CELL_H;

            for (int cy = 0; cy < FB_CELL_H; cy++) {
                int glY = FB_CELL_H - 1 - cy;
                const unsigned char* srcRow = src + (size_t)glY * FB_CELL_W * 4;
                unsigned char* dstRow = canvas + ((size_t)(dstCellY + cy) * totalW + dstCellX) * 4;
                std::memcpy(dstRow, srcRow, (size_t)FB_CELL_W * 4);
            }
        }
    }

    const unsigned char sepR = 0x3A, sepG = 0x3A, sepB = 0x3A;
    for (int row = 1; row < FB_NROWS; row++) {
        int sepY = row * FB_CELL_H;
        if (sepY >= totalH) continue;
        for (int x = 0; x < totalW; x++) {
            unsigned char* p = canvas + ((size_t)sepY * totalW + x) * 4;
            p[0] = sepR; p[1] = sepG; p[2] = sepB; p[3] = 0xFF;
        }
    }
    for (int col = 1; col < FB_NCOLS; col++) {
        int sepX = col * FB_CELL_W;
        if (sepX >= totalW) continue;
        for (int y = 0; y < totalH; y++) {
            unsigned char* p = canvas + ((size_t)y * totalW + sepX) * 4;
            p[0] = sepR; p[1] = sepG; p[2] = sepB; p[3] = 0xFF;
        }
    }

    SDL_Surface* surf = SDL_CreateRGBSurfaceFrom(
        canvas, totalW, totalH,
        32, totalW * 4,
        0x000000FFu, 0x0000FF00u, 0x00FF0000u, 0xFF000000u);
    if (!surf) {
        fprintf(stderr, "[fancybakedstring_render] SDL_CreateRGBSurfaceFrom failed: %s\n", SDL_GetError());
        std::free(canvas);
        return false;
    }
    bool ok = h.SavePng(surf, name);
    SDL_FreeSurface(surf);
    std::free(canvas);
    if (!ok) return false;
    printf("[fancybakedstring_render] grid %dx%d (%d rows x %d cols)\n",
           totalW, totalH, FB_NROWS, FB_NCOLS);
    return true;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "fancybakedstring_render");
    h.SetInitFrames(120); // burn-in so gangofchinese.ttf is lazily loaded by game init
    if (!h.ParseFlags()) return 1;
    if (!h.InitComponent()) {
        fprintf(stderr, "SKIP: InitComponent failed\n");
        return 77;
    }

    if (!FB_LoadFBO()) {
        fprintf(stderr, "SKIP: FBO extensions unavailable\n");
        return 77;
    }

    static Mortar::SmartPtr<Mortar::Font> s_GangFont =
        Mortar::Font::Create("fontstruetype/gangofchinese.ttf");
    Mortar::FontCacheObjectTTF* gangFont = NULL;
    if (s_GangFont.IsValid()) {
        gangFont = Mortar::FontTTFRegistry::GetInstance().Lookup(s_GangFont.Get());
    }
    if (!gangFont) {
        fprintf(stderr, "FAIL: could not obtain FontCacheObjectTTF for gangofchinese.ttf\n");
        return 1;
    }
    printf("[fancybakedstring_render] TTF font: gangofchinese.ttf OK\n");

    Mortar::Font* bitmapLabel = NULL;
    if (game_work.pFontMain.IsValid()) {
        bitmapLabel = game_work.pFontMain.Get();
        printf("[fancybakedstring_render] Bitmap label fallback: pFontMain OK\n");
    }

    Mortar::FontCacheObjectTTF* verdanaFont = NULL;
    {
        const char* verdanaPath = "C:\\Windows\\Fonts\\verdana.ttf";
        verdanaFont = new Mortar::FontCacheObjectTTF(verdanaPath, 12);
        if (!verdanaFont->IsValid()) {
            delete verdanaFont;
            verdanaFont = NULL;
            printf("[fancybakedstring_render] WARN: verdana.ttf not found -- bitmap label fallback\n");
        } else {
            printf("[fancybakedstring_render] Verdana label font: OK\n");
        }
    }

    FB_CellFBO cfbo;
    if (!cfbo.Create()) {
        fprintf(stderr, "SKIP: FB_CellFBO create failed\n");
        delete verdanaFont;
        return 77;
    }

    const size_t cellBytes = (size_t)FB_CELL_W * FB_CELL_H * 4;
    unsigned char** cellPixels = (unsigned char**)std::calloc((size_t)FB_NCELLS, sizeof(unsigned char*));
    if (!cellPixels) {
        fprintf(stderr, "FAIL: out of memory for cell pixel array\n");
        cfbo.Destroy();
        delete verdanaFont;
        return 1;
    }
    for (int i = 0; i < FB_NCELLS; i++) {
        cellPixels[i] = (unsigned char*)std::malloc(cellBytes);
        if (!cellPixels[i]) {
            fprintf(stderr, "FAIL: out of memory for cell %d pixels\n", i);
            for (int j = 0; j < i; j++) std::free(cellPixels[j]);
            std::free(cellPixels);
            cfbo.Destroy();
            delete verdanaFont;
            return 1;
        }
        std::memset(cellPixels[i], 0x12, cellBytes);
        for (int k = 3; k < (int)cellBytes; k += 4) cellPixels[i][k] = 0xFF;
    }

    int failCount = 0;

    // Render each cell.
    for (int i = 0; i < FB_NCELLS; i++) {
        const FB_CellDesc& desc = s_Cells[i];
        FB_RenderCell(cfbo, gangFont, bitmapLabel, verdanaFont, desc, cellPixels[i]);

        bool hasGlyphs = FB_HasGlyphsInTextArea(cellPixels[i]);
        const char* status = hasGlyphs ? "OK" : "FAIL (no glyphs)";
        printf("[fancybakedstring_render] cell %d (%s): %s\n", i, desc.caption, status);
        if (!hasGlyphs) {
            fprintf(stderr, "FAIL: cell %d (%s) drew no foreground pixels\n", i, desc.caption);
            ++failCount;
        }
    }

    // Hard regression guard: the shadow cell (index 1) must paint at least one
    // text-area pixel the plain cell (index 0) does not -- proves the BLUR layer's
    // padded rasterisation grows the drawn footprint beyond m_pMain alone.
    int extraFg = FB_CountExtraFgPixels(cellPixels[1], cellPixels[0]);
    printf("[fancybakedstring_render] shadow-vs-plain extra foreground pixels: %d\n", extraFg);
    if (extraFg <= 0) {
        fprintf(stderr, "FAIL: shadow layer (cell 1) drew no pixels beyond the plain cell (cell 0) --"
                        " BLUR layer did not render\n");
        ++failCount;
    }

    printf("[fancybakedstring_render] Cell coverage:\n");
    printf("  0: main only (baseline)\n");
    printf("  1: main+shadow (BLUR, wired -- padded footprint growth)\n");
    printf("  2: main+stroke (INNER_GLOW, unwired -- same-footprint colour swap)\n");
    printf("  3: main+glow (STROKE, wired -- padded outline)\n");
    printf("  4: main+shadow+stroke (combined layer stack)\n");
    printf("  5: main + 3-stop ApplyGradientSplit (y=0.55/0.5/0.0)\n");

    bool saved = false;
    if (h.IsScreenshot()) {
        const unsigned char* const* constCells = (const unsigned char* const*)cellPixels;
        saved = FB_SaveGrid(constCells, h, "fancybakedstring/grid");
        if (!saved) {
            fprintf(stderr, "FAIL: could not save FancyBakedString grid PNG\n");
            ++failCount;
        }
    }

    for (int i = 0; i < FB_NCELLS; i++) std::free(cellPixels[i]);
    std::free(cellPixels);
    cfbo.Destroy();
    delete verdanaFont;

    if (failCount > 0) {
        fprintf(stderr, "FAIL: test_fancybakedstring_render (%d failure(s))\n", failCount);
        h.Shutdown();
        return 1;
    }

    printf("PASS: test_fancybakedstring_render complete\n");
    return h.Shutdown();
}
