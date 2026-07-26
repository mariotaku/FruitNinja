// test_font_align -- pixel-readback regression guard for Font::DrawString alignment.
//
// RE-confirmed binary semantics (Mortar::Font::DrawString @0x0024c7f0 / @0x00198e44):
//   alignment & 0x3 (horizontal bits):
//     0x0 = LEFT   -> lineOffset = 0 (text starts at anchor X, extends right)
//     0x1 = CENTER -> lineOffset = 0 (INERT: same as LEFT -- bit-1 is not a real centre)
//     0x2 = RIGHT  -> right edge at anchor X (lineOffset = -lineWidth, extends left)
//     0x3 = 0x2|0x1 -> lineOffset = -lineWidth/2 (true centre on anchor X)
//   alignment & 0xC (vertical bits) -- not tested here (axes independent).
//
// ScoreControl passes align=0x0D (0b1101) -> horizontal bits 0x01 = "inert centre"
// = effectively LEFT. This test proves the port renders 0x0D LEFT-anchored.
//
// Test strategy:
//   1. Boot the full game via TestHarness::InitComponent() so pFontNumbers (a
//      real .fnt atlas) is available.
//   2. Create an offscreen FBO (512x128 pixels).
//   3. Set up a pixel-space ortho so 1 world unit == 1 FBO pixel.
//   4. For each alignment flag, render the string "123" (asymmetric, non-trivial
//      width) with a white colour on a black background.
//   5. Read back pixels, scan columns to find the leftmost (xmin) and rightmost
//      (xmax) lit pixel column.
//   6. Assert that the measured bounding box matches the expected placement:
//      LEFT (0x0):   xmin ~= anchorX  (text extends right from anchor)
//      CENTER (0x1): xmin ~= anchorX  (same as LEFT -- inert bit)
//      RIGHT (0x2):  xmax ~= anchorX  (text extends left from anchor)
//      0x3 (center): midpoint ~= anchorX
//      0x0D (score): xmin ~= anchorX  (horizontal bits = 0x01 -> inert -> LEFT)
//      0x0F (bonus): midpoint ~= anchorX  (horizontal bits = 0x03 -> true centre)
//
// Tolerance: 8 px. LEFT vs RIGHT vs CENTER cases differ by ~tens of pixels
// so 8 px cannot produce a false pass.
//
// Screenshot output (always written after assertions):
//   tmp/test/screenshots/font_align/all.png    -- composite: 5 rows, one per case
//   tmp/test/screenshots/font_align/<hex>.png  -- individual row per alignment flag
// Each row shows the rendered text plus a magenta vertical reference line at anchorX.
//
// If the game boot fails (no assets, no GL), the test exits 77 (CTest SKIP).
//
// No audio: SFX volume is silenced inside TestHarness::Init().
// No lambdas, no auto, no range-for (cross-build GCC 4.4.1 safe).

#include "test_harness.h"
#include "render/Font.h"
#include "render/MatrixManager.h"
#include "render/MatrixStack.h"
#include "render/gl_funcs.h"
#include "game/GameWork.h"
#include "math/_Vector3.h"
#include "math/_Vector2.h"
#include "math/Colour.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <SDL_image.h>

// ---------------------------------------------------------------------------
// FBO helpers (matches test_renderer.cpp pattern)
// ---------------------------------------------------------------------------

typedef void  (APIENTRYP PFN_glGenFramebuffers_t)(GLsizei, GLuint*);
typedef void  (APIENTRYP PFN_glBindFramebuffer_t)(GLenum, GLuint);
typedef void  (APIENTRYP PFN_glGenRenderbuffers_t)(GLsizei, GLuint*);
typedef void  (APIENTRYP PFN_glBindRenderbuffer_t)(GLenum, GLuint);
typedef void  (APIENTRYP PFN_glRenderbufferStorage_t)(GLenum, GLenum, GLsizei, GLsizei);
typedef void  (APIENTRYP PFN_glFramebufferRenderbuffer_t)(GLenum, GLenum, GLenum, GLuint);
typedef void  (APIENTRYP PFN_glFramebufferTexture2D_t)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum(APIENTRYP PFN_glCheckFramebufferStatus_t)(GLenum);
typedef void  (APIENTRYP PFN_glDeleteFramebuffers_t)(GLsizei, const GLuint*);
typedef void  (APIENTRYP PFN_glDeleteRenderbuffers_t)(GLsizei, const GLuint*);
typedef void  (APIENTRYP PFN_glReadPixels_t2)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, GLvoid*);

static PFN_glGenFramebuffers_t      fa_glGenFramebuffers      = NULL;
static PFN_glBindFramebuffer_t      fa_glBindFramebuffer      = NULL;
static PFN_glGenRenderbuffers_t     fa_glGenRenderbuffers     = NULL;
static PFN_glBindRenderbuffer_t     fa_glBindRenderbuffer     = NULL;
static PFN_glRenderbufferStorage_t  fa_glRenderbufferStorage  = NULL;
static PFN_glFramebufferRenderbuffer_t fa_glFramebufferRenderbuffer = NULL;
static PFN_glFramebufferTexture2D_t fa_glFramebufferTexture2D = NULL;
static PFN_glCheckFramebufferStatus_t fa_glCheckFramebufferStatus = NULL;
static PFN_glDeleteFramebuffers_t   fa_glDeleteFramebuffers   = NULL;
static PFN_glDeleteRenderbuffers_t  fa_glDeleteRenderbuffers  = NULL;
static PFN_glReadPixels_t2          fa_glReadPixels           = NULL;

#define FA_FB_           0x8D40u
#define FA_RB_           0x8D41u
#define FA_COLOR0_       0x8CE0u
#define FA_DEPTH_ATTACH_ 0x8D00u
#define FA_DEPTH16_      0x81A5u
#define FA_FB_COMPLETE_  0x8CD5u
#define FA_RGBA8_        0x8058u

static bool FA_LoadFBOFunctions() {
#define FA_LOAD(name, T) fa_##name = (T)SDL_GL_GetProcAddress(#name); \
    if (!fa_##name) { fprintf(stderr, "SKIP: " #name " unavailable\n"); return false; }
    FA_LOAD(glGenFramebuffers,       PFN_glGenFramebuffers_t)
    FA_LOAD(glBindFramebuffer,       PFN_glBindFramebuffer_t)
    FA_LOAD(glGenRenderbuffers,      PFN_glGenRenderbuffers_t)
    FA_LOAD(glBindRenderbuffer,      PFN_glBindRenderbuffer_t)
    FA_LOAD(glRenderbufferStorage,   PFN_glRenderbufferStorage_t)
    FA_LOAD(glFramebufferRenderbuffer, PFN_glFramebufferRenderbuffer_t)
    FA_LOAD(glFramebufferTexture2D,  PFN_glFramebufferTexture2D_t)
    FA_LOAD(glCheckFramebufferStatus, PFN_glCheckFramebufferStatus_t)
    FA_LOAD(glDeleteFramebuffers,    PFN_glDeleteFramebuffers_t)
    FA_LOAD(glDeleteRenderbuffers,   PFN_glDeleteRenderbuffers_t)
#undef FA_LOAD
    fa_glReadPixels = (PFN_glReadPixels_t2)SDL_GL_GetProcAddress("glReadPixels");
    if (!fa_glReadPixels) {
        fprintf(stderr, "SKIP: glReadPixels unavailable\n");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// FBO struct
// ---------------------------------------------------------------------------
static const int FA_W = 512;
static const int FA_H = 128;

struct AlignFBO {
    GLuint fbo;
    GLuint colorTex;
    GLuint depthRbo;

    AlignFBO() : fbo(0), colorTex(0), depthRbo(0) {}

    bool Create() {
        glGenTextures(1, &colorTex);
        glBindTexture(GL_TEXTURE_2D, colorTex);
        glTexImage2D(GL_TEXTURE_2D, 0, (GLint)FA_RGBA8_, FA_W, FA_H,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);

        fa_glGenRenderbuffers(1, &depthRbo);
        fa_glBindRenderbuffer(FA_RB_, depthRbo);
        fa_glRenderbufferStorage(FA_RB_, FA_DEPTH16_, FA_W, FA_H);
        fa_glBindRenderbuffer(FA_RB_, 0);

        fa_glGenFramebuffers(1, &fbo);
        fa_glBindFramebuffer(FA_FB_, fbo);
        fa_glFramebufferTexture2D(FA_FB_, FA_COLOR0_, GL_TEXTURE_2D, colorTex, 0);
        fa_glFramebufferRenderbuffer(FA_FB_, FA_DEPTH_ATTACH_, FA_RB_, depthRbo);

        GLenum status = fa_glCheckFramebufferStatus(FA_FB_);
        if (status != FA_FB_COMPLETE_) {
            fprintf(stderr, "SKIP: AlignFBO incomplete (status=0x%x)\n", (unsigned)status);
            fa_glBindFramebuffer(FA_FB_, 0);
            return false;
        }
        return true;
    }

    void Bind() {
        fa_glBindFramebuffer(FA_FB_, fbo);
        glViewport(0, 0, FA_W, FA_H);
    }

    void Unbind() {
        fa_glBindFramebuffer(FA_FB_, 0);
    }

    void ReadPixels(unsigned char* buf) {
        // Stage-2 2D batching: drain pending 2D draws before the readback.
        Renderer::GetInstance()->Flush2D();
        fa_glReadPixels(0, 0, FA_W, FA_H, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    }

    void Destroy() {
        if (fbo)      { fa_glDeleteFramebuffers(1, &fbo); fbo = 0; }
        if (colorTex) { glDeleteTextures(1, &colorTex); colorTex = 0; }
        if (depthRbo) { fa_glDeleteRenderbuffers(1, &depthRbo); depthRbo = 0; }
    }
};

// ---------------------------------------------------------------------------
// Ortho: FA_W x FA_H pixel space (Y up, same as test_renderer).
// SetupOrtho(top, bottom, left, right, near, far):
//   top=FA_H, bottom=0, left=0, right=FA_W, near=1, far=-1.
// anchorX is in pixel space: pixels map 1:1 to world units.
// ---------------------------------------------------------------------------
static void FA_SetupPixelOrtho() {
    MatrixManager& mm = MatrixManager::GetInstance();
    mm.SetupOrtho((float)FA_H, 0.0f, 0.0f, (float)FA_W, 1.0f, -1.0f);
    mm.GetViewStack().Reset();
    mm.GetWorldStack().Reset();
    MatrixManager::GetInstance().UploadModelViewOnly();
}

// ---------------------------------------------------------------------------
// Scan pixel buffer (RGBA, bottom-up GL order) for bounding box of lit pixels.
// A pixel is "lit" if R > 32 (font is white on black; anything above noise).
// buf: FA_W * FA_H * 4 bytes, row 0 = bottom row.
// Returns false if no lit pixels found.
// ---------------------------------------------------------------------------
static bool FA_ScanBBox(const unsigned char* buf, int* outXmin, int* outXmax) {
    *outXmin = FA_W;
    *outXmax = -1;
    for (int y = 0; y < FA_H; ++y) {
        for (int x = 0; x < FA_W; ++x) {
            const unsigned char* p = buf + ((size_t)y * FA_W + (size_t)x) * 4;
            if (p[0] > 32) {   // R channel bright -> glyph pixel
                if (x < *outXmin) *outXmin = x;
                if (x > *outXmax) *outXmax = x;
            }
        }
    }
    return (*outXmax >= *outXmin);
}

// ---------------------------------------------------------------------------
// Render one alignment case:
//   - Clear FBO to black.
//   - Set up pixel ortho.
//   - Call Font::DrawString at anchor (FA_W/2, FA_H/2) with given alignment.
//   - Read pixels.
//   - Scan for bounding box.
// outPixels: if non-null, receives the raw RGBA FBO pixels (FA_W*FA_H*4 bytes,
//   bottom-up GL order). Caller must supply a buffer of that size.
// Returns false if rendering fails (no lit pixels).
// ---------------------------------------------------------------------------
static bool FA_RenderAndScan(AlignFBO& fbo, Mortar::Font* font, const char* text,
                              int alignment, int* outXmin, int* outXmax,
                              unsigned char* outPixels = NULL) {
    fbo.Bind();

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    FA_SetupPixelOrtho();

    // Anchor at horizontal center of FBO.
    const float anchorX = (float)(FA_W / 2);
    const float anchorY = (float)(FA_H / 2);
    const float scale   = 32.0f;   // render at 32px -- readable but not oversized

    _Vector3<float> pos(anchorX, anchorY, 0.0f);
    Colour white(255, 255, 255, 255);

    font->DrawString(scale, 1.0f, 0.0f, text, pos, white, alignment);

    unsigned char buf[FA_W * FA_H * 4];
    fbo.ReadPixels(buf);
    fbo.Unbind();

    if (outPixels) {
        std::memcpy(outPixels, buf, (size_t)FA_W * FA_H * 4);
    }

    if (!FA_ScanBBox(buf, outXmin, outXmax)) {
        fprintf(stderr, "  [scan] no lit pixels (alignment=0x%02X) -- font not rendering?\n",
                alignment);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// PNG composite save helpers.
// ---------------------------------------------------------------------------

// Save one row's RGBA pixels (FA_W x FA_H, bottom-up GL) as a PNG after:
//   - flipping vertically (GL bottom-up -> top-down for PNG viewers)
//   - painting a 2-px wide magenta vertical line at anchorX
// name: <suite>/<case> screenshot name (harness builds the path + dirs).
// Returns true on success.
static bool FA_SaveRowPng(fn::TestHarness& h, const unsigned char* glPixels,
                          int anchorX, const char* name) {
    // Flip bottom-up to top-down into a working buffer.
    unsigned char* flipped = (unsigned char*)std::malloc((size_t)FA_W * FA_H * 4);
    if (!flipped) return false;
    for (int y = 0; y < FA_H; ++y) {
        std::memcpy(flipped + (size_t)y * FA_W * 4,
                    glPixels + (size_t)(FA_H - 1 - y) * FA_W * 4,
                    (size_t)FA_W * 4);
    }

    // Paint magenta (255, 0, 255) reference line at anchorX (2 px wide).
    for (int y = 0; y < FA_H; ++y) {
        for (int dx = 0; dx < 2; ++dx) {
            int col = anchorX + dx;
            if (col < 0 || col >= FA_W) continue;
            unsigned char* p = flipped + ((size_t)y * FA_W + (size_t)col) * 4;
            p[0] = 255;  // R
            p[1] = 0;    // G
            p[2] = 255;  // B
            p[3] = 255;  // A
        }
    }

    SDL_Surface* surf = SDL_CreateRGBSurfaceFrom(
        flipped, FA_W, FA_H,
        32,          // bits per pixel
        FA_W * 4,    // pitch (bytes per row)
        0x000000FFu, // Rmask
        0x0000FF00u, // Gmask
        0x00FF0000u, // Bmask
        0xFF000000u);// Amask
    if (!surf) {
        fprintf(stderr, "[font_align] SDL_CreateRGBSurfaceFrom failed: %s\n", SDL_GetError());
        std::free(flipped);
        return false;
    }
    bool ok = h.SavePng(surf, name);
    SDL_FreeSurface(surf);
    std::free(flipped);
    return ok;
}

// Save composite PNG: numRows rows stacked vertically, each FA_W x FA_H.
// glPixelRows: array of numRows pointers, each pointing to FA_W*FA_H*4 bytes (GL bottom-up RGBA).
// Returns true on success.
static bool FA_SaveCompositePng(fn::TestHarness& h, const unsigned char* const* glPixelRows,
                                int numRows, int anchorX, const char* name) {
    int totalH = FA_H * numRows;
    unsigned char* canvas = (unsigned char*)std::malloc((size_t)FA_W * totalH * 4);
    if (!canvas) return false;

    // Fill dark background (very dark grey so the black font row background
    // is distinguishable from the inter-row separator).
    std::memset(canvas, 0x10, (size_t)FA_W * totalH * 4);
    // Force alpha to 255 everywhere.
    for (int i = 3; i < FA_W * totalH * 4; i += 4) {
        canvas[i] = 255;
    }

    for (int row = 0; row < numRows; ++row) {
        const unsigned char* src = glPixelRows[row];
        if (!src) continue;
        // Flip GL bottom-up row into top-down canvas slot: canvas row 'row' starts at
        // canvas y = row * FA_H, and GL row 0 is the BOTTOM of the FBO.
        int dstRowBase = row * FA_H;
        for (int y = 0; y < FA_H; ++y) {
            // canvas y = dstRowBase + y; GL y = FA_H - 1 - y (flip).
            std::memcpy(canvas + ((size_t)(dstRowBase + y) * FA_W) * 4,
                        src    + (size_t)(FA_H - 1 - y) * FA_W * 4,
                        (size_t)FA_W * 4);
        }
    }

    // Paint magenta vertical reference line at anchorX across the entire height.
    for (int y = 0; y < totalH; ++y) {
        for (int dx = 0; dx < 2; ++dx) {
            int col = anchorX + dx;
            if (col < 0 || col >= FA_W) continue;
            unsigned char* p = canvas + ((size_t)y * FA_W + (size_t)col) * 4;
            p[0] = 255;
            p[1] = 0;
            p[2] = 255;
            p[3] = 255;
        }
    }

    SDL_Surface* surf = SDL_CreateRGBSurfaceFrom(
        canvas, FA_W, totalH,
        32,
        FA_W * 4,
        0x000000FFu,
        0x0000FF00u,
        0x00FF0000u,
        0xFF000000u);
    if (!surf) {
        fprintf(stderr, "[font_align] composite SDL_CreateRGBSurfaceFrom failed: %s\n",
                SDL_GetError());
        std::free(canvas);
        return false;
    }
    bool ok = h.SavePng(surf, name);
    SDL_FreeSurface(surf);
    std::free(canvas);
    if (!ok) return false;
    printf("[font_align] composite %dx%d (%d rows)\n", FA_W, totalH, numRows);
    return true;
}

// ---------------------------------------------------------------------------
// Scale x Alignment matrix screenshot (SM_*)
// 6 scale rows (16,24,32,48,64,96 px) x 5 alignment columns.
// Column order: LEFT(0x00), CTR(0x03), RIGHT(0x02), 0x0D, 0x0F.
// Row order (top->bottom): 16 24 32 48 64 96 px.
// Each cell: "123" at that scale/align, magenta line at anchor X.
// Canvas: SM_NUM_ALIGNS*SM_CELL_W x SM_NUM_SCALES*SM_CELL_H px.
// Uses a separate FBO (SM_FBO_W x SM_FBO_H) so large scales don't clip.
// ---------------------------------------------------------------------------

static const int SM_FBO_W         = 600;   // FBO wider than cell; anchor at centre
static const int SM_FBO_H         = 200;   // taller than FA_H to hold scale=96 glyphs
static const int SM_CELL_W        = 250;   // extracted cell width per column
static const int SM_CELL_H        = 200;   // extracted cell height (== SM_FBO_H)
static const int SM_FBO_ANCHOR_X  = 300;   // SM_FBO_W / 2
static const int SM_FBO_ANCHOR_Y  = 100;   // SM_FBO_H / 2
static const int SM_CELL_ANCHOR_X = 125;   // SM_CELL_W / 2
static const int SM_EXTRACT_LEFT  = 175;   // SM_FBO_ANCHOR_X - SM_CELL_ANCHOR_X
static const int SM_NUM_SCALES    = 6;
static const int SM_NUM_ALIGNS    = 5;

static const float SM_SCALES[SM_NUM_SCALES] = { 16.0f, 24.0f, 32.0f, 48.0f, 64.0f, 96.0f };
static const int   SM_ALIGNS[SM_NUM_ALIGNS] = { 0x0, 0x3, 0x2, 0x0D, 0x0F };

struct SmMatrixFBO {
    GLuint fbo;
    GLuint colorTex;
    GLuint depthRbo;

    SmMatrixFBO() : fbo(0), colorTex(0), depthRbo(0) {}

    bool Create() {
        glGenTextures(1, &colorTex);
        glBindTexture(GL_TEXTURE_2D, colorTex);
        glTexImage2D(GL_TEXTURE_2D, 0, (GLint)FA_RGBA8_, SM_FBO_W, SM_FBO_H,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);

        fa_glGenRenderbuffers(1, &depthRbo);
        fa_glBindRenderbuffer(FA_RB_, depthRbo);
        fa_glRenderbufferStorage(FA_RB_, FA_DEPTH16_, SM_FBO_W, SM_FBO_H);
        fa_glBindRenderbuffer(FA_RB_, 0);

        fa_glGenFramebuffers(1, &fbo);
        fa_glBindFramebuffer(FA_FB_, fbo);
        fa_glFramebufferTexture2D(FA_FB_, FA_COLOR0_, GL_TEXTURE_2D, colorTex, 0);
        fa_glFramebufferRenderbuffer(FA_FB_, FA_DEPTH_ATTACH_, FA_RB_, depthRbo);

        GLenum status = fa_glCheckFramebufferStatus(FA_FB_);
        if (status != FA_FB_COMPLETE_) {
            fprintf(stderr, "[scale_matrix] SmMatrixFBO incomplete (0x%x)\n", (unsigned)status);
            fa_glBindFramebuffer(FA_FB_, 0);
            return false;
        }
        return true;
    }

    void Bind() {
        fa_glBindFramebuffer(FA_FB_, fbo);
        glViewport(0, 0, SM_FBO_W, SM_FBO_H);
    }

    void Unbind() { fa_glBindFramebuffer(FA_FB_, 0); }

    void ReadPixels(unsigned char* buf) {
        // Stage-2 2D batching: drain pending 2D draws before the readback.
        Renderer::GetInstance()->Flush2D();
        fa_glReadPixels(0, 0, SM_FBO_W, SM_FBO_H, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    }

    void Destroy() {
        if (fbo)      { fa_glDeleteFramebuffers(1, &fbo); fbo = 0; }
        if (colorTex) { glDeleteTextures(1, &colorTex); colorTex = 0; }
        if (depthRbo) { fa_glDeleteRenderbuffers(1, &depthRbo); depthRbo = 0; }
    }
};

static void SM_SetupPixelOrtho() {
    MatrixManager& mm = MatrixManager::GetInstance();
    mm.SetupOrtho((float)SM_FBO_H, 0.0f, 0.0f, (float)SM_FBO_W, 1.0f, -1.0f);
    mm.GetViewStack().Reset();
    mm.GetWorldStack().Reset();
    MatrixManager::GetInstance().UploadModelViewOnly();
}

// Render scale x alignment matrix and save scale_matrix.png.
// Must be called while the GL context is live (before Shutdown).
static void SM_RenderMatrix(fn::TestHarness& h, Mortar::Font* font, const char* text) {
    SmMatrixFBO smfbo;
    if (!smfbo.Create()) {
        fprintf(stderr, "[scale_matrix] SmMatrixFBO create failed -- skipping\n");
        return;
    }

    int totalW = SM_NUM_ALIGNS * SM_CELL_W;   // 5 * 250 = 1250
    int totalH = SM_NUM_SCALES * SM_CELL_H;   // 6 * 200 = 1200

    unsigned char* canvas = (unsigned char*)std::malloc((size_t)totalW * (size_t)totalH * 4);
    unsigned char* fboBuf = (unsigned char*)std::malloc((size_t)SM_FBO_W * (size_t)SM_FBO_H * 4);
    if (!canvas || !fboBuf) {
        fprintf(stderr, "[scale_matrix] out of memory\n");
        std::free(canvas);
        std::free(fboBuf);
        smfbo.Destroy();
        return;
    }

    // Dark background (very dark grey, alpha=255).
    std::memset(canvas, 0x18, (size_t)totalW * (size_t)totalH * 4);
    for (int i = 3; i < totalW * totalH * 4; i += 4)
        canvas[i] = 255;

    for (int row = 0; row < SM_NUM_SCALES; ++row) {
        float scale = SM_SCALES[row];
        for (int col = 0; col < SM_NUM_ALIGNS; ++col) {
            int align = SM_ALIGNS[col];

            smfbo.Bind();
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            SM_SetupPixelOrtho();

            _Vector3<float> pos((float)SM_FBO_ANCHOR_X, (float)SM_FBO_ANCHOR_Y, 0.0f);
            Colour white(255, 255, 255, 255);
            font->DrawString(scale, 1.0f, 0.0f, text, pos, white, align);

            smfbo.ReadPixels(fboBuf);
            smfbo.Unbind();

            // Copy extracted window [SM_EXTRACT_LEFT, SM_EXTRACT_LEFT+SM_CELL_W) from FBO
            // into the canvas cell at (col*SM_CELL_W, row*SM_CELL_H), flipping GL bottom-up
            // to canvas top-down.
            int dstCellX = col * SM_CELL_W;
            int dstCellY = row * SM_CELL_H;

            for (int cy = 0; cy < SM_CELL_H; ++cy) {
                int fboY = SM_CELL_H - 1 - cy;
                for (int cx = 0; cx < SM_CELL_W; ++cx) {
                    int fboX = SM_EXTRACT_LEFT + cx;
                    if (fboX < 0 || fboX >= SM_FBO_W) continue;
                    const unsigned char* src =
                        fboBuf + ((size_t)fboY * (size_t)SM_FBO_W + (size_t)fboX) * 4;
                    unsigned char* dst =
                        canvas + ((size_t)(dstCellY + cy) * (size_t)totalW +
                                  (size_t)(dstCellX + cx)) * 4;
                    dst[0] = src[0]; dst[1] = src[1];
                    dst[2] = src[2]; dst[3] = src[3];
                }
            }
        }
    }

    // Magenta 2-px vertical reference lines at each column's anchor X (across all rows).
    for (int col = 0; col < SM_NUM_ALIGNS; ++col) {
        int lineX = col * SM_CELL_W + SM_CELL_ANCHOR_X;
        for (int y = 0; y < totalH; ++y) {
            for (int dx = 0; dx < 2; ++dx) {
                int x = lineX + dx;
                if (x < 0 || x >= totalW) continue;
                unsigned char* p = canvas + ((size_t)y * (size_t)totalW + (size_t)x) * 4;
                p[0] = 255; p[1] = 0; p[2] = 255; p[3] = 255;
            }
        }
    }

    // 1-px dark separator lines between rows and columns.
    for (int row = 1; row < SM_NUM_SCALES; ++row) {
        int sepY = row * SM_CELL_H;
        for (int x = 0; x < totalW; ++x) {
            unsigned char* p = canvas + ((size_t)sepY * (size_t)totalW + (size_t)x) * 4;
            p[0] = 0x50; p[1] = 0x50; p[2] = 0x50; p[3] = 255;
        }
    }
    for (int col = 1; col < SM_NUM_ALIGNS; ++col) {
        int sepX = col * SM_CELL_W;
        for (int y = 0; y < totalH; ++y) {
            unsigned char* p = canvas + ((size_t)y * (size_t)totalW + (size_t)sepX) * 4;
            p[0] = 0x50; p[1] = 0x50; p[2] = 0x50; p[3] = 255;
        }
    }

    SDL_Surface* surf = SDL_CreateRGBSurfaceFrom(
        canvas, totalW, totalH,
        32, totalW * 4,
        0x000000FFu, 0x0000FF00u, 0x00FF0000u, 0xFF000000u);
    if (surf) {
        if (h.SavePng(surf, "font_align/scale_matrix")) {
            printf("[font_align] scale_matrix %dx%d\n", totalW, totalH);
            printf("[font_align] cols left->right: LEFT(0x00) CTR(0x03) RIGHT(0x02) 0x0D 0x0F\n");
            printf("[font_align] rows top->bottom: 16 24 32 48 64 96 px\n");
            printf("[font_align] magenta=anchor; LEFT:text right, CTR:straddles, RIGHT:text left\n");
        }
        SDL_FreeSurface(surf);
    } else {
        fprintf(stderr, "[font_align] SDL_CreateRGBSurfaceFrom failed for scale_matrix\n");
    }

    smfbo.Destroy();
    std::free(canvas);
    std::free(fboBuf);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

// Number of alignment cases (rows in composite PNG).
static const int FA_NUM_CASES = 5;
// Alignment flags for each row, top to bottom.
static const int FA_ALIGN_FLAGS[FA_NUM_CASES] = { 0x0, 0x2, 0x3, 0x0D, 0x0F };
// Labels for each row (used only in log messages).
static const char* const FA_ALIGN_LABELS[FA_NUM_CASES] = {
    "0x0 LEFT",
    "0x2 RIGHT",
    "0x3 CENTER",
    "0x0D score-left",
    "0x0F bonus-center"
};

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "font_align");
    h.SetInitFrames(5);
    if (!h.ParseFlags()) return 1;

    // InitComponent boots the full game (assets loaded) and clears the HUD
    // so we get a blank canvas. If it fails (no GL, no assets), skip.
    if (!h.InitComponent()) {
        fprintf(stderr, "SKIP: InitComponent failed (no GL or assets)\n");
        return 77;
    }

    // pFontNumbers must be loaded (fruit_ninja_numbers.fnt is loaded by game.init).
    if (!game_work.pFontNumbers.IsValid()) {
        fprintf(stderr, "SKIP: pFontNumbers not loaded\n");
        return 77;
    }
    Mortar::Font* font = game_work.pFontNumbers.Get();
    if (!font) {
        fprintf(stderr, "SKIP: pFontNumbers.Get() = null\n");
        return 77;
    }

    // Check font has glyphs.
    if (font->m_GlyphCount == 0) {
        fprintf(stderr, "SKIP: font has 0 glyphs\n");
        return 77;
    }

    // Load FBO extension functions.
    if (!FA_LoadFBOFunctions()) {
        return 77;
    }

    AlignFBO fbo;
    if (!fbo.Create()) {
        return 77;
    }

    // The string to render. "123" is asymmetric (1 is narrow, 3 is wider)
    // so LEFT vs RIGHT alignment produces clearly different bounding boxes.
    const char* kText = "123";

    // Anchor pixel column: FA_W/2 = 256.
    const int anchorX = FA_W / 2;
    // Tolerance: 8 pixels.
    const int tol = 8;

    int xmin = 0, xmax = 0;
    int failures = 0;

    // Pixel capture buffers for PNG output -- one per case (RGBA, bottom-up GL order).
    static const size_t kRowBytes = (size_t)FA_W * FA_H * 4;
    unsigned char* pixBufs[FA_NUM_CASES];
    for (int i = 0; i < FA_NUM_CASES; ++i) {
        pixBufs[i] = (unsigned char*)std::malloc(kRowBytes);
        if (!pixBufs[i]) {
            fprintf(stderr, "WARN: out of memory for pixel capture buf %d\n", i);
        }
    }

    // Measure the string width at scale=32 so we know expected bbox width.
    float measW = font->MeasureWidth(32.0f, kText) * 32.0f;
    int   measWi = (int)(measW + 0.5f);
    printf("[font_align] string='%s' scale=32 anchorX=%d measuredWidth=%.1f px\n",
           kText, anchorX, measW);

    // -----------------------------------------------------------------------
    // Case 0x0 -- LEFT (lineOffset=0, text starts at anchor, extends right)
    // Expected: xmin ~= anchorX
    // -----------------------------------------------------------------------
    printf("[font_align] case 0x0 (LEFT):\n");
    if (!FA_RenderAndScan(fbo, font, kText, 0x0, &xmin, &xmax, pixBufs[0])) {
        fprintf(stderr, "FAIL [0x0]: no pixels\n");
        ++failures;
    } else {
        int mid = (xmin + xmax) / 2;
        int leftEdgeDelta = xmin - anchorX;
        printf("  xmin=%d xmax=%d mid=%d  leftEdgeDelta=%d (anchor=%d)\n",
               xmin, xmax, mid, leftEdgeDelta, anchorX);
        if (abs(leftEdgeDelta) > tol) {
            fprintf(stderr,
                "FAIL [0x0] LEFT: expected xmin ~= %d, got %d (delta=%d, tol=%d)\n",
                anchorX, xmin, leftEdgeDelta, tol);
            ++failures;
        } else {
            printf("PASS [0x0] LEFT: xmin=%d ~= anchor=%d\n", xmin, anchorX);
        }
        // Also verify text is to the RIGHT of anchor (not to the left).
        if (xmax <= anchorX) {
            fprintf(stderr,
                "FAIL [0x0] LEFT-extends-right: xmax=%d should be > anchorX=%d\n",
                xmax, anchorX);
            ++failures;
        }
    }

    // -----------------------------------------------------------------------
    // Case 0x1 -- CENTER bit (INERT per binary RE; should render same as LEFT)
    // Expected: xmin ~= anchorX (same as LEFT)
    // NOTE: 0x1 is NOT in the composite rows (it's identical to 0x0 LEFT
    // by RE; the composite shows the five *distinct* alignment semantics).
    // -----------------------------------------------------------------------
    printf("[font_align] case 0x1 (CENTER/inert):\n");
    int xmin1 = 0, xmax1 = 0;
    if (!FA_RenderAndScan(fbo, font, kText, 0x1, &xmin1, &xmax1)) {
        fprintf(stderr, "FAIL [0x1]: no pixels\n");
        ++failures;
    } else {
        int leftEdgeDelta1 = xmin1 - anchorX;
        printf("  xmin=%d xmax=%d  leftEdgeDelta=%d (anchor=%d)\n",
               xmin1, xmax1, leftEdgeDelta1, anchorX);
        if (abs(leftEdgeDelta1) > tol) {
            fprintf(stderr,
                "FAIL [0x1] CENTER-inert: expected xmin ~= %d (same as LEFT), got %d (delta=%d, tol=%d)\n",
                anchorX, xmin1, leftEdgeDelta1, tol);
            ++failures;
        } else {
            printf("PASS [0x1] CENTER-inert: xmin=%d ~= anchor=%d (same as LEFT)\n", xmin1, anchorX);
        }
        // Verify it matches the LEFT case within tolerance.
        if (abs(xmin1 - xmin) > tol) {
            fprintf(stderr,
                "FAIL [0x1] vs [0x0]: xmin differs by %d (should be ~same, tol=%d)\n",
                abs(xmin1 - xmin), tol);
            ++failures;
        } else {
            printf("PASS [0x1] vs [0x0]: xmin delta=%d <= tol=%d\n", abs(xmin1 - xmin), tol);
        }
    }

    // -----------------------------------------------------------------------
    // Case 0x2 -- RIGHT (right edge at anchor, extends left)
    // Expected: xmax ~= anchorX
    // -----------------------------------------------------------------------
    printf("[font_align] case 0x2 (RIGHT):\n");
    int xmin2 = 0, xmax2 = 0;
    if (!FA_RenderAndScan(fbo, font, kText, 0x2, &xmin2, &xmax2, pixBufs[1])) {
        fprintf(stderr, "FAIL [0x2]: no pixels\n");
        ++failures;
    } else {
        int rightEdgeDelta = xmax2 - anchorX;
        printf("  xmin=%d xmax=%d  rightEdgeDelta=%d (anchor=%d)\n",
               xmin2, xmax2, rightEdgeDelta, anchorX);
        if (abs(rightEdgeDelta) > tol) {
            fprintf(stderr,
                "FAIL [0x2] RIGHT: expected xmax ~= %d, got %d (delta=%d, tol=%d)\n",
                anchorX, xmax2, rightEdgeDelta, tol);
            ++failures;
        } else {
            printf("PASS [0x2] RIGHT: xmax=%d ~= anchor=%d\n", xmax2, anchorX);
        }
        // Verify text is to the LEFT of anchor.
        if (xmin2 >= anchorX) {
            fprintf(stderr,
                "FAIL [0x2] RIGHT-extends-left: xmin=%d should be < anchorX=%d\n",
                xmin2, anchorX);
            ++failures;
        }
    }

    // -----------------------------------------------------------------------
    // Case 0x3 -- true CENTER (midpoint at anchor)
    // Expected: mid ~= anchorX
    // -----------------------------------------------------------------------
    printf("[font_align] case 0x3 (CENTER):\n");
    int xmin3 = 0, xmax3 = 0;
    if (!FA_RenderAndScan(fbo, font, kText, 0x3, &xmin3, &xmax3, pixBufs[2])) {
        fprintf(stderr, "FAIL [0x3]: no pixels\n");
        ++failures;
    } else {
        int mid3 = (xmin3 + xmax3) / 2;
        int midDelta3 = mid3 - anchorX;
        printf("  xmin=%d xmax=%d mid=%d  midDelta=%d (anchor=%d)\n",
               xmin3, xmax3, mid3, midDelta3, anchorX);
        if (abs(midDelta3) > tol) {
            fprintf(stderr,
                "FAIL [0x3] CENTER: expected mid ~= %d, got mid=%d (delta=%d, tol=%d)\n",
                anchorX, mid3, midDelta3, tol);
            ++failures;
        } else {
            printf("PASS [0x3] CENTER: mid=%d ~= anchor=%d\n", mid3, anchorX);
        }
        // LEFT vs RIGHT must straddle anchor.
        if (xmin3 >= anchorX) {
            fprintf(stderr,
                "FAIL [0x3] CENTER-straddle: xmin=%d should be < anchorX=%d\n",
                xmin3, anchorX);
            ++failures;
        }
        if (xmax3 <= anchorX) {
            fprintf(stderr,
                "FAIL [0x3] CENTER-straddle: xmax=%d should be > anchorX=%d\n",
                xmax3, anchorX);
            ++failures;
        }
    }

    // -----------------------------------------------------------------------
    // Case 0x0D -- ScoreControl's score digits (0b1101: horiz=0x01 inert, vert=0xC center)
    // Key assertion: must render LEFT-anchored, same as 0x0.
    // -----------------------------------------------------------------------
    printf("[font_align] case 0x0D (score flag: horiz=0x01 inert = LEFT):\n");
    int xmin0d = 0, xmax0d = 0;
    if (!FA_RenderAndScan(fbo, font, kText, 0x0D, &xmin0d, &xmax0d, pixBufs[3])) {
        fprintf(stderr, "FAIL [0x0D]: no pixels\n");
        ++failures;
    } else {
        int leftEdgeDelta0d = xmin0d - anchorX;
        printf("  xmin=%d xmax=%d  leftEdgeDelta=%d (anchor=%d)\n",
               xmin0d, xmax0d, leftEdgeDelta0d, anchorX);
        // Primary assertion: left-anchored.
        if (abs(leftEdgeDelta0d) > tol) {
            fprintf(stderr,
                "FAIL [0x0D] score-left-anchor: expected xmin ~= %d, got %d (delta=%d, tol=%d)\n",
                anchorX, xmin0d, leftEdgeDelta0d, tol);
            fprintf(stderr,
                "  => The port's 0x0D alignment is NOT left-anchored. Binary says it MUST be.\n");
            ++failures;
        } else {
            printf("PASS [0x0D] score-left-anchor: xmin=%d ~= anchor=%d\n", xmin0d, anchorX);
        }
        // Must match 0x0 (LEFT) within tolerance.
        if (abs(xmin0d - xmin) > tol) {
            fprintf(stderr,
                "FAIL [0x0D] vs [0x0]: xmin differs by %d (should be ~same as LEFT, tol=%d)\n",
                abs(xmin0d - xmin), tol);
            ++failures;
        } else {
            printf("PASS [0x0D] vs [0x0]: xmin delta=%d <= tol=%d\n",
                   abs(xmin0d - xmin), tol);
        }
        // Must NOT match RIGHT case.
        if (abs(xmin0d - xmin2) <= tol && abs(xmax0d - xmax2) <= tol) {
            fprintf(stderr,
                "WARN [0x0D] matches RIGHT case -- may be right-anchored instead of left\n");
            // Only fail if the primary assertion also failed.
        }
    }

    // -----------------------------------------------------------------------
    // Case 0x0F -- bonus popup flag (0b1111: horiz=0x03 = true center, vert=0xC)
    // Expected: midpoint ~= anchorX
    // -----------------------------------------------------------------------
    printf("[font_align] case 0x0F (bonus flag: horiz=0x03 = true CENTER):\n");
    int xmin0f = 0, xmax0f = 0;
    if (!FA_RenderAndScan(fbo, font, kText, 0x0F, &xmin0f, &xmax0f, pixBufs[4])) {
        fprintf(stderr, "FAIL [0x0F]: no pixels\n");
        ++failures;
    } else {
        int mid0f = (xmin0f + xmax0f) / 2;
        int midDelta0f = mid0f - anchorX;
        printf("  xmin=%d xmax=%d mid=%d  midDelta=%d (anchor=%d)\n",
               xmin0f, xmax0f, mid0f, midDelta0f, anchorX);
        if (abs(midDelta0f) > tol) {
            fprintf(stderr,
                "FAIL [0x0F] bonus-center: expected mid ~= %d, got mid=%d (delta=%d, tol=%d)\n",
                anchorX, mid0f, midDelta0f, tol);
            ++failures;
        } else {
            printf("PASS [0x0F] bonus-center: mid=%d ~= anchor=%d\n", mid0f, anchorX);
        }
        // Must match 0x3 center within tolerance.
        if (abs(mid0f - (xmin3 + xmax3) / 2) > tol) {
            fprintf(stderr,
                "FAIL [0x0F] vs [0x3]: midpoint differs by %d (should be ~same, tol=%d)\n",
                abs(mid0f - (xmin3 + xmax3) / 2), tol);
            ++failures;
        } else {
            printf("PASS [0x0F] vs [0x3]: midpoint delta=%d <= tol=%d\n",
                   abs(mid0f - (xmin3 + xmax3) / 2), tol);
        }
    }

    // -----------------------------------------------------------------------
    // Sanity: LEFT and RIGHT must be clearly different (> half the text width
    // apart), so the tolerance cannot mask a LEFT/RIGHT confusion.
    // -----------------------------------------------------------------------
    if (xmax >= xmin && xmax2 >= xmin2) {
        int leftRightSep = abs(xmin - xmin2);
        if (leftRightSep < measWi / 2 && measWi > 8) {
            fprintf(stderr,
                "WARN: LEFT xmin=%d vs RIGHT xmin=%d separation=%d < half-width=%d"
                " -- font may be too small to distinguish\n",
                xmin, xmin2, leftRightSep, measWi / 2);
        } else {
            printf("[sanity] LEFT-RIGHT xmin separation=%d (expected ~%d px)\n",
                   leftRightSep, measWi);
        }
    }

    fbo.Destroy();

    // -----------------------------------------------------------------------
    // PNG output -- written unconditionally after assertions so the images
    // are always available regardless of pass/fail.
    // Rows (top to bottom): 0x0 LEFT, 0x2 RIGHT, 0x3 CENTER, 0x0D score, 0x0F bonus.
    // A magenta 2-px vertical line at anchorX (pixel column FA_W/2=256) is
    // painted into every row so the viewer can see exactly where the anchor is.
    // -----------------------------------------------------------------------
    printf("[font_align] writing PNG output to tmp/test/screenshots/font_align/\n");

    // Scale x alignment matrix (6 scales x 5 align flags).
    SM_RenderMatrix(h, font, kText);

    // Per-flag individual PNGs.
    {
        // hex names match alignment flag values: 00.png, 02.png, 03.png, 0d.png, 0f.png
        static const char* const kFlagNames[FA_NUM_CASES] = { "00", "02", "03", "0d", "0f" };
        for (int i = 0; i < FA_NUM_CASES; ++i) {
            if (!pixBufs[i]) continue;
            char name[256];
            std::snprintf(name, sizeof(name), "font_align/%s", kFlagNames[i]);
            FA_SaveRowPng(h, pixBufs[i], anchorX, name);
        }
    }

    // Composite PNG: 5 rows stacked.
    {
        const unsigned char* rowPtrs[FA_NUM_CASES];
        for (int i = 0; i < FA_NUM_CASES; ++i) {
            rowPtrs[i] = pixBufs[i];
        }
        FA_SaveCompositePng(h, rowPtrs, FA_NUM_CASES, anchorX, "font_align/all");
    }

    // Free pixel capture buffers.
    for (int i = 0; i < FA_NUM_CASES; ++i) {
        std::free(pixBufs[i]);
        pixBufs[i] = NULL;
    }

    if (failures == 0) {
        printf("PASS: all font alignment cases ok\n");
        return h.Shutdown();
    }
    fprintf(stderr, "FAIL: %d font alignment case(s) failed\n", failures);
    h.Shutdown();
    return 1;
}
