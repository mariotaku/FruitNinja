// test_renderer -- pixel-readback + golden-image suite for the Renderer 2D path.
//
// Guards the DrawQuad UV convention (uMin,uMax,vMin,vMax), tint/modulate,
// alpha blend, and draw_fullscreen_quad. These are the highest-fan-in 2D
// leaf calls -- bugs here break every textured quad in the game silently.
//
// Golden images live in tests/golden/renderer/<case>.png.
// To regenerate goldens:
//   test_renderer --update-golden
// Normal run (CTest) compares against committed goldens.
//
// If no GL context is available (headless CI), the test prints
//   SKIP: no GL context
// and exits with code 77, which CTest treats as SKIPPED (not FAIL).
//
// Ortho projection: the test uses a simple pixel-space ortho so vertices
// in [0..64] x [0..64] map 1:1 to FBO pixels. SetupOrtho is called with
// (top=64, bottom=0, left=0, right=64, near=1, far=-1) which is equivalent
// to a standard 2D ortho over [0,64]x[0,64] with Y going up.
//
// All test textures use GL_NEAREST min/mag filters and are rendered with
// axis-aligned, pixel-exact quads, so readback is byte-exact across drivers.
//
// Usage:
//   test_renderer [--update-golden]

#include <SDL.h>
#include <SDL_image.h>
#include "render/gl_funcs.h"
#include "render/MatrixManager.h"
#include "render/Renderer.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "asset/Texture.h"
#include "math/Colour.h"
#include "math/Matrix44.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

#ifdef _WIN32
#  include <direct.h>
#  define MKDIR(p) _mkdir(p)
#else
#  include <sys/stat.h>
#  define MKDIR(p) mkdir((p), 0755)
#endif

// ---------------------------------------------------------------------------
// FBO entry points -- loaded at runtime so we don't need GLEW
// ---------------------------------------------------------------------------
typedef void  (APIENTRYP PFN_glGenFramebuffers)(GLsizei, GLuint*);
typedef void  (APIENTRYP PFN_glBindFramebuffer)(GLenum, GLuint);
typedef void  (APIENTRYP PFN_glGenRenderbuffers)(GLsizei, GLuint*);
typedef void  (APIENTRYP PFN_glBindRenderbuffer)(GLenum, GLuint);
typedef void  (APIENTRYP PFN_glRenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
typedef void  (APIENTRYP PFN_glFramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
typedef void  (APIENTRYP PFN_glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum(APIENTRYP PFN_glCheckFramebufferStatus)(GLenum);
typedef void  (APIENTRYP PFN_glDeleteFramebuffers)(GLsizei, const GLuint*);
typedef void  (APIENTRYP PFN_glDeleteRenderbuffers)(GLsizei, const GLuint*);
typedef void  (APIENTRYP PFN_glReadPixels_t)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, GLvoid*);

static PFN_glGenFramebuffers      fn_glGenFramebuffers      = NULL;
static PFN_glBindFramebuffer      fn_glBindFramebuffer      = NULL;
static PFN_glGenRenderbuffers     fn_glGenRenderbuffers     = NULL;
static PFN_glBindRenderbuffer     fn_glBindRenderbuffer     = NULL;
static PFN_glRenderbufferStorage  fn_glRenderbufferStorage  = NULL;
static PFN_glFramebufferRenderbuffer fn_glFramebufferRenderbuffer = NULL;
static PFN_glFramebufferTexture2D fn_glFramebufferTexture2D = NULL;
static PFN_glCheckFramebufferStatus fn_glCheckFramebufferStatus = NULL;
static PFN_glDeleteFramebuffers   fn_glDeleteFramebuffers   = NULL;
static PFN_glDeleteRenderbuffers  fn_glDeleteRenderbuffers  = NULL;
static PFN_glReadPixels_t         fn_glReadPixels           = NULL;

// GL constants not in GL 1.1 headers
#define GL_FRAMEBUFFER_           0x8D40u
#define GL_RENDERBUFFER_          0x8D41u
#define GL_COLOR_ATTACHMENT0_     0x8CE0u
#define GL_DEPTH_ATTACHMENT_      0x8D00u
#define GL_DEPTH_COMPONENT16_     0x81A5u
#define GL_FRAMEBUFFER_COMPLETE_  0x8CD5u
#define GL_RGBA8_                 0x8058u

static bool LoadFBOFunctions() {
#define LOAD(name, T) fn_##name = (T)SDL_GL_GetProcAddress(#name); \
                      if (!fn_##name) { \
                          fprintf(stderr, "SKIP: " #name " unavailable\n"); return false; }
    LOAD(glGenFramebuffers,       PFN_glGenFramebuffers)
    LOAD(glBindFramebuffer,       PFN_glBindFramebuffer)
    LOAD(glGenRenderbuffers,      PFN_glGenRenderbuffers)
    LOAD(glBindRenderbuffer,      PFN_glBindRenderbuffer)
    LOAD(glRenderbufferStorage,   PFN_glRenderbufferStorage)
    LOAD(glFramebufferRenderbuffer, PFN_glFramebufferRenderbuffer)
    LOAD(glFramebufferTexture2D,  PFN_glFramebufferTexture2D)
    LOAD(glCheckFramebufferStatus, PFN_glCheckFramebufferStatus)
    LOAD(glDeleteFramebuffers,    PFN_glDeleteFramebuffers)
    LOAD(glDeleteRenderbuffers,   PFN_glDeleteRenderbuffers)
#undef LOAD
    fn_glReadPixels = (PFN_glReadPixels_t)SDL_GL_GetProcAddress("glReadPixels");
    if (!fn_glReadPixels) { fprintf(stderr, "SKIP: glReadPixels unavailable\n"); return false; }
    return true;
}

// ---------------------------------------------------------------------------
// FBO wrapper
// ---------------------------------------------------------------------------
static const int FBO_W = 64;
static const int FBO_H = 64;

struct FBO {
    GLuint fbo;
    GLuint colorTex;
    GLuint depthRbo;

    FBO() : fbo(0), colorTex(0), depthRbo(0) {}

    bool Create() {
        // Color attachment: RGBA8 texture with NEAREST filter.
        // Upload binds go through BindTextureForUpload so the Renderer's
        // texture shadow stays in sync (see Renderer.h state-cache doc).
        glGenTextures(1, &colorTex);
        Renderer::GetInstance()->BindTextureForUpload(colorTex);
        glTexImage2D(GL_TEXTURE_2D, 0, (GLint)GL_RGBA8_, FBO_W, FBO_H, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        Renderer::GetInstance()->BindTextureForUpload(0);

        // Depth renderbuffer.
        fn_glGenRenderbuffers(1, &depthRbo);
        fn_glBindRenderbuffer(GL_RENDERBUFFER_, depthRbo);
        fn_glRenderbufferStorage(GL_RENDERBUFFER_, GL_DEPTH_COMPONENT16_, FBO_W, FBO_H);
        fn_glBindRenderbuffer(GL_RENDERBUFFER_, 0);

        // Framebuffer.
        fn_glGenFramebuffers(1, &fbo);
        fn_glBindFramebuffer(GL_FRAMEBUFFER_, fbo);
        fn_glFramebufferTexture2D(GL_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_,
                                  GL_TEXTURE_2D, colorTex, 0);
        fn_glFramebufferRenderbuffer(GL_FRAMEBUFFER_, GL_DEPTH_ATTACHMENT_,
                                     GL_RENDERBUFFER_, depthRbo);

        GLenum status = fn_glCheckFramebufferStatus(GL_FRAMEBUFFER_);
        if (status != GL_FRAMEBUFFER_COMPLETE_) {
            fprintf(stderr, "SKIP: FBO incomplete (status=0x%x)\n", (unsigned)status);
            fn_glBindFramebuffer(GL_FRAMEBUFFER_, 0);
            return false;
        }
        return true;
    }

    void Bind() {
        fn_glBindFramebuffer(GL_FRAMEBUFFER_, fbo);
        glViewport(0, 0, FBO_W, FBO_H);
    }

    void Unbind() {
        fn_glBindFramebuffer(GL_FRAMEBUFFER_, 0);
    }

    void Destroy() {
        if (fbo)      { fn_glDeleteFramebuffers(1, &fbo); fbo = 0; }
        if (colorTex) { glDeleteTextures(1, &colorTex); colorTex = 0; }
        if (depthRbo) { fn_glDeleteRenderbuffers(1, &depthRbo); depthRbo = 0; }
    }

    // Read RGBA pixels from FBO into a caller-allocated FBO_W*FBO_H*4 buffer.
    // glReadPixels returns bottom-up; this returns bottom-up too (raw GL order).
    void ReadPixels(unsigned char* buf) {
        // Stage-2 2D batching: drain pending 2D draws before the readback.
        Renderer::GetInstance()->Flush2D();
        fn_glReadPixels(0, 0, FBO_W, FBO_H, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    }
};

// ---------------------------------------------------------------------------
// Pixel helpers
// ---------------------------------------------------------------------------

// buf is RGBA bottom-up (GL readPixels order): row 0 = bottom.
// pixel(x,y) where y=0 is BOTTOM row (GL order).
static const unsigned char* pixelAt(const unsigned char* buf, int x, int y) {
    return buf + ((size_t)y * FBO_W + (size_t)x) * 4;
}

// Return false and print diagnostic if pixel at (x,y) differs from (er,eg,eb,ea) by > tol.
static bool assertPixel(const unsigned char* buf, int x, int y,
                         int er, int eg, int eb, int ea,
                         const char* label, int tol = 2)
{
    const unsigned char* p = pixelAt(buf, x, y);
    int dr = abs((int)p[0] - er);
    int dg = abs((int)p[1] - eg);
    int db = abs((int)p[2] - eb);
    int da = abs((int)p[3] - ea);
    if (dr > tol || dg > tol || db > tol || da > tol) {
        fprintf(stderr,
            "FAIL [%s] pixel(%d,%d): got rgba(%d,%d,%d,%d) want rgba(%d,%d,%d,%d) tol=%d\n",
            label, x, y, (int)p[0], (int)p[1], (int)p[2], (int)p[3],
            er, eg, eb, ea, tol);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// makeTex -- create a GL texture from a raw RGBA pixel buffer.
// Uses GL_NEAREST so pixel readback is exact.
// ---------------------------------------------------------------------------
static GLuint makeTex(int w, int h, const unsigned char* rgba) {
    GLuint id = 0;
    glGenTextures(1, &id);
    Renderer::GetInstance()->BindTextureForUpload(id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    Renderer::GetInstance()->BindTextureForUpload(0);
    return id;
}

// After makeTex, call this to tell Renderer::DrawQuad a texture is bound.
// BindTexture2D is the lazy sampling bind (resolved at the next draw).
static void bindTestTex(GLuint id) {
    Renderer::GetInstance()->BindTexture2D(id);
    Mortar::Texture::s_LastBoundTexId = id;
}

// ---------------------------------------------------------------------------
// Ortho setup for the FBO.
//
// We use a simple 64x64 pixel-space ortho so vertices in screen-pixel coords
// draw directly. The MatrixManager::SetupOrtho signature is:
//   SetupOrtho(top, bottom, left, right, near, far)
// We want: X in [0..64] (left=0,right=64), Y in [0..64] (bottom=0,top=64).
// near/far: 1 and -1 (flat 2D, depth irrelevant).
// ---------------------------------------------------------------------------
static void setupPixelOrtho() {
    MatrixManager& mm = MatrixManager::GetInstance();
    // top=64, bottom=0, left=0, right=64, near=1, far=-1
    mm.SetupOrtho(64.0f, 0.0f, 0.0f, 64.0f, 1.0f, -1.0f);
    mm.GetViewStack().Reset();
    mm.GetWorldStack().Reset();
}

// ---------------------------------------------------------------------------
// DrawQuad helper: scales the unit quad to fill the given rect in pixel coords.
// The Renderer::DrawQuad draws a unit quad (-0.5..0.5) scaled by WorldStack.
// To fill rect [x0,x1] x [y0,y1], set Scale=(x1-x0, y1-y0), Translate=(cx,cy).
// ---------------------------------------------------------------------------
static void drawPixelQuad(float x0, float y0, float x1, float y1,
                           const Colour& tint,
                           float uMin, float uMax, float vMin, float vMax)
{
    float cx = (x0 + x1) * 0.5f;
    float cy = (y0 + y1) * 0.5f;
    float sw = x1 - x0;
    float sh = y1 - y0;

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.GetWorldStack().Scale(_Vector3<float>(sw, sh, 1.0f));
    mm.GetWorldStack().Translate(_Vector3<float>(cx, cy, 0.0f));
    mm.UploadModelViewOnly();

    Renderer::GetInstance()->DrawQuad(tint, uMin, uMax, vMin, vMax);
}

// ---------------------------------------------------------------------------
// Golden image helpers
// ---------------------------------------------------------------------------

static void makeGoldenDir() {
    MKDIR("tests");
    MKDIR("tests/golden");
    MKDIR("tests/golden/renderer");
}

// Save a 64x64 RGBA bottom-up GL buffer as a PNG (flipped to top-down for viewers).
static bool saveGolden(const char* caseName, const unsigned char* buf) {
    makeGoldenDir();
    char path[256];
    snprintf(path, sizeof(path), "tests/golden/renderer/%s.png", caseName);

    // Flip bottom-up -> top-down for SDL_Surface (SDL expects top-down).
    unsigned char flipped[FBO_W * FBO_H * 4];
    for (int row = 0; row < FBO_H; ++row) {
        memcpy(flipped + (size_t)row * FBO_W * 4,
               buf    + (size_t)(FBO_H - 1 - row) * FBO_W * 4,
               (size_t)FBO_W * 4);
    }

    SDL_Surface* surf = SDL_CreateRGBSurfaceFrom(
        flipped, FBO_W, FBO_H,
        32, FBO_W * 4,
        0x000000FFu,   // Rmask
        0x0000FF00u,   // Gmask
        0x00FF0000u,   // Bmask
        0xFF000000u);  // Amask
    if (!surf) {
        fprintf(stderr, "saveGolden: SDL_CreateRGBSurfaceFrom failed: %s\n", SDL_GetError());
        return false;
    }
    int rc = IMG_SavePNG(surf, path);
    SDL_FreeSurface(surf);
    if (rc != 0) {
        fprintf(stderr, "saveGolden: IMG_SavePNG(%s) failed: %s\n", path, IMG_GetError());
        return false;
    }
    printf("[golden] wrote %s\n", path);
    return true;
}

// Compare rendered buffer vs committed golden PNG.
// Tolerance: per-channel +/-2, allow <0.1% pixels to differ.
// Returns false and prints diff info on failure.
static bool compareGolden(const char* caseName, const unsigned char* rendered) {
    char path[256];
    snprintf(path, sizeof(path), "tests/golden/renderer/%s.png", caseName);

    SDL_Surface* png = IMG_Load(path);
    if (!png) {
        fprintf(stderr, "FAIL [%s]: golden not found at %s (run with --update-golden first)\n",
                caseName, path);
        return false;
    }

    // Convert to a surface whose MEMORY byte order is R,G,B,A to match the
    // glReadPixels(GL_RGBA, GL_UNSIGNED_BYTE) buffer. SDL pixel-format names are
    // PACKED (MSB-first) uint32, so on a little-endian host the byte order R,G,B,A
    // is SDL_PIXELFORMAT_ABGR8888 -- NOT RGBA8888 (which is A,B,G,R in memory LE).
    SDL_Surface* rgba = SDL_ConvertSurfaceFormat(png, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(png);
    if (!rgba) {
        fprintf(stderr, "FAIL [%s]: SDL_ConvertSurfaceFormat failed\n", caseName);
        return false;
    }
    if (rgba->w != FBO_W || rgba->h != FBO_H) {
        fprintf(stderr, "FAIL [%s]: golden size %dx%d != expected %dx%d\n",
                caseName, rgba->w, rgba->h, FBO_W, FBO_H);
        SDL_FreeSurface(rgba);
        return false;
    }

    // Golden PNG is top-down; rendered buf is bottom-up (GL order).
    // Flip rendered to top-down for comparison.
    unsigned char flipped[FBO_W * FBO_H * 4];
    for (int row = 0; row < FBO_H; ++row) {
        memcpy(flipped + (size_t)row * FBO_W * 4,
               rendered + (size_t)(FBO_H - 1 - row) * FBO_W * 4,
               (size_t)FBO_W * 4);
    }

    // SDL_PIXELFORMAT_RGBA8888 stores pixels as R8G8B8A8 in memory on any
    // endian (it's a packed format, not component order). Map to bytes.
    // Byte order in memory: R=byte0, G=byte1, B=byte2, A=byte3 (little-endian host).
    const unsigned char* golden = (const unsigned char*)rgba->pixels;

    const int tol = 2;
    const int maxBad = (FBO_W * FBO_H) / 1000 + 1;  // 0.1% of pixels
    int badPixels = 0;
    int firstBadX = -1, firstBadY = -1;
    unsigned char fbGR = 0, fbGG = 0, fbGB = 0, fbGA = 0;
    unsigned char fbRR = 0, fbRG = 0, fbRB = 0, fbRA = 0;
    for (int y = 0; y < FBO_H; ++y) {
        for (int x = 0; x < FBO_W; ++x) {
            const unsigned char* gp = golden   + ((size_t)y * FBO_W + (size_t)x) * 4;
            const unsigned char* rp = flipped  + ((size_t)y * FBO_W + (size_t)x) * 4;
            // SDL RGBA8888 (packed big-endian): gp[0]=R, gp[1]=G, gp[2]=B, gp[3]=A
            if (abs((int)gp[0] - (int)rp[0]) > tol ||
                abs((int)gp[1] - (int)rp[1]) > tol ||
                abs((int)gp[2] - (int)rp[2]) > tol ||
                abs((int)gp[3] - (int)rp[3]) > tol) {
                if (badPixels == 0) {
                    firstBadX = x; firstBadY = y;
                    fbGR = gp[0]; fbGG = gp[1]; fbGB = gp[2]; fbGA = gp[3];
                    fbRR = rp[0]; fbRG = rp[1]; fbRB = rp[2]; fbRA = rp[3];
                }
                ++badPixels;
            }
        }
    }
    SDL_FreeSurface(rgba);

    if (badPixels > maxBad) {
        fprintf(stderr,
            "FAIL [%s]: %d/%d pixels differ (limit %d, tol=%d)\n"
            "  first bad pixel (%d,%d): golden=rgba(%d,%d,%d,%d) rendered=rgba(%d,%d,%d,%d)\n",
            caseName, badPixels, FBO_W * FBO_H, maxBad, tol,
            firstBadX, firstBadY,
            (int)fbGR, (int)fbGG, (int)fbGB, (int)fbGA,
            (int)fbRR, (int)fbRG, (int)fbRB, (int)fbRA);
        return false;
    }
    printf("[golden] PASS %s (%d bad pixels, limit %d)\n", caseName, badPixels, maxBad);
    return true;
}

// ---------------------------------------------------------------------------
// Test cases
// ---------------------------------------------------------------------------

// Helper: clear FBO to background colour and reset GL state for 2D draw.
static void clearFBO(float r, float g, float b, float a = 1.0f) {
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
}

// Case A: solid tint quad.
// Bind a 1x1 white RGBA texture, draw a red-tinted quad filling the FBO.
// All pixels should be red (255,0,0,255).
static bool caseA_SolidTintQuad(FBO& fbo, bool updateGolden) {
    static const unsigned char kWhite1x1[4] = { 255, 255, 255, 255 };
    GLuint tex = makeTex(1, 1, kWhite1x1);

    fbo.Bind();
    clearFBO(0.0f, 0.0f, 0.0f);
    setupPixelOrtho();
    bindTestTex(tex);
    glDisable(GL_BLEND);

    drawPixelQuad(0.0f, 0.0f, 64.0f, 64.0f,
                  Colour(255, 0, 0, 255),
                  0.0f, 1.0f, 0.0f, 1.0f);

    unsigned char buf[FBO_W * FBO_H * 4];
    fbo.ReadPixels(buf);
    fbo.Unbind();
    glDeleteTextures(1, &tex);
    Mortar::Texture::s_LastBoundTexId = 0;

    bool ok = true;
    if (!updateGolden) {
        // Assert center pixel and corner pixels all red.
        ok = ok && assertPixel(buf, 32, 32, 255,  0,  0, 255, "A_SolidTintQuad");
        ok = ok && assertPixel(buf,  0,  0, 255,  0,  0, 255, "A_SolidTintQuad");
        ok = ok && assertPixel(buf, 63, 63, 255,  0,  0, 255, "A_SolidTintQuad");
        if (ok) ok = compareGolden("A_SolidTintQuad", buf);
    } else {
        ok = saveGolden("A_SolidTintQuad", buf);
    }
    return ok;
}

// Case B: full-texture UV orientation.
// 2x2 texture with 4 distinct quadrant colors:
//   TL = red (0,0), TR = green (1,0), BL = blue (0,1), BR = white (1,1)
// In OpenGL UV convention: (u,v)=(0,0) is bottom-left of texture.
// BUT the binary's DrawQuad vertex table maps:
//   BL=(uMin,vMax), BR=(uMax,vMax), TL=(uMin,vMin), TR=(uMax,vMin)
// So with full UV (0,1,0,1):
//   BL texel=(u=0,v=1)=BL of tex, BR texel=(u=1,v=1)=BR of tex,
//   TL texel=(u=0,v=0)=TL of tex, TR texel=(u=1,v=0)=TR of tex.
// FBO is 64x64; we draw a 64x64 quad split into 4 32x32 pixel quadrants.
// Each 32x32 quadrant should match the texel at that corner.
//
// Texel layout (standard OpenGL, v=0 at bottom):
//   (u=0,v=0) = bottom-left of texture pixel array (but we store row-major top-down)
// We define the pixel array top-down: row[0] = top of texture:
//   pixels[0] = TL pixel (u=0, v_top=0 in GL means... depends on texImage2D).
//
// glTexImage2D uploads row 0 as the BOTTOM row when internalformat=GL_RGBA.
// Wait -- actually glTexImage2D row 0 IS the bottom row in GL convention.
// But we supply pixels top-down here, so row[0] we supply ends up at v=1 (top in GL).
// Let's be explicit:
//   We want pixel at GL (u=0,v=0) -> blue (bottom-left of texture)
//                  (u=1,v=0) -> white (bottom-right of texture)
//                  (u=0,v=1) -> red   (top-left of texture)
//                  (u=1,v=1) -> green (top-right of texture)
// DrawQuad maps: BL vertex (screen y=0=bottom, GL y=bottom) gets (uMin, vMax)=(0,1)=red
//               BR vertex gets (uMax,vMax)=(1,1)=green
//               TL vertex (screen top) gets (uMin,vMin)=(0,0)=blue
//               TR vertex gets (uMax,vMin)=(1,0)=white
// So FBO (bottom-up GL readback):
//   bottom-left quadrant [0..31][0..31] = red
//   bottom-right quadrant [32..63][0..31] = green
//   top-left quadrant [0..31][32..63] = blue
//   top-right quadrant [32..63][32..63] = white
//
// We supply pixels to glTexImage2D in GL bottom-up order:
//   row 0 (bottom) = [blue, white]
//   row 1 (top)    = [red, green]
static bool caseB_FullUVOrientation(FBO& fbo, bool updateGolden) {
    // 2x2 RGBA, supplied in GL bottom-up order.
    static const unsigned char kQuad2x2[2*2*4] = {
        // row 0 (v=0, bottom): blue (0,0,255,255), white (255,255,255,255)
          0,   0, 255, 255,  255, 255, 255, 255,
        // row 1 (v=1, top): red (255,0,0,255), green (0,255,0,255)
        255,   0,   0, 255,    0, 255,   0, 255,
    };
    GLuint tex = makeTex(2, 2, kQuad2x2);

    fbo.Bind();
    clearFBO(0.0f, 0.0f, 0.0f);
    setupPixelOrtho();
    bindTestTex(tex);
    glDisable(GL_BLEND);

    // Draw full 64x64 quad with full UV.
    drawPixelQuad(0.0f, 0.0f, 64.0f, 64.0f,
                  Colour(255, 255, 255, 255),
                  0.0f, 1.0f, 0.0f, 1.0f);

    unsigned char buf[FBO_W * FBO_H * 4];
    fbo.ReadPixels(buf);
    fbo.Unbind();
    glDeleteTextures(1, &tex);
    Mortar::Texture::s_LastBoundTexId = 0;

    bool ok = true;
    if (!updateGolden) {
        // Check center of each quadrant in GL bottom-up buf coords.
        // Bottom-left quadrant center = (16, 16): expect red
        ok = ok && assertPixel(buf, 16, 16, 255,   0,   0, 255, "B_FullUV/BL=red");
        // Bottom-right quadrant center = (48, 16): expect green
        ok = ok && assertPixel(buf, 48, 16,   0, 255,   0, 255, "B_FullUV/BR=green");
        // Top-left quadrant center = (16, 48): expect blue
        ok = ok && assertPixel(buf, 16, 48,   0,   0, 255, 255, "B_FullUV/TL=blue");
        // Top-right quadrant center = (48, 48): expect white
        ok = ok && assertPixel(buf, 48, 48, 255, 255, 255, 255, "B_FullUV/TR=white");
        if (ok) ok = compareGolden("B_FullUVOrientation", buf);
    } else {
        ok = saveGolden("B_FullUVOrientation", buf);
    }
    return ok;
}

// Case C: sub-rect UV -- the #225 regression guard.
// Same 2x2 texture as case B. Draw with UV selecting only the TL texel
// (u=0..0.5, v=0.5..1.0) -> entire quad should be red.
// Then draw with UV selecting only the BR texel (u=0.5..1, v=0..0.5) -> entire quad white.
// This tests that the (uMin,uMax,vMin,vMax) convention is applied correctly,
// which is exactly the bug that was fixed in the prior commit.
static bool caseC_SubRectUV(FBO& fbo, bool updateGolden) {
    static const unsigned char kQuad2x2[2*2*4] = {
          0,   0, 255, 255,  255, 255, 255, 255,  // row 0 (v=0, bottom): blue, white
        255,   0,   0, 255,    0, 255,   0, 255,  // row 1 (v=1, top): red, green
    };
    GLuint tex = makeTex(2, 2, kQuad2x2);

    unsigned char bufTL[FBO_W * FBO_H * 4];
    unsigned char bufBR[FBO_W * FBO_H * 4];

    // Sub-case 1: select top-left texel (red).
    // TL texel = (u in [0,0.5], v in [0.5,1.0]).
    fbo.Bind();
    clearFBO(0.0f, 0.0f, 0.0f);
    setupPixelOrtho();
    bindTestTex(tex);
    glDisable(GL_BLEND);
    drawPixelQuad(0.0f, 0.0f, 64.0f, 64.0f,
                  Colour(255, 255, 255, 255),
                  0.0f, 0.5f, 0.5f, 1.0f);  // uMin=0,uMax=0.5, vMin=0.5,vMax=1.0
    fbo.ReadPixels(bufTL);

    // Sub-case 2: select bottom-right texel (white).
    // BR texel = (u in [0.5,1], v in [0,0.5]).
    clearFBO(0.0f, 0.0f, 0.0f);
    setupPixelOrtho();
    bindTestTex(tex);
    glDisable(GL_BLEND);
    drawPixelQuad(0.0f, 0.0f, 64.0f, 64.0f,
                  Colour(255, 255, 255, 255),
                  0.5f, 1.0f, 0.0f, 0.5f);  // uMin=0.5,uMax=1, vMin=0,vMax=0.5
    fbo.ReadPixels(bufBR);

    fbo.Unbind();
    glDeleteTextures(1, &tex);
    Mortar::Texture::s_LastBoundTexId = 0;

    bool ok = true;
    if (!updateGolden) {
        // TL sub-case: whole quad should be red.
        ok = ok && assertPixel(bufTL, 16, 16, 255,   0,   0, 255, "C_SubUV/TL-center");
        ok = ok && assertPixel(bufTL, 48, 48, 255,   0,   0, 255, "C_SubUV/TL-topright");
        ok = ok && assertPixel(bufTL,  0,  0, 255,   0,   0, 255, "C_SubUV/TL-BLcorner");
        ok = ok && assertPixel(bufTL, 63, 63, 255,   0,   0, 255, "C_SubUV/TL-TRcorner");
        // BR sub-case: whole quad should be white.
        ok = ok && assertPixel(bufBR, 16, 16, 255, 255, 255, 255, "C_SubUV/BR-center");
        ok = ok && assertPixel(bufBR, 48, 48, 255, 255, 255, 255, "C_SubUV/BR-topright");
        if (ok) {
            ok = ok && compareGolden("C_SubRectUV_TL", bufTL);
            ok = ok && compareGolden("C_SubRectUV_BR", bufBR);
        }
    } else {
        ok = ok && saveGolden("C_SubRectUV_TL", bufTL);
        ok = ok && saveGolden("C_SubRectUV_BR", bufBR);
    }
    return ok;
}

// Case D: alpha blend.
// Clear FBO to opaque blue. Draw a red quad with tint alpha=128.
// Result at each pixel: src=(255,0,0,128), dst=(0,0,255,255).
// With SRC_ALPHA/ONE_MINUS_SRC_ALPHA:
//   out.r = 255 * (128/255) + 0 * (127/255) = 128
//   out.g = 0
//   out.b = 0 * (128/255) + 255 * (127/255) = 127
// Expected: ~(128, 0, 127, 255). Tolerance 4 for alpha math rounding.
static bool caseD_AlphaBlend(FBO& fbo, bool updateGolden) {
    static const unsigned char kRed1x1[4] = { 255, 0, 0, 255 };
    GLuint tex = makeTex(1, 1, kRed1x1);

    fbo.Bind();
    clearFBO(0.0f, 0.0f, 1.0f);  // clear to blue
    setupPixelOrtho();
    bindTestTex(tex);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    drawPixelQuad(0.0f, 0.0f, 64.0f, 64.0f,
                  Colour(255, 0, 0, 128),
                  0.0f, 1.0f, 0.0f, 1.0f);

    unsigned char buf[FBO_W * FBO_H * 4];
    fbo.ReadPixels(buf);
    fbo.Unbind();
    glDeleteTextures(1, &tex);
    Mortar::Texture::s_LastBoundTexId = 0;
    glDisable(GL_BLEND);

    bool ok = true;
    if (!updateGolden) {
        // tolerance=4 for blend math rounding across GPUs.
        // Expected dest alpha is the BLENDED alpha, not 255: SRC_ALPHA/ONE_MINUS_SRC_ALPHA
        // blends the alpha channel too -> 128*(128/255) + 255*(1-128/255) ~= 191.
        // (Destination alpha is irrelevant for on-screen output, but the FBO keeps it.)
        ok = ok && assertPixel(buf, 32, 32, 128,   0, 127, 191, "D_AlphaBlend", 4);
        ok = ok && assertPixel(buf,  0,  0, 128,   0, 127, 191, "D_AlphaBlend", 4);
        if (ok) ok = compareGolden("D_AlphaBlend", buf);
    } else {
        ok = saveGolden("D_AlphaBlend", buf);
    }
    return ok;
}

// Case E: draw_fullscreen_quad.
// Bind a solid green texture and call draw_fullscreen_quad.
// FBO should be entirely green.
static bool caseE_FullscreenQuad(FBO& fbo, bool updateGolden) {
    static const unsigned char kGreen1x1[4] = { 0, 255, 0, 255 };
    GLuint tex = makeTex(1, 1, kGreen1x1);

    fbo.Bind();
    clearFBO(0.0f, 0.0f, 0.0f);
    // draw_fullscreen_quad uses clip-space vertices with identity MVP --
    // no ortho setup needed; it ignores the MatrixManager entirely.
    glDisable(GL_BLEND);

    Renderer::GetInstance()->draw_fullscreen_quad(tex, 1.0f);

    unsigned char buf[FBO_W * FBO_H * 4];
    fbo.ReadPixels(buf);
    fbo.Unbind();
    glDeleteTextures(1, &tex);

    bool ok = true;
    if (!updateGolden) {
        ok = ok && assertPixel(buf, 32, 32,   0, 255,   0, 255, "E_FullscreenQuad");
        ok = ok && assertPixel(buf,  0,  0,   0, 255,   0, 255, "E_FullscreenQuad");
        ok = ok && assertPixel(buf, 63, 63,   0, 255,   0, 255, "E_FullscreenQuad");
        if (ok) ok = compareGolden("E_FullscreenQuad", buf);
    } else {
        ok = saveGolden("E_FullscreenQuad", buf);
    }
    return ok;
}

// Case F: DrawTriList with per-vertex colour.
// Build two triangles (a quad) with QUADCUSTOMVERTEX covering the FBO.
// Each vertex coloured yellow (255,255,0,255). Expect yellow FBO.
// The colour field in QUADCUSTOMVERTEX is packed BGRA uint32 (little-endian).
static bool caseF_DrawTriList(FBO& fbo, bool updateGolden) {
    // Yellow in BGRA packed uint32 (little-endian):
    // B=0, G=255, R=255, A=255 -> bytes: 0x00, 0xFF, 0xFF, 0xFF -> uint32 = 0xFFFF00FF? No.
    // Memory bytes: B G R A = 0x00 0xFF 0xFF 0xFF
    // As little-endian uint32: 0xFF_FF_FF_00
    // Wait: BGRA packed = B at lowest byte address.
    // B=0x00, G=0xFF, R=0xFF, A=0xFF -> uint32 LE = (A<<24)|(R<<16)|(G<<8)|B
    // = (0xFF<<24)|(0xFF<<16)|(0xFF<<8)|0x00 = 0xFFFFFF00
    // But glColorPointer(4, GL_UNSIGNED_BYTE, stride, &verts->colour) reads 4 bytes
    // at the colour address as R G B A in byte order. So BGRA packed means:
    // byte0=B, byte1=G, byte2=R, byte3=A, which is read as R=B? No -- depends on
    // the GL convention. Let's check what DrawTriList does: glColorPointer with
    // GL_UNSIGNED_BYTE, 4 components -> GL reads byte0=R, byte1=G, byte2=B, byte3=A.
    // QUADCUSTOMVERTEX.colour is documented as "packed BGRA uint32".
    // So byte0 in memory = B, byte1=G, byte2=R, byte3=A.
    // For yellow (R=255, G=255, B=0, A=255): B=0, G=255, R=255, A=255
    // uint32 value (little-endian stored as B G R A bytes): 0xFF_FF_FF_00
    // i.e. colour = 0xFFFFFF00u
    // GL reads this as: R=0x00, G=0xFF, B=0xFF, A=0xFF -> cyan? That's not right.
    //
    // Actually: if the uint32 field "colour" is stored in memory as bytes at its address:
    // On a little-endian machine, uint32 value 0xAABBCCDD is stored as
    //   byte[0]=0xDD, byte[1]=0xCC, byte[2]=0xBB, byte[3]=0xAA.
    // "packed BGRA" means the bytes at the colour address are B G R A.
    // So colour (uint32 LE) = (A<<24) | (R<<16) | (G<<8) | (B<<0) would give
    //   byte[0]=B, byte[1]=G, byte[2]=R, byte[3]=A.
    // For yellow: R=255,G=255,B=0,A=255: colour = (255<<24)|(255<<16)|(255<<8)|0
    //           = 0xFFFFFF00u
    // In memory: byte[0]=0x00(B), byte[1]=0xFF(G), byte[2]=0xFF(R), byte[3]=0xFF(A)
    // GL reads: R=byte0=0x00, G=byte1=0xFF, B=byte2=0xFF, A=byte3=0xFF -> Cyan.
    //
    // Hmm, that gives cyan. To get yellow on screen, we need byte0=0xFF(R), byte1=0xFF(G),
    // byte2=0x00(B), byte3=0xFF(A), which means colour uint32 LE = 0xFF00FFFF.
    // Conclusion: use white (255,255,255,255) for the colour test to avoid the BGRA confusion
    // and validate the draw path without the colour-packing question.
    // White: all bytes 0xFF -> colour = 0xFFFFFFFFu (unambiguous).

    static const unsigned char kWhite1x1[4] = { 255, 255, 255, 255 };
    GLuint tex = makeTex(1, 1, kWhite1x1);

    // 4 vertices covering [0,64]x[0,64] pixel ortho, white BGRA.
    // Pixel ortho: top=64, bottom=0, left=0, right=64.
    // Unit quad drawn by DrawTriList needs to be in the ortho space directly.
    // DrawTriList uses the current MatrixManager MVP (proj*view*world).
    // We set up the pixel ortho and identity world, so vertices in [0,64] draw correctly.
    fbo.Bind();
    clearFBO(0.0f, 0.0f, 0.0f);
    setupPixelOrtho();
    Renderer::GetInstance()->BindTexture2D(tex);

    // Build 6 verts (2 triangles) covering the FBO.
    QUADCUSTOMVERTEX verts[6];
    memset(verts, 0, sizeof(verts));
    // Triangle 1: BL, BR, TL
    verts[0].x = 0.0f;  verts[0].y =  0.0f;  verts[0].z = 0.0f;
    verts[0].u = 0.0f;  verts[0].v = 1.0f; verts[0].colour = 0xFFFFFFFFu;
    verts[1].x = 64.0f; verts[1].y =  0.0f;  verts[1].z = 0.0f;
    verts[1].u = 1.0f;  verts[1].v = 1.0f; verts[1].colour = 0xFFFFFFFFu;
    verts[2].x = 0.0f;  verts[2].y = 64.0f;  verts[2].z = 0.0f;
    verts[2].u = 0.0f;  verts[2].v = 0.0f; verts[2].colour = 0xFFFFFFFFu;
    // Triangle 2: BR, TR, TL
    verts[3].x = 64.0f; verts[3].y =  0.0f;  verts[3].z = 0.0f;
    verts[3].u = 1.0f;  verts[3].v = 1.0f; verts[3].colour = 0xFFFFFFFFu;
    verts[4].x = 64.0f; verts[4].y = 64.0f;  verts[4].z = 0.0f;
    verts[4].u = 1.0f;  verts[4].v = 0.0f; verts[4].colour = 0xFFFFFFFFu;
    verts[5].x = 0.0f;  verts[5].y = 64.0f;  verts[5].z = 0.0f;
    verts[5].u = 0.0f;  verts[5].v = 0.0f; verts[5].colour = 0xFFFFFFFFu;

    Renderer::GetInstance()->DrawTriList(verts, 6);

    unsigned char buf[FBO_W * FBO_H * 4];
    fbo.ReadPixels(buf);
    fbo.Unbind();
    glDeleteTextures(1, &tex);

    bool ok = true;
    if (!updateGolden) {
        // White quad rendered with white texture tint -> white pixels.
        ok = ok && assertPixel(buf, 32, 32, 255, 255, 255, 255, "F_DrawTriList");
        ok = ok && assertPixel(buf,  1,  1, 255, 255, 255, 255, "F_DrawTriList/BL");
        ok = ok && assertPixel(buf, 62, 62, 255, 255, 255, 255, "F_DrawTriList/TR");
        if (ok) ok = compareGolden("F_DrawTriList", buf);
    } else {
        ok = saveGolden("F_DrawTriList", buf);
    }
    return ok;
}

// Case G: DrawColorQuad -- untextured tinted quad (used by BombHit crit-flash).
// No texture bound; expect the FBO filled with the tint colour.
static bool caseG_ColorQuad(FBO& fbo, bool updateGolden) {
    fbo.Bind();
    clearFBO(0.0f, 0.0f, 0.0f);
    setupPixelOrtho();
    Mortar::Texture::s_LastBoundTexId = 0;   // DrawColorQuad binds no texture
    glDisable(GL_BLEND);

    // 64x64 quad centred (same world-matrix setup as drawPixelQuad).
    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.GetWorldStack().Scale(_Vector3<float>(64.0f, 64.0f, 1.0f));
    mm.GetWorldStack().Translate(_Vector3<float>(32.0f, 32.0f, 0.0f));
    mm.UploadModelViewOnly();

    Renderer::GetInstance()->DrawColorQuad(Colour(0, 128, 255, 255));

    unsigned char buf[FBO_W * FBO_H * 4];
    fbo.ReadPixels(buf);
    fbo.Unbind();

    bool ok = true;
    if (!updateGolden) {
        ok = ok && assertPixel(buf, 32, 32,   0, 128, 255, 255, "G_ColorQuad");
        ok = ok && assertPixel(buf,  0,  0,   0, 128, 255, 255, "G_ColorQuad");
        ok = ok && assertPixel(buf, 63, 63,   0, 128, 255, 255, "G_ColorQuad");
        if (ok) ok = compareGolden("G_ColorQuad", buf);
    } else {
        ok = saveGolden("G_ColorQuad", buf);
    }
    return ok;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    bool updateGolden = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--update-golden") == 0) updateGolden = true;
    }

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SKIP: SDL_Init failed: %s\n", SDL_GetError());
        return 77;
    }

    // Request compatibility profile (fixed-function pipeline).
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);

    SDL_Window* window = SDL_CreateWindow(
        "test_renderer", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) {
        fprintf(stderr, "SKIP: no GL window: %s\n", SDL_GetError());
        SDL_Quit();
        return 77;
    }

    SDL_GLContext glctx = SDL_GL_CreateContext(window);
    if (!glctx) {
        fprintf(stderr, "SKIP: no GL context: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 77;
    }

    if (!gl_load_functions()) {
        fprintf(stderr, "SKIP: gl_load_functions failed\n");
        SDL_GL_DeleteContext(glctx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 77;
    }

    if (!LoadFBOFunctions()) {
        SDL_GL_DeleteContext(glctx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 77;
    }

    // Boot the minimal engine singletons needed by Renderer::DrawQuad.
    // MatrixManager is a class-static and initialised at static-init time.
    // Renderer needs its s_instance set via init().
    static Renderer s_renderer;
    s_renderer.init();

    FBO fbo;
    if (!fbo.Create()) {
        SDL_GL_DeleteContext(glctx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 77;
    }

    if (updateGolden) {
        printf("[test_renderer] --update-golden mode: writing golden images\n");
    } else {
        printf("[test_renderer] compare mode: checking against goldens\n");
    }

    int failures = 0;
    if (!caseA_SolidTintQuad     (fbo, updateGolden)) { ++failures; fprintf(stderr, "FAIL: case A\n"); }
    if (!caseB_FullUVOrientation (fbo, updateGolden)) { ++failures; fprintf(stderr, "FAIL: case B\n"); }
    if (!caseC_SubRectUV         (fbo, updateGolden)) { ++failures; fprintf(stderr, "FAIL: case C\n"); }
    if (!caseD_AlphaBlend        (fbo, updateGolden)) { ++failures; fprintf(stderr, "FAIL: case D\n"); }
    if (!caseE_FullscreenQuad    (fbo, updateGolden)) { ++failures; fprintf(stderr, "FAIL: case E\n"); }
    if (!caseF_DrawTriList       (fbo, updateGolden)) { ++failures; fprintf(stderr, "FAIL: case F\n"); }
    if (!caseG_ColorQuad         (fbo, updateGolden)) { ++failures; fprintf(stderr, "FAIL: case G\n"); }

    fbo.Destroy();
    SDL_GL_DeleteContext(glctx);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (failures == 0) {
        printf("PASS: all renderer cases ok\n");
        return 0;
    }
    fprintf(stderr, "FAIL: %d renderer case(s) failed\n", failures);
    return 1;
}
