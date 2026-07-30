// test_text_render.cpp -- multi-language x gradient/effect screenshot test for BakedStringBox.
//
// Renders a labeled grid:
//   rows = 6 languages (EN, ZH, JA, KO, FR, AR)
//   cols = 6 effect variants (FLAT, GRADIENT, METALLIC, METALLIC+STROKE,
//                             METALLIC+SHADOW, STROKE_ONLY)
// Only English gets all 6 columns; other languages get cols 0, 2, 4
// (FLAT, METALLIC, METALLIC+SHADOW) -- the empty cols render dark gray.
//
// Output (--screenshot mode):
//   tmp/test/screenshots/text_render/grid.png
//
// Cell labels (bottom strip) are rendered in Verdana (C:\Windows\Fonts\verdana.ttf)
// via a FontCacheObjectTTF + BakedStringBox when available; falls back to pFontMain
// (bitmap .fnt) when Verdana is absent.
//
// Shadow/stroke effects are fully rendered as of this version:
//   shadow = solid-colour copy at (anchor + m_ShadowOffset), drawn before fg.
//   stroke = 8-direction solid-colour outline copies, drawn after shadow, before fg.
//   DIFFERS from binary which blurs the shadow/glow glyph atlas slice.
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

typedef void  (APIENTRYP PFN_TR_glGenFramebuffers)(GLsizei, GLuint*);
typedef void  (APIENTRYP PFN_TR_glBindFramebuffer)(GLenum, GLuint);
typedef void  (APIENTRYP PFN_TR_glGenRenderbuffers)(GLsizei, GLuint*);
typedef void  (APIENTRYP PFN_TR_glBindRenderbuffer)(GLenum, GLuint);
typedef void  (APIENTRYP PFN_TR_glRenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
typedef void  (APIENTRYP PFN_TR_glFramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
typedef void  (APIENTRYP PFN_TR_glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum(APIENTRYP PFN_TR_glCheckFramebufferStatus)(GLenum);
typedef void  (APIENTRYP PFN_TR_glDeleteFramebuffers)(GLsizei, const GLuint*);
typedef void  (APIENTRYP PFN_TR_glDeleteRenderbuffers)(GLsizei, const GLuint*);
typedef void  (APIENTRYP PFN_TR_glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, GLvoid*);

static PFN_TR_glGenFramebuffers       tr_glGenFramebuffers       = NULL;
static PFN_TR_glBindFramebuffer       tr_glBindFramebuffer       = NULL;
static PFN_TR_glGenRenderbuffers      tr_glGenRenderbuffers      = NULL;
static PFN_TR_glBindRenderbuffer      tr_glBindRenderbuffer      = NULL;
static PFN_TR_glRenderbufferStorage   tr_glRenderbufferStorage   = NULL;
static PFN_TR_glFramebufferRenderbuffer tr_glFramebufferRenderbuffer = NULL;
static PFN_TR_glFramebufferTexture2D  tr_glFramebufferTexture2D  = NULL;
static PFN_TR_glCheckFramebufferStatus tr_glCheckFramebufferStatus = NULL;
static PFN_TR_glDeleteFramebuffers    tr_glDeleteFramebuffers    = NULL;
static PFN_TR_glDeleteRenderbuffers   tr_glDeleteRenderbuffers   = NULL;
static PFN_TR_glReadPixels            tr_glReadPixels            = NULL;

#define TR_FB_           0x8D40u
#define TR_RB_           0x8D41u
#define TR_COLOR0_       0x8CE0u
#define TR_DEPTH_ATTACH_ 0x8D00u
#define TR_DEPTH16_      0x81A5u
#define TR_FB_COMPLETE_  0x8CD5u
#define TR_RGBA8_        0x8058u

static bool TR_LoadFBO() {
#define TR_LOAD(name, T) tr_##name = (T)SDL_GL_GetProcAddress(#name); \
    if (!tr_##name) { fprintf(stderr, "SKIP: " #name " unavailable\n"); return false; }
    TR_LOAD(glGenFramebuffers,       PFN_TR_glGenFramebuffers)
    TR_LOAD(glBindFramebuffer,       PFN_TR_glBindFramebuffer)
    TR_LOAD(glGenRenderbuffers,      PFN_TR_glGenRenderbuffers)
    TR_LOAD(glBindRenderbuffer,      PFN_TR_glBindRenderbuffer)
    TR_LOAD(glRenderbufferStorage,   PFN_TR_glRenderbufferStorage)
    TR_LOAD(glFramebufferRenderbuffer, PFN_TR_glFramebufferRenderbuffer)
    TR_LOAD(glFramebufferTexture2D,  PFN_TR_glFramebufferTexture2D)
    TR_LOAD(glCheckFramebufferStatus, PFN_TR_glCheckFramebufferStatus)
    TR_LOAD(glDeleteFramebuffers,    PFN_TR_glDeleteFramebuffers)
    TR_LOAD(glDeleteRenderbuffers,   PFN_TR_glDeleteRenderbuffers)
#undef TR_LOAD
    tr_glReadPixels = (PFN_TR_glReadPixels)SDL_GL_GetProcAddress("glReadPixels");
    if (!tr_glReadPixels) {
        fprintf(stderr, "SKIP: glReadPixels unavailable\n");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Cell FBO
// ---------------------------------------------------------------------------

static const int CELL_W = 240;
static const int CELL_H = 72;
static const int LABEL_H = 14;  // bottom label strip height in pixels

struct CellFBO {
    GLuint fbo;
    GLuint colorTex;
    GLuint depthRbo;

    CellFBO() : fbo(0), colorTex(0), depthRbo(0) {}

    bool Create() {
        glGenTextures(1, &colorTex);
        glBindTexture(GL_TEXTURE_2D, colorTex);
        glTexImage2D(GL_TEXTURE_2D, 0, (GLint)TR_RGBA8_, CELL_W, CELL_H,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);

        tr_glGenRenderbuffers(1, &depthRbo);
        tr_glBindRenderbuffer(TR_RB_, depthRbo);
        tr_glRenderbufferStorage(TR_RB_, TR_DEPTH16_, CELL_W, CELL_H);
        tr_glBindRenderbuffer(TR_RB_, 0);

        tr_glGenFramebuffers(1, &fbo);
        tr_glBindFramebuffer(TR_FB_, fbo);
        tr_glFramebufferTexture2D(TR_FB_, TR_COLOR0_, GL_TEXTURE_2D, colorTex, 0);
        tr_glFramebufferRenderbuffer(TR_FB_, TR_DEPTH_ATTACH_, TR_RB_, depthRbo);

        GLenum status = tr_glCheckFramebufferStatus(TR_FB_);
        if (status != TR_FB_COMPLETE_) {
            fprintf(stderr, "SKIP: CellFBO incomplete (0x%x)\n", (unsigned)status);
            tr_glBindFramebuffer(TR_FB_, 0);
            return false;
        }
        return true;
    }

    void Bind() {
        tr_glBindFramebuffer(TR_FB_, fbo);
        glViewport(0, 0, CELL_W, CELL_H);
    }

    void Unbind() {
        tr_glBindFramebuffer(TR_FB_, 0);
    }

    void ReadRGBA(unsigned char* buf) {
        // Stage-2 2D batching: drain pending 2D draws before the readback.
        Renderer::GetInstance()->Flush2D();
        tr_glReadPixels(0, 0, CELL_W, CELL_H, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    }

    void Destroy() {
        if (fbo)      { tr_glDeleteFramebuffers(1, &fbo); fbo = 0; }
        if (colorTex) { glDeleteTextures(1, &colorTex); colorTex = 0; }
        if (depthRbo) { tr_glDeleteRenderbuffers(1, &depthRbo); depthRbo = 0; }
    }
};

// Pixel-space ortho: top=CELL_H, bottom=0, left=0, right=CELL_W.
// Y=0 at bottom, Y=CELL_H at top (standard GL origin).
static void TR_SetupCellOrtho() {
    MatrixManager& mm = MatrixManager::GetInstance();
    mm.SetupOrtho((float)CELL_H, 0.0f, 0.0f, (float)CELL_W, 1.0f, -1.0f);
    mm.GetViewStack().Reset();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();
}

// ---------------------------------------------------------------------------
// Language definitions
// ---------------------------------------------------------------------------

static const int LANG_COUNT = 6;

static const char* const LANG_CODES[LANG_COUNT] = {
    "EN", "ZH", "JA", "KO", "FR", "AR"
};

static const char* const LANG_NAMES[LANG_COUNT] = {
    "English",  "Chinese", "Japanese", "Korean", "French", "Arabic"
};

// UTF-8 encoded sample strings per language.
// EN:  ASCII
// ZH:  \xe6\xb0\xb4\xe6\x9e\x9c  = U+6C34 U+679C = water+fruit
// JA:  \xe3\x83\x95\xe3\x83\xab\xe3\x83\xbc\xe3\x83\x84 = U+30D5 U+30EB U+30FC U+30C4 = "fruits"
// KO:  \xec\x88\x98\xeb\xb0\x95 = U+C218 U+BC15 = watermelon
// FR:  Cr\xc3\xa8me = "Creme" with accent
// AR:  \xd9\x81\xd8\xa7\xd9\x83\xd9\x87\xd8\xa9 = U+0641 U+0627 U+0643 U+0647 U+0629 = "fruit"
static const char* const LANG_SAMPLES[LANG_COUNT] = {
    "Slice Now!",
    "\xe6\xb0\xb4\xe6\x9e\x9c",
    "\xe3\x83\x95\xe3\x83\xab\xe3\x83\xbc\xe3\x83\x84",
    "\xec\x88\x98\xeb\xb0\x95",
    "Cr\xc3\xa8me",
    "\xd9\x81\xd8\xa7\xd9\x83\xd9\x87\xd8\xa9"
};

// ---------------------------------------------------------------------------
// Effect definitions (6 variants)
// ---------------------------------------------------------------------------

static const int EFF_COUNT = 6;

static const char* const EFF_NAMES[EFF_COUNT] = {
    "FLAT",
    "GRADIENT",
    "METALLIC",
    "METAL+STROKE",
    "METAL+SHADOW",
    "FLAT+STROKE"
};

// Apply effect to a BakedStringBox. Called after SetText and before Draw.
static void TR_ApplyEffect(Mortar::BakedStringBox* box, int effIdx) {
    switch (effIdx) {
    case 0:
        // Flat white colour.
        box->SetColour(Colour(255, 255, 255, 255), 0);
        break;
    case 1:
        // 2-stop top-to-bottom gradient: orange top, white bottom.
        box->SetGradient(Colour(255, 180, 40, 255), Colour(255, 255, 255, 255), false);
        break;
    case 2:
        // Metallic gold gradient (matches IngamePopup NEW badge).
        // SetMetallicGradient(top, bottom, c2, c3, flag)
        box->SetMetallicGradient(
            Colour(255, 253, 88, 255),
            Colour(255, 255, 255, 255),
            Colour(152, 123, 10, 255),
            Colour(255, 253, 88, 255),
            false
        );
        break;
    case 3:
        // Metallic gold + outer glow (black stroke weight 5).
        // API note: SetStroke stores but does not yet render (port TODO).
        box->SetMetallicGradient(
            Colour(255, 253, 88, 255),
            Colour(255, 255, 255, 255),
            Colour(152, 123, 10, 255),
            Colour(255, 253, 88, 255),
            false
        );
        box->SetStroke(5.0f, Colour(0, 0, 0, 255));
        break;
    case 4:
        // Metallic gold + visible offset drop-shadow (dark blue, 2px right/down).
        // Demonstrates the shadow pass -- shadow is drawn before fg in the same glyphs.
        box->SetMetallicGradient(
            Colour(255, 253, 88, 255),
            Colour(255, 255, 255, 255),
            Colour(152, 123, 10, 255),
            Colour(255, 253, 88, 255),
            false
        );
        box->SetShadow(1.0f, Colour(0, 0, 80, 220), _Vector3<float>(2.0f, -2.0f, 0.0f), false);
        break;
    case 5:
        // Flat white + stroke (single colour, cyan outline).
        // API note: SetStroke stores but does not yet render (port TODO).
        box->SetColour(Colour(255, 255, 255, 255), 0);
        box->SetStroke(3.0f, Colour(0, 200, 255, 255));
        break;
    default:
        box->SetColour(Colour(255, 255, 255, 255), 0);
        break;
    }
}

// ---------------------------------------------------------------------------
// Glyph presence scan
// ---------------------------------------------------------------------------

// Returns true if any RGBA pixel in a CELL_W*CELL_H*4 buffer (bottom-up GL)
// has R or G or B > threshold. Used to detect whether glyphs actually rendered.
// Background is set to dark gray (R=G=B=0x18) so threshold=0x30 safely separates
// rendered glyphs from background noise.
static bool TR_HasGlyphs(const unsigned char* rgba) {
    const int N = CELL_W * CELL_H * 4;
    for (int i = 0; i < N; i += 4) {
        if (rgba[i] > 0x30 || rgba[i+1] > 0x30 || rgba[i+2] > 0x30) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Render one cell into CellFBO, read back pixels.
// Returns true if glyphs were rendered, false if the box was empty.
// outPixels must point to CELL_W*CELL_H*4 bytes.
// verdanaFont: optional FontCacheObjectTTF for Verdana; used for the cell
//   label strip. Falls back to labelFont->DrawString when NULL.
// ---------------------------------------------------------------------------
static bool TR_RenderCell(
    CellFBO& cfbo,
    Mortar::FontCacheObjectTTF* ttfFont,
    Mortar::Font* labelFont,
    Mortar::FontCacheObjectTTF* verdanaFont,
    const char* langCode,
    const char* sampleText,
    int effIdx,
    unsigned char* outPixels)
{
    cfbo.Bind();

    // Dark gray background (distinct from black so we can spot rendering).
    glClearColor(0.09f, 0.09f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    TR_SetupCellOrtho();

    bool hasGlyphs = false;

    if (ttfFont) {
        // BakedStringBox: centred in the text area (above the label strip).
        // boxW/boxH give the wrap bounds.
        const int boxW = CELL_W - 20;
        const int boxH = CELL_H - LABEL_H - 8;
        const float fontSize = 18.0f;

        // Text area vertical centre (in GL Y-up pixel space):
        //   Y=0 bottom, Y=CELL_H top; label strip at Y=[0,LABEL_H].
        //   Text area: [LABEL_H, CELL_H]; centre = LABEL_H + (CELL_H-LABEL_H)/2.
        const float textCentreX = (float)(CELL_W) * 0.5f;
        const float textCentreY = (float)LABEL_H + (float)(CELL_H - LABEL_H) * 0.5f;

        // align=0x0f: true horizontal centre (bits 0-1=0x3) + vertical centre (bits 2-3=0xC).
        // flag=1 in SetTranslation mirrors IngamePopup usage: m_Pos = pos - (boxW/2, -boxH/2)
        // so the box is centred on pos.x / pos.y, not anchored at its top-left.
        Mortar::BakedStringBox box(ttfFont, fontSize, boxW, boxH, (Mortar::ALIGNMENT_TYPE)0x0f, 1, 0);
        box.SetText(sampleText);
        TR_ApplyEffect(&box, effIdx);
        _Vector3<float> pos(textCentreX, textCentreY, 0.0f);
        box.SetTranslation(pos, 1);
        _Vector2<float> sc(1.0f, 1.0f);
        box.Draw(sc, 0.0f, 1);
    }

    // Read pixels to detect glyph presence.
    cfbo.ReadRGBA(outPixels);
    hasGlyphs = TR_HasGlyphs(outPixels);

    if (!hasGlyphs && labelFont) {
        // Re-clear and render fallback "[no glyphs:XX]" using the ASCII font.
        glClearColor(0.09f, 0.09f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        TR_SetupCellOrtho();

        char noGlyphMsg[32];
        std::snprintf(noGlyphMsg, sizeof(noGlyphMsg), "[no glyphs:%s]", langCode);

        const float scale = 7.0f;
        _Vector3<float> fallbackPos((float)(CELL_W) * 0.5f, (float)(CELL_H) * 0.5f, 0.0f);
        Colour grey(160, 160, 160, 255);
        // 0x3 = true horizontal centre (bits 0-1), 0x0 = top vertical -> centre flag bits.
        // align=0x03 centres horizontally on pos.x.
        labelFont->DrawString(scale, 1.0f, 0.0f, noGlyphMsg, fallbackPos, grey, 0x3);

        cfbo.ReadRGBA(outPixels);
    }

    if (verdanaFont) {
        // Verdana label via BakedStringBox (legible at small sizes).
        char label[40];
        std::snprintf(label, sizeof(label), "%s %s", langCode, EFF_NAMES[effIdx]);

        // Cell ortho: pixel-space, Y=0 bottom, Y=CELL_H top.
        // Label strip: Y=[0, LABEL_H]. Centre at (CELL_W/2, LABEL_H/2).
        const int boxW = CELL_W - 4;
        const int boxH = LABEL_H;
        const float fontSize = 9.0f;

        Mortar::BakedStringBox lblBox(verdanaFont, fontSize, boxW, boxH, (Mortar::ALIGNMENT_TYPE)0x0f, 1, 0);
        lblBox.SetText(label);
        lblBox.SetColour(Colour(210, 210, 210, 255), 0);

        // SetTranslation flag=1 pre-shifts: m_Pos.x = x - boxW/2, m_Pos.y = y + boxH/2.
        // With center-V and center=1, the ink centre lands on y = LABEL_H/2 = centre of strip.
        _Vector3<float> lblPos((float)(CELL_W / 2), (float)(LABEL_H / 2), 0.0f);
        lblBox.SetTranslation(lblPos, 1);
        _Vector2<float> lblSc(1.0f, 1.0f);
        lblBox.Draw(lblSc, 0.0f, 1);

        cfbo.ReadRGBA(outPixels);
    } else if (labelFont) {
        // Fallback: bitmap font label.
        char label[40];
        std::snprintf(label, sizeof(label), "%s:%s", langCode, EFF_NAMES[effIdx]);

        const float lblScale = 6.0f;
        _Vector3<float> lblPos(4.0f, 8.0f, 0.0f);
        Colour lblCol(200, 200, 200, 255);
        labelFont->DrawString(lblScale, 1.0f, 0.0f, label, lblPos, lblCol, 0x0);

        cfbo.ReadRGBA(outPixels);
    }

    cfbo.Unbind();
    return hasGlyphs;
}

// ---------------------------------------------------------------------------
// Build composite canvas and save PNG
// ---------------------------------------------------------------------------

static const int NUM_LANGS = LANG_COUNT;
static const int NUM_EFFS  = EFF_COUNT;

// For non-English rows, only populate cols 0 (FLAT), 2 (METALLIC), 4 (METAL+SHADOW).
static const int LANG_EFF_MASK[LANG_COUNT] = {
    0x3F,  // EN: all 6 effects (bits 0-5)
    0x15,  // ZH: cols 0, 2, 4
    0x15,  // JA: cols 0, 2, 4
    0x15,  // KO: cols 0, 2, 4
    0x15,  // FR: cols 0, 2, 4
    0x15   // AR: cols 0, 2, 4
};

static bool TR_SaveGrid(
    const unsigned char* const* cells,  // [LANG_COUNT * EFF_COUNT] row-major, each CELL_W*CELL_H*4 (bottom-up GL)
    fn::TestHarness& h,
    const char* name)
{
    const int totalW = NUM_EFFS  * CELL_W;
    const int totalH = NUM_LANGS * CELL_H;
    const size_t canvasBytes = (size_t)totalW * (size_t)totalH * 4;

    unsigned char* canvas = (unsigned char*)std::malloc(canvasBytes);
    if (!canvas) {
        fprintf(stderr, "[text_render] out of memory for grid canvas\n");
        return false;
    }

    // Fill background: dark charcoal.
    for (size_t i = 0; i < canvasBytes; i += 4) {
        canvas[i+0] = 0x12;
        canvas[i+1] = 0x12;
        canvas[i+2] = 0x12;
        canvas[i+3] = 0xFF;
    }

    // Blit each cell (GL bottom-up -> canvas top-down).
    for (int langIdx = 0; langIdx < NUM_LANGS; langIdx++) {
        for (int effIdx = 0; effIdx < NUM_EFFS; effIdx++) {
            const unsigned char* src = cells[langIdx * NUM_EFFS + effIdx];
            if (!src) continue;

            // Canvas cell origin (top-left, top-down).
            int dstCellX = effIdx  * CELL_W;
            int dstCellY = langIdx * CELL_H;

            for (int cy = 0; cy < CELL_H; cy++) {
                // GL row 0 = bottom; canvas row 0 = top.
                int glY = CELL_H - 1 - cy;
                const unsigned char* srcRow = src + (size_t)glY * CELL_W * 4;
                unsigned char* dstRow = canvas + ((size_t)(dstCellY + cy) * totalW + dstCellX) * 4;
                std::memcpy(dstRow, srcRow, (size_t)CELL_W * 4);
            }
        }
    }

    // Draw 1px separator lines between cells (medium gray).
    const unsigned char sepR = 0x3A, sepG = 0x3A, sepB = 0x3A;
    // Horizontal separators between rows.
    for (int langIdx = 1; langIdx < NUM_LANGS; langIdx++) {
        int sepY = langIdx * CELL_H;
        if (sepY >= totalH) continue;
        for (int x = 0; x < totalW; x++) {
            unsigned char* p = canvas + ((size_t)sepY * totalW + x) * 4;
            p[0] = sepR; p[1] = sepG; p[2] = sepB; p[3] = 0xFF;
        }
    }
    // Vertical separators between columns.
    for (int effIdx = 1; effIdx < NUM_EFFS; effIdx++) {
        int sepX = effIdx * CELL_W;
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
        fprintf(stderr, "[text_render] SDL_CreateRGBSurfaceFrom failed: %s\n", SDL_GetError());
        std::free(canvas);
        return false;
    }
    bool ok = h.SavePng(surf, name);
    SDL_FreeSurface(surf);
    std::free(canvas);
    if (!ok) return false;
    printf("[text_render] grid %dx%d (%d langs x %d effects)\n",
           totalW, totalH, NUM_LANGS, NUM_EFFS);
    return true;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "text_render");
    // 120 burn-in frames: allows GameInitialise + MainScreen to load
    // gangofchinese.ttf (lazily loaded on first MenuButton/BSButton creation).
    h.SetInitFrames(120);
    if (!h.ParseFlags()) return 1;
    if (!h.InitComponent()) {
        fprintf(stderr, "SKIP: InitComponent failed\n");
        return 77;
    }

    // Load GL extension functions.
    if (!TR_LoadFBO()) {
        fprintf(stderr, "SKIP: FBO extensions unavailable\n");
        return 77;
    }

    // Obtain the shared TTF font (gangofchinese.ttf).
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
    printf("[text_render] TTF font: gangofchinese.ttf OK\n");

    // Load arabic.ttf for the AR row (langIdx 5); gangofchinese.ttf has no Arabic glyphs.
    // arabicFont is registry-owned via s_ArabicFont SmartPtr -- no manual free needed.
    // Falls back to ttfFont (gangofchinese) if arabic.ttf fails to load.
    static Mortar::SmartPtr<Mortar::Font> s_ArabicFont =
        Mortar::Font::Create("fontstruetype/arabic.ttf");
    Mortar::FontCacheObjectTTF* arabicFont = NULL;
    if (s_ArabicFont.IsValid()) {
        arabicFont = Mortar::FontTTFRegistry::GetInstance().Lookup(s_ArabicFont.Get());
    }
    if (!arabicFont) {
        printf("[text_render] WARN: arabic.ttf failed to load -- AR row falls back to gangofchinese.ttf\n");
        arabicFont = ttfFont;
    } else {
        printf("[text_render] TTF font: arabic.ttf OK (AR row)\n");
    }

    // Label font: Verdana preferred (legible at small sizes); pFontMain as fallback.
    Mortar::Font* labelFont = NULL;
    if (game_work.pFontMain.IsValid()) {
        labelFont = game_work.pFontMain.Get();
        printf("[text_render] Fallback label font: pFontMain OK (glyphs=%d)\n",
               labelFont ? labelFont->m_GlyphCount : 0);
    } else {
        printf("[text_render] WARN: pFontMain not available -- no bitmap label fallback\n");
    }

    // Verdana via FontCacheObjectTTF (direct backend load, not via Font::Create path resolve).
    Mortar::FontCacheObjectTTF* verdanaFont = NULL;
    {
        const char* verdanaPath = "C:\\Windows\\Fonts\\verdana.ttf";
        verdanaFont = new Mortar::FontCacheObjectTTF(verdanaPath, 12);
        if (!verdanaFont->IsValid()) {
            delete verdanaFont;
            verdanaFont = NULL;
            printf("[text_render] WARN: verdana.ttf not found at %s -- using bitmap fallback\n",
                   verdanaPath);
        } else {
            printf("[text_render] Verdana label font: OK (%s)\n", verdanaPath);
        }
    }

    // Create shared cell FBO.
    CellFBO cfbo;
    if (!cfbo.Create()) {
        fprintf(stderr, "SKIP: CellFBO create failed\n");
        return 77;
    }

    // Allocate pixel buffers for all cells.
    const size_t cellBytes = (size_t)CELL_W * CELL_H * 4;
    const int totalCells = NUM_LANGS * NUM_EFFS;
    unsigned char** cellPixels = (unsigned char**)std::calloc((size_t)totalCells, sizeof(unsigned char*));
    if (!cellPixels) {
        fprintf(stderr, "FAIL: out of memory for cell pixel array\n");
        cfbo.Destroy();
        return 1;
    }
    for (int i = 0; i < totalCells; i++) {
        cellPixels[i] = (unsigned char*)std::malloc(cellBytes);
        if (!cellPixels[i]) {
            fprintf(stderr, "FAIL: out of memory for cell %d pixels\n", i);
            for (int j = 0; j < i; j++) std::free(cellPixels[j]);
            std::free(cellPixels);
            cfbo.Destroy();
            return 1;
        }
        // Default to empty dark background.
        std::memset(cellPixels[i], 0x12, cellBytes);
        for (int k = 3; k < (int)cellBytes; k += 4) {
            cellPixels[i][k] = 0xFF;
        }
    }

    // Render each cell.
    int noGlyphCount = 0;
    for (int langIdx = 0; langIdx < NUM_LANGS; langIdx++) {
        int mask = LANG_EFF_MASK[langIdx];
        for (int effIdx = 0; effIdx < NUM_EFFS; effIdx++) {
            if (!(mask & (1 << effIdx))) {
                // Not enabled for this language -- leave as dark background.
                printf("[text_render] %s x %s: skipped (non-EN row)\n",
                       LANG_CODES[langIdx], EFF_NAMES[effIdx]);
                continue;
            }

            unsigned char* buf = cellPixels[langIdx * NUM_EFFS + effIdx];
            Mortar::FontCacheObjectTTF* cellFont = (langIdx == 5) ? arabicFont : ttfFont;
            bool hasGlyphs = TR_RenderCell(
                cfbo, cellFont, labelFont, verdanaFont,
                LANG_CODES[langIdx], LANG_SAMPLES[langIdx],
                effIdx, buf);

            if (hasGlyphs) {
                printf("[text_render] %s x %s: glyphs OK\n",
                       LANG_CODES[langIdx], EFF_NAMES[effIdx]);
            } else {
                printf("[text_render] %s x %s: NO GLYPHS (fallback rendered)\n",
                       LANG_CODES[langIdx], EFF_NAMES[effIdx]);
                noGlyphCount++;
            }
        }
    }

    printf("[text_render] Cells with no glyphs: %d / %d enabled\n",
           noGlyphCount, NUM_LANGS * NUM_EFFS);
    printf("[text_render] Shadow/stroke passes now active:\n");
    printf("  METAL+STROKE: 8-direction solid outline in m_StrokeCol0\n");
    printf("  METAL+SHADOW: solid copy at anchor + m_ShadowOffset (2px right/down demo)\n");
    printf("  FLAT+STROKE:  cyan 8-dir outline around white text\n");
    printf("  DIFFERS from binary (blurred glyph atlas) -- see BakedStringBox.h SetShadow/SetStroke\n");

    // Save output PNG.
    bool saved = false;
    if (h.IsScreenshot()) {
        const unsigned char* const* constCells =
            (const unsigned char* const*)cellPixels;
        saved = TR_SaveGrid(constCells, h, "text_render/grid");
        if (!saved) {
            fprintf(stderr, "FAIL: could not save grid PNG\n");
        }
    }

    // Free pixel buffers.
    for (int i = 0; i < totalCells; i++) {
        std::free(cellPixels[i]);
    }
    std::free(cellPixels);

    cfbo.Destroy();

    // Release Verdana TTF (not registered with FontTTFRegistry, so manual delete).
    delete verdanaFont;
    verdanaFont = NULL;

    // Drop the TTF Font handles BEFORE Shutdown(): GameDestroy's GL-handle leak
    // check runs inside game.shutdown(), and these statics would still hold the
    // FontCacheObjectTTF (and its FontInterface atlas pages) at that point.
    // ttfFont / arabicFont dangle from here on and must not be used again.
    s_TTFFont.SetNull();
    s_ArabicFont.SetNull();
    ttfFont = NULL;
    arabicFont = NULL;

    if (h.IsScreenshot() && !saved) {
        h.Shutdown();
        return 1;
    }

    printf("PASS: test_text_render complete\n");
    return h.Shutdown();
}
