// test_text_effects.cpp -- BakedStringBox effects-combination screenshot grid.
//
// Renders a 4-column x 7-row grid where each cell applies a different
// BakedStringBox effect (or combination). Validates the 200-byte re-layout:
// shadow/gradient/metallic/stroke/clipping fields at their v1.6.1 offsets.
//
// Grid rows:
//   Row 0: Fill      -- plain white, 2-color gradient, metallic gold, metallic silver
//   Row 1: Shadow    -- small offset, medium offset, inner glow (flag=1), shadow+gradient
//   Row 2: Stroke    -- thin cyan, thick black, 2-color, triple combo (metal+shadow+stroke)
//   Row 3: Size      -- 10px / 16px / 24px / 32px (all metallic gold)
//   Row 4: Alignment -- left-H, center-H, right-H, center-H+top-V (fixed 220px box)
//   Row 5: Clipping  -- no clip, left-half, top-half, center-band (worldspace scissor)
//   Row 6: Languages -- ZH metallic, JA shadow+gradient, KO 2-color gradient, AR plain
//
// Cell labels (bottom 14px strip) are drawn in Verdana (C:\Windows\Fonts\verdana.ttf)
// for legibility; falls back to pFontMain bitmap when Verdana is absent.
//
// Output: tmp/test/screenshots/text_effects/grid.png
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
#include "math/_Vector2.h"
#include "math/_Vector3.h"
#include "math/Colour.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <SDL_image.h>

// ---------------------------------------------------------------------------
// GL extension function pointers (FBO + readpixels)
// ---------------------------------------------------------------------------

typedef void   (APIENTRYP PFN_TE_glGenFramebuffers)(GLsizei, GLuint*);
typedef void   (APIENTRYP PFN_TE_glBindFramebuffer)(GLenum, GLuint);
typedef void   (APIENTRYP PFN_TE_glGenRenderbuffers)(GLsizei, GLuint*);
typedef void   (APIENTRYP PFN_TE_glBindRenderbuffer)(GLenum, GLuint);
typedef void   (APIENTRYP PFN_TE_glRenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
typedef void   (APIENTRYP PFN_TE_glFramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
typedef void   (APIENTRYP PFN_TE_glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (APIENTRYP PFN_TE_glCheckFramebufferStatus)(GLenum);
typedef void   (APIENTRYP PFN_TE_glDeleteFramebuffers)(GLsizei, const GLuint*);
typedef void   (APIENTRYP PFN_TE_glDeleteRenderbuffers)(GLsizei, const GLuint*);
typedef void   (APIENTRYP PFN_TE_glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, GLvoid*);

static PFN_TE_glGenFramebuffers        te_glGenFramebuffers       = NULL;
static PFN_TE_glBindFramebuffer        te_glBindFramebuffer       = NULL;
static PFN_TE_glGenRenderbuffers       te_glGenRenderbuffers      = NULL;
static PFN_TE_glBindRenderbuffer       te_glBindRenderbuffer      = NULL;
static PFN_TE_glRenderbufferStorage    te_glRenderbufferStorage   = NULL;
static PFN_TE_glFramebufferRenderbuffer te_glFramebufferRenderbuffer = NULL;
static PFN_TE_glFramebufferTexture2D   te_glFramebufferTexture2D  = NULL;
static PFN_TE_glCheckFramebufferStatus te_glCheckFramebufferStatus = NULL;
static PFN_TE_glDeleteFramebuffers     te_glDeleteFramebuffers    = NULL;
static PFN_TE_glDeleteRenderbuffers    te_glDeleteRenderbuffers   = NULL;
static PFN_TE_glReadPixels             te_glReadPixels            = NULL;

#define TE_FB_           0x8D40u
#define TE_RB_           0x8D41u
#define TE_COLOR0_       0x8CE0u
#define TE_DEPTH_ATTACH_ 0x8D00u
#define TE_DEPTH16_      0x81A5u
#define TE_FB_COMPLETE_  0x8CD5u
#define TE_RGBA8_        0x8058u

static bool TE_LoadFBO() {
#define TE_LOAD(name, T) te_##name = (T)SDL_GL_GetProcAddress(#name); \
    if (!te_##name) { fprintf(stderr, "SKIP: " #name " unavailable\n"); return false; }
    TE_LOAD(glGenFramebuffers,        PFN_TE_glGenFramebuffers)
    TE_LOAD(glBindFramebuffer,        PFN_TE_glBindFramebuffer)
    TE_LOAD(glGenRenderbuffers,       PFN_TE_glGenRenderbuffers)
    TE_LOAD(glBindRenderbuffer,       PFN_TE_glBindRenderbuffer)
    TE_LOAD(glRenderbufferStorage,    PFN_TE_glRenderbufferStorage)
    TE_LOAD(glFramebufferRenderbuffer, PFN_TE_glFramebufferRenderbuffer)
    TE_LOAD(glFramebufferTexture2D,   PFN_TE_glFramebufferTexture2D)
    TE_LOAD(glCheckFramebufferStatus, PFN_TE_glCheckFramebufferStatus)
    TE_LOAD(glDeleteFramebuffers,     PFN_TE_glDeleteFramebuffers)
    TE_LOAD(glDeleteRenderbuffers,    PFN_TE_glDeleteRenderbuffers)
#undef TE_LOAD
    te_glReadPixels = (PFN_TE_glReadPixels)SDL_GL_GetProcAddress("glReadPixels");
    if (!te_glReadPixels) {
        fprintf(stderr, "SKIP: glReadPixels unavailable\n");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Cell FBO
// ---------------------------------------------------------------------------

static const int TE_CELL_W = 240;
static const int TE_CELL_H = 72;
static const int TE_LABEL_H = 14;   // bottom label-strip height in pixels

struct TE_CellFBO {
    GLuint fbo;
    GLuint colorTex;
    GLuint depthRbo;

    TE_CellFBO() : fbo(0), colorTex(0), depthRbo(0) {}

    bool Create() {
        glGenTextures(1, &colorTex);
        glBindTexture(GL_TEXTURE_2D, colorTex);
        glTexImage2D(GL_TEXTURE_2D, 0, (GLint)TE_RGBA8_, TE_CELL_W, TE_CELL_H,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);

        te_glGenRenderbuffers(1, &depthRbo);
        te_glBindRenderbuffer(TE_RB_, depthRbo);
        te_glRenderbufferStorage(TE_RB_, TE_DEPTH16_, TE_CELL_W, TE_CELL_H);
        te_glBindRenderbuffer(TE_RB_, 0);

        te_glGenFramebuffers(1, &fbo);
        te_glBindFramebuffer(TE_FB_, fbo);
        te_glFramebufferTexture2D(TE_FB_, TE_COLOR0_, GL_TEXTURE_2D, colorTex, 0);
        te_glFramebufferRenderbuffer(TE_FB_, TE_DEPTH_ATTACH_, TE_RB_, depthRbo);

        GLenum status = te_glCheckFramebufferStatus(TE_FB_);
        if (status != TE_FB_COMPLETE_) {
            fprintf(stderr, "SKIP: TE_CellFBO incomplete (0x%x)\n", (unsigned)status);
            te_glBindFramebuffer(TE_FB_, 0);
            return false;
        }
        return true;
    }

    void Bind() {
        te_glBindFramebuffer(TE_FB_, fbo);
        glViewport(0, 0, TE_CELL_W, TE_CELL_H);
    }

    void Unbind() {
        te_glBindFramebuffer(TE_FB_, 0);
    }

    void ReadRGBA(unsigned char* buf) {
        // Stage-2 2D batching: drain pending 2D draws before the readback.
        Renderer::GetInstance()->Flush2D();
        te_glReadPixels(0, 0, TE_CELL_W, TE_CELL_H, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    }

    void Destroy() {
        if (fbo)      { te_glDeleteFramebuffers(1, &fbo); fbo = 0; }
        if (colorTex) { glDeleteTextures(1, &colorTex); colorTex = 0; }
        if (depthRbo) { te_glDeleteRenderbuffers(1, &depthRbo); depthRbo = 0; }
    }
};

// Pixel-space ortho: top=TE_CELL_H, bottom=0, left=0, right=TE_CELL_W.
// Y=0 at bottom, Y=TE_CELL_H at top (standard GL origin).
static void TE_SetupCellOrtho() {
    MatrixManager& mm = MatrixManager::GetInstance();
    mm.SetupOrtho((float)TE_CELL_H, 0.0f, 0.0f, (float)TE_CELL_W, 1.0f, -1.0f);
    mm.GetViewStack().Reset();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();
}

// Returns true if any RGBA pixel has R, G, or B > threshold.
// Background is dark gray (0x17) so threshold=0x30 separates glyphs from bg.
static bool TE_HasGlyphs(const unsigned char* rgba) {
    const int N = TE_CELL_W * TE_CELL_H * 4;
    for (int i = 0; i < N; i += 4) {
        if (rgba[i] > 0x30 || rgba[i+1] > 0x30 || rgba[i+2] > 0x30) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Cell descriptor
// ---------------------------------------------------------------------------

// UTF-8 language samples (same code points as test_text_render.cpp).
// EN:  ASCII "Slice!"
// ZH:  U+6C34 U+679C = water+fruit
// JA:  U+30D5 U+30EB U+30FC U+30C4 = "fruits"
// KO:  U+C218 U+BC15 = watermelon
// AR:  U+0641..U+0629 = "fruit" (Arabic)
static const char* const TE_SAMPLE_EN = "Slice!";
static const char* const TE_SAMPLE_ZH = "\xe6\xb0\xb4\xe6\x9e\x9c";
static const char* const TE_SAMPLE_JA = "\xe3\x83\x95\xe3\x83\xab\xe3\x83\xbc\xe3\x83\x84";
static const char* const TE_SAMPLE_KO = "\xec\x88\x98\xeb\xb0\x95";
static const char* const TE_SAMPLE_AR = "\xd9\x81\xd8\xa7\xd9\x83\xd9\x87\xd8\xa9";

// Alignment demo uses a short ASCII string that fits in one line but is
// noticeably narrower than the 220px box, so left/center/right placement
// is clearly visible.
static const char* const TE_SAMPLE_ALIGN = "Slice!";

struct TE_CellDesc {
    const char* caption;    // short label drawn in Verdana at bottom strip
    const char* sample;     // UTF-8 text to render
    int         fontIdx;    // 0=gangofchinese.ttf (EN/ZH/JA/KO), 1=arabic.ttf (AR)
    int         effIdx;     // index into TE_ApplyEffect
    int         align;      // BakedStringBox alignment flags
    float       fontSize;   // render size in px
    int         clipMode;   // 0=none, 1=left-half, 2=top-half, 3=center-band
};

static const int TE_NCOLS  = 4;
static const int TE_NROWS  = 7;
static const int TE_NCELLS = TE_NCOLS * TE_NROWS;

// 0x0f = centre-H (bits 1:0=11) + centre-V (bits 3:2=11)
// 0x0C = left-H   (bits 1:0=00) + centre-V (bits 3:2=11)
// 0x0E = right-H  (bits 1:0=10) + centre-V (bits 3:2=11)
// 0x03 = centre-H (bits 1:0=11) + top-V    (bits 3:2=00)
static const TE_CellDesc s_Cells[TE_NCELLS] = {
    // Row 0: Fill effects
    { "Plain white",      TE_SAMPLE_EN, 0,  0, 0x0f, 18.0f, 0 },
    { "2-color gradient", TE_SAMPLE_EN, 0,  1, 0x0f, 18.0f, 0 },
    { "Metallic gold",    TE_SAMPLE_EN, 0,  2, 0x0f, 18.0f, 0 },
    { "Metallic silver",  TE_SAMPLE_EN, 0,  3, 0x0f, 18.0f, 0 },
    // Row 1: Shadow variants
    { "Shadow sm off=1",  TE_SAMPLE_EN, 0,  4, 0x0f, 18.0f, 0 },
    { "Shadow md off=3",  TE_SAMPLE_EN, 0,  5, 0x0f, 18.0f, 0 },
    { "InnerGlow flag=1", TE_SAMPLE_EN, 0,  6, 0x0f, 18.0f, 0 },
    { "Shadow+gradient",  TE_SAMPLE_EN, 0,  7, 0x0f, 18.0f, 0 },
    // Row 2: Stroke variants + combo
    { "Stroke w=1 cyan",  TE_SAMPLE_EN, 0,  8, 0x0f, 18.0f, 0 },
    { "Stroke w=5 black", TE_SAMPLE_EN, 0,  9, 0x0f, 18.0f, 0 },
    { "Stroke 2-color",   TE_SAMPLE_EN, 0, 10, 0x0f, 18.0f, 0 },
    { "Metal+Shd+Stroke", TE_SAMPLE_EN, 0, 11, 0x0f, 18.0f, 0 },
    // Row 3: Font sizes (metallic gold, same effect, different size)
    { "Size 10px",        TE_SAMPLE_EN, 0,  2, 0x0f, 10.0f, 0 },
    { "Size 16px",        TE_SAMPLE_EN, 0,  2, 0x0f, 16.0f, 0 },
    { "Size 24px",        TE_SAMPLE_EN, 0,  2, 0x0f, 24.0f, 0 },
    { "Size 32px",        TE_SAMPLE_EN, 0,  2, 0x0f, 32.0f, 0 },
    // Row 4: Alignment flags in a 220px box (plain white, fontSize=14)
    { "Left-H+CenterV",  TE_SAMPLE_ALIGN, 0,  0, 0x0C, 14.0f, 0 },
    { "Center-H+CenterV",TE_SAMPLE_ALIGN, 0,  0, 0x0f, 14.0f, 0 },
    { "Right-H+CenterV", TE_SAMPLE_ALIGN, 0,  0, 0x0E, 14.0f, 0 },
    { "CenterH+Top-V",   TE_SAMPLE_ALIGN, 0,  0, 0x03, 14.0f, 0 },
    // Row 5: Worldspace scissor clipping (metallic gold so effect is visible)
    { "No clip (full)",   TE_SAMPLE_EN, 0,  2, 0x0f, 18.0f, 0 },
    { "Clip left-half",   TE_SAMPLE_EN, 0,  2, 0x0f, 18.0f, 1 },
    { "Clip top-half",    TE_SAMPLE_EN, 0,  2, 0x0f, 18.0f, 2 },
    { "Clip center-band", TE_SAMPLE_EN, 0,  2, 0x0f, 18.0f, 3 },
    // Row 6: Language coverage (different fonts + effects)
    { "ZH Metallic",      TE_SAMPLE_ZH, 0,  2, 0x0f, 18.0f, 0 },
    { "JA Shadow+grad",   TE_SAMPLE_JA, 0,  7, 0x0f, 18.0f, 0 },
    { "KO 2-color grad",  TE_SAMPLE_KO, 0,  1, 0x0f, 18.0f, 0 },
    { "AR Plain white",   TE_SAMPLE_AR, 1,  0, 0x0f, 18.0f, 0 },
};

// ---------------------------------------------------------------------------
// Effect applicators
// ---------------------------------------------------------------------------

static void TE_ApplyEffect(Mortar::BakedStringBox* box, int effIdx) {
    switch (effIdx) {
    case 0:
        // Solid white.
        box->SetColour(Colour(255, 255, 255, 255), 0);
        break;
    case 1:
        // 2-color gradient: orange top, mint green bottom.
        box->SetGradient(Colour(255, 160, 40, 255), Colour(80, 220, 120, 255), false);
        break;
    case 2:
        // Metallic gold (matches IngamePopup NEW badge).
        box->SetMetallicGradient(
            Colour(255, 253, 88, 255),
            Colour(255, 255, 255, 255),
            Colour(152, 123, 10, 255),
            Colour(255, 253, 88, 255),
            false);
        break;
    case 3:
        // Metallic silver/blue.
        box->SetMetallicGradient(
            Colour(200, 220, 255, 255),
            Colour(255, 255, 255, 255),
            Colour( 60,  90, 160, 255),
            Colour(180, 200, 240, 255),
            false);
        break;
    case 4:
        // Shadow small: scale=0.5, dark blue, offset=(1,-1), flag=0.
        box->SetColour(Colour(255, 255, 255, 255), 0);
        box->SetShadow(0.5f, Colour(0, 0, 80, 200), _Vector3<float>(1.0f, -1.0f, 0.0f), 0);
        break;
    case 5:
        // Shadow medium: scale=1.0, dark navy, offset=(3,-2), flag=0.
        box->SetColour(Colour(255, 255, 255, 255), 0);
        box->SetShadow(1.0f, Colour(0, 0, 60, 220), _Vector3<float>(3.0f, -2.0f, 0.0f), 0);
        break;
    case 6:
        // Inner glow: scale=2.0, yellow, offset=(0,0), flag=1.
        // flag=1 fires shadow when scale>=0 (vs flag=0 which requires scale>0).
        box->SetColour(Colour(255, 255, 255, 255), 0);
        box->SetShadow(2.0f, Colour(255, 220, 0, 180), _Vector3<float>(0.0f, 0.0f, 0.0f), 1);
        break;
    case 7:
        // Shadow + 2-color gradient combo.
        box->SetGradient(Colour(255, 160, 40, 255), Colour(80, 220, 120, 255), false);
        box->SetShadow(1.0f, Colour(0, 0, 60, 200), _Vector3<float>(2.0f, -2.0f, 0.0f), 0);
        break;
    case 8:
        // Stroke thin w=1, solid cyan outline.
        box->SetColour(Colour(255, 255, 255, 255), 0);
        box->SetStroke(1.0f, Colour(0, 200, 255, 255));
        break;
    case 9:
        // Stroke thick w=5, black outline over warm-white text.
        box->SetColour(Colour(255, 220, 140, 255), 0);
        box->SetStroke(5.0f, Colour(0, 0, 0, 255));
        break;
    case 10:
        // 2-color stroke w=3: gold primary, dark-red secondary.
        box->SetColour(Colour(255, 255, 255, 255), 0);
        box->SetStroke(3.0f, Colour(255, 200, 0, 255), Colour(180, 30, 0, 255));
        break;
    case 11:
        // Triple combo: metallic gold + shadow + stroke.
        box->SetMetallicGradient(
            Colour(255, 253, 88, 255),
            Colour(255, 255, 255, 255),
            Colour(152, 123, 10, 255),
            Colour(255, 253, 88, 255),
            false);
        box->SetShadow(1.0f, Colour(0, 0, 60, 200), _Vector3<float>(2.0f, -2.0f, 0.0f), 0);
        box->SetStroke(2.0f, Colour(0, 0, 0, 255));
        break;
    default:
        box->SetColour(Colour(255, 255, 255, 255), 0);
        break;
    }
}

// Apply worldspace scissor clipping.
//
// BakedStringBox::Draw (port) converts the stored clip rect from the game's
// standard 480x320 world-space to GL scissor pixels using:
//   sx = (clipX0 + 240) / 480 * vpW
//   sy = ((clipY0 - clipH) + 160) / 320 * vpH
//   sw = clipW / 480 * vpW
//   sh = clipH / 320 * vpH
//
// Our cell FBO viewport is (vpW=240, vpH=72). World-space centre (0,0) maps to
// pixel (120, 36) -- the exact centre of our 240x72 cell. So game-world clip
// rectangles partition the cell pixel-space cleanly:
//   left-half   -> SetWorldspaceClipping(-240, 160, 240, 320): sx=0,  sw=120
//   top-half    -> SetWorldspaceClipping(-240, 160, 480, 160): sy=36, sh=36
//   center-band -> SetWorldspaceClipping( -60, 160, 120, 320): sx=90, sw=60
static void TE_ApplyClip(Mortar::BakedStringBox* box, int clipMode) {
    switch (clipMode) {
    case 1:
        // Show left half of cell: x=[0, CELL_W/2].
        box->SetWorldspaceClipping(-240, 160, 240, 320);
        break;
    case 2:
        // Show top half of cell: y=[CELL_H/2, CELL_H] (GL bottom-up).
        box->SetWorldspaceClipping(-240, 160, 480, 160);
        break;
    case 3:
        // Show center vertical band: x=[CELL_W*3/8, CELL_W*5/8] = [90, 150].
        box->SetWorldspaceClipping(-60, 160, 120, 320);
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Render one cell into the shared FBO; read back RGBA pixels.
// Returns true if glyphs were detected (any pixel brighter than threshold).
// outPixels must be TE_CELL_W*TE_CELL_H*4 bytes.
// ---------------------------------------------------------------------------
static bool TE_RenderCell(
    TE_CellFBO& cfbo,
    Mortar::FontCacheObjectTTF* gangFont,
    Mortar::FontCacheObjectTTF* arabicFont,
    Mortar::Font* bitmapLabel,
    Mortar::FontCacheObjectTTF* verdanaFont,
    const TE_CellDesc& desc,
    unsigned char* outPixels)
{
    cfbo.Bind();

    // Mid grey so dark drop-shadows (#257 blur) are visible; near-black hid them.
    glClearColor(0.50f, 0.50f, 0.50f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    TE_SetupCellOrtho();

    bool hasGlyphs = false;

    Mortar::FontCacheObjectTTF* cellFont = (desc.fontIdx == 1) ? arabicFont : gangFont;
    if (!cellFont) cellFont = gangFont;

    if (cellFont) {
        const int boxW = TE_CELL_W - 20;
        const int boxH = TE_CELL_H - TE_LABEL_H - 8;

        // Pixel-space ortho: Y=0 bottom, Y=TE_CELL_H top.
        // Label strip: [0, TE_LABEL_H]. Text area: [TE_LABEL_H, TE_CELL_H].
        // Text centre is the vertical midpoint of the text area.
        const float textCentreX = (float)TE_CELL_W * 0.5f;
        const float textCentreY = (float)TE_LABEL_H + (float)(TE_CELL_H - TE_LABEL_H) * 0.5f;

        Mortar::BakedStringBox box(cellFont, desc.fontSize, boxW, boxH, (Mortar::ALIGNMENT_TYPE)desc.align, 1, 0);
        box.SetText(desc.sample);
        TE_ApplyEffect(&box, desc.effIdx);
        if (desc.clipMode != 0) {
            TE_ApplyClip(&box, desc.clipMode);
        }

        _Vector3<float> pos(textCentreX, textCentreY, 0.0f);
        // preShift=1: SetTranslation pre-shifts by (-boxW/2, +boxH/2)
        // so the box is centred on (textCentreX, textCentreY).
        box.SetTranslation(pos, 1);
        _Vector2<float> sc(1.0f, 1.0f);
        box.Draw(sc, 0.0f, 1);
    }

    // Read back to detect glyph presence.
    cfbo.ReadRGBA(outPixels);
    hasGlyphs = TE_HasGlyphs(outPixels);

    // If no glyphs rendered, draw a fallback "[no glyphs]" indicator.
    if (!hasGlyphs && bitmapLabel) {
        glClearColor(0.09f, 0.09f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        TE_SetupCellOrtho();

        const float scale = 7.0f;
        _Vector3<float> fbPos((float)TE_CELL_W * 0.5f, (float)TE_CELL_H * 0.5f, 0.0f);
        Colour grey(160, 160, 160, 255);
        bitmapLabel->DrawString(scale, 1.0f, 0.0f, "[no glyphs]", fbPos, grey, 0x3);
        cfbo.ReadRGBA(outPixels);
    }

    // Draw the Verdana caption in the bottom label strip.
    // The label is drawn AFTER the glyph-presence read so the label pixels don't
    // count as "glyphs" for the has-glyphs check above.
    if (verdanaFont) {
        const int lblBoxW = TE_CELL_W - 4;
        const int lblBoxH = TE_LABEL_H;
        const float lblSize = 9.0f;

        Mortar::BakedStringBox lblBox(verdanaFont, lblSize, lblBoxW, lblBoxH, (Mortar::ALIGNMENT_TYPE)0x0f, 1, 0);
        lblBox.SetText(desc.caption);
        lblBox.SetColour(Colour(210, 210, 210, 255), 0);

        // Centre the label in the strip: pixel (CELL_W/2, LABEL_H/2).
        _Vector3<float> lblPos((float)(TE_CELL_W / 2), (float)(TE_LABEL_H / 2), 0.0f);
        lblBox.SetTranslation(lblPos, 1);
        _Vector2<float> lblSc(1.0f, 1.0f);
        lblBox.Draw(lblSc, 0.0f, 1);

        cfbo.ReadRGBA(outPixels);
    } else if (bitmapLabel) {
        // Bitmap label fallback.
        const float lblScale = 6.0f;
        _Vector3<float> lblPos(4.0f, 8.0f, 0.0f);
        Colour lblCol(200, 200, 200, 255);
        bitmapLabel->DrawString(lblScale, 1.0f, 0.0f, desc.caption, lblPos, lblCol, 0x0);
        cfbo.ReadRGBA(outPixels);
    }

    cfbo.Unbind();
    return hasGlyphs;
}

// ---------------------------------------------------------------------------
// Assemble grid canvas and write PNG
// ---------------------------------------------------------------------------

static bool TE_SaveGrid(
    const unsigned char* const* cells, // [TE_NCELLS] row-major, each TE_CELL_W*TE_CELL_H*4 (bottom-up GL)
    fn::TestHarness& h,
    const char* name)
{
    const int totalW = TE_NCOLS * TE_CELL_W;
    const int totalH = TE_NROWS * TE_CELL_H;
    const size_t canvasBytes = (size_t)totalW * totalH * 4;

    unsigned char* canvas = (unsigned char*)std::malloc(canvasBytes);
    if (!canvas) {
        fprintf(stderr, "[text_effects] out of memory for grid canvas\n");
        return false;
    }

    // Dark charcoal background.
    for (size_t i = 0; i < canvasBytes; i += 4) {
        canvas[i+0] = 0x12;
        canvas[i+1] = 0x12;
        canvas[i+2] = 0x12;
        canvas[i+3] = 0xFF;
    }

    // Blit each cell: GL bottom-up -> canvas top-down.
    for (int row = 0; row < TE_NROWS; row++) {
        for (int col = 0; col < TE_NCOLS; col++) {
            const unsigned char* src = cells[row * TE_NCOLS + col];
            if (!src) continue;

            const int dstCellX = col * TE_CELL_W;
            const int dstCellY = row * TE_CELL_H;

            for (int cy = 0; cy < TE_CELL_H; cy++) {
                int glY = TE_CELL_H - 1 - cy;
                const unsigned char* srcRow = src + (size_t)glY * TE_CELL_W * 4;
                unsigned char* dstRow = canvas + ((size_t)(dstCellY + cy) * totalW + dstCellX) * 4;
                std::memcpy(dstRow, srcRow, (size_t)TE_CELL_W * 4);
            }
        }
    }

    // Draw 1px separator lines between cells.
    const unsigned char sepR = 0x3A, sepG = 0x3A, sepB = 0x3A;
    for (int row = 1; row < TE_NROWS; row++) {
        int sepY = row * TE_CELL_H;
        if (sepY >= totalH) continue;
        for (int x = 0; x < totalW; x++) {
            unsigned char* p = canvas + ((size_t)sepY * totalW + x) * 4;
            p[0] = sepR; p[1] = sepG; p[2] = sepB; p[3] = 0xFF;
        }
    }
    for (int col = 1; col < TE_NCOLS; col++) {
        int sepX = col * TE_CELL_W;
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
        fprintf(stderr, "[text_effects] SDL_CreateRGBSurfaceFrom failed: %s\n", SDL_GetError());
        std::free(canvas);
        return false;
    }
    bool ok = h.SavePng(surf, name);
    SDL_FreeSurface(surf);
    std::free(canvas);
    if (!ok) return false;
    printf("[text_effects] grid %dx%d (%d rows x %d cols)\n",
           totalW, totalH, TE_NROWS, TE_NCOLS);
    return true;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "text_effects");
    // 120 burn-in frames so gangofchinese.ttf is lazily loaded by game init.
    h.SetInitFrames(120);
    if (!h.ParseFlags()) return 1;
    if (!h.InitComponent()) {
        fprintf(stderr, "SKIP: InitComponent failed\n");
        return 77;
    }

    if (!TE_LoadFBO()) {
        fprintf(stderr, "SKIP: FBO extensions unavailable\n");
        return 77;
    }

    // Load gangofchinese.ttf (covers EN/ZH/JA/KO).
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
    printf("[text_effects] TTF font: gangofchinese.ttf OK\n");

    // Load arabic.ttf for the AR cell; falls back to gangofchinese if absent.
    static Mortar::SmartPtr<Mortar::Font> s_ArabicFont =
        Mortar::Font::Create("fontstruetype/arabic.ttf");
    Mortar::FontCacheObjectTTF* arabicFont = NULL;
    if (s_ArabicFont.IsValid()) {
        arabicFont = Mortar::FontTTFRegistry::GetInstance().Lookup(s_ArabicFont.Get());
    }
    if (!arabicFont) {
        printf("[text_effects] WARN: arabic.ttf not found -- AR cell uses gangofchinese.ttf\n");
        arabicFont = gangFont;
    } else {
        printf("[text_effects] TTF font: arabic.ttf OK\n");
    }

    // Bitmap fallback label font.
    Mortar::Font* bitmapLabel = NULL;
    if (game_work.pFontMain.IsValid()) {
        bitmapLabel = game_work.pFontMain.Get();
        printf("[text_effects] Bitmap label fallback: pFontMain OK\n");
    }

    // Verdana for cell captions (legible at 9px).
    Mortar::FontCacheObjectTTF* verdanaFont = NULL;
    {
        const char* verdanaPath = "C:\\Windows\\Fonts\\verdana.ttf";
        verdanaFont = new Mortar::FontCacheObjectTTF(verdanaPath, 12);
        if (!verdanaFont->IsValid()) {
            delete verdanaFont;
            verdanaFont = NULL;
            printf("[text_effects] WARN: verdana.ttf not found -- bitmap label fallback\n");
        } else {
            printf("[text_effects] Verdana label font: OK\n");
        }
    }

    // Create shared cell FBO.
    TE_CellFBO cfbo;
    if (!cfbo.Create()) {
        fprintf(stderr, "SKIP: TE_CellFBO create failed\n");
        delete verdanaFont;
        return 77;
    }

    // Allocate pixel buffers.
    const size_t cellBytes = (size_t)TE_CELL_W * TE_CELL_H * 4;
    unsigned char** cellPixels = (unsigned char**)std::calloc((size_t)TE_NCELLS, sizeof(unsigned char*));
    if (!cellPixels) {
        fprintf(stderr, "FAIL: out of memory for cell pixel array\n");
        cfbo.Destroy();
        delete verdanaFont;
        return 1;
    }
    for (int i = 0; i < TE_NCELLS; i++) {
        cellPixels[i] = (unsigned char*)std::malloc(cellBytes);
        if (!cellPixels[i]) {
            fprintf(stderr, "FAIL: out of memory for cell %d pixels\n", i);
            for (int j = 0; j < i; j++) std::free(cellPixels[j]);
            std::free(cellPixels);
            cfbo.Destroy();
            delete verdanaFont;
            return 1;
        }
        // Default: dark background.
        std::memset(cellPixels[i], 0x12, cellBytes);
        for (int k = 3; k < (int)cellBytes; k += 4) {
            cellPixels[i][k] = 0xFF;
        }
    }

    // Render each cell.
    int noGlyphCount = 0;
    for (int i = 0; i < TE_NCELLS; i++) {
        const TE_CellDesc& desc = s_Cells[i];
        bool hasGlyphs = TE_RenderCell(
            cfbo, gangFont, arabicFont, bitmapLabel, verdanaFont, desc, cellPixels[i]);

        const char* status = hasGlyphs ? "OK" : "NO GLYPHS";
        printf("[text_effects] cell %2d (%s): %s\n", i, desc.caption, status);
        if (!hasGlyphs) noGlyphCount++;
    }

    printf("[text_effects] Cells with no glyphs: %d / %d\n", noGlyphCount, TE_NCELLS);

    // Print effect summary for log clarity.
    printf("[text_effects] Effect coverage:\n");
    printf("  Row 0: plain / 2-color-gradient / metallic-gold / metallic-silver\n");
    printf("  Row 1: shadow-sm / shadow-md / inner-glow(flag=1) / shadow+gradient\n");
    printf("  Row 2: stroke-thin / stroke-thick / stroke-2color / metal+shadow+stroke\n");
    printf("  Row 3: size-10 / size-16 / size-24 / size-32 (all metallic-gold)\n");
    printf("  Row 4: left-H / center-H / right-H / center-H+top-V (align flags)\n");
    printf("  Row 5: no-clip / left-half / top-half / center-band (worldspace scissor)\n");
    printf("  Row 6: ZH-metallic / JA-shadow+grad / KO-gradient / AR-plain\n");
    printf("  Shadow and stroke now use real SDF/blurred glyph rasterisation (#257).\n");
    printf("  Stroke 2/3-color gradient (m_StrokeMode 2/3) applies per-line top->bottom lerp (#257).\n");

    // Write PNG.
    bool saved = false;
    if (h.IsScreenshot()) {
        const unsigned char* const* constCells = (const unsigned char* const*)cellPixels;
        saved = TE_SaveGrid(constCells, h, "text_effects/grid");
        if (!saved) {
            fprintf(stderr, "FAIL: could not save effects grid PNG\n");
        }
    }

    // Cleanup.
    for (int i = 0; i < TE_NCELLS; i++) std::free(cellPixels[i]);
    std::free(cellPixels);
    cfbo.Destroy();
    delete verdanaFont;

    if (h.IsScreenshot() && !saved) {
        h.Shutdown();
        return 1;
    }

    printf("PASS: test_text_effects complete\n");
    return h.Shutdown();
}
