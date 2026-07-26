// test_bakedstringbox_baseline.cpp -- Pure deterministic unit test for
// BakedStringBox::ComputeBaselineY() vertical baseline math.
//
// Binary: v1.6.1 Mortar::BakedStringBox::RebuildAlignments @0x00245c78
//
// Pins the exact binary formula for all three vertical-alignment cases:
//   center-V single-line:  -boxH*0.5 - (fontSize+4.0)*0.5
//     (else-branch @0x00245e74; metric-INDEPENDENT -- FontInterface::GetInstance()[0]=0x48!=0
//      always takes this branch at runtime; the metric-based if-branch @0x00245d74 is dead code)
//   center-V multi-line:   (step*nLines)*0.5 - step*0.5 - boxH*0.5 - maxSpan*0.5 - i*step
//   top-anchored:          -(ascentSpan*0.5) - step*0.5 - descent
//   bottom-anchored:       boxH
//
// Pure in-process: no GPU, no audio, no FreeType, no SDL.
// Cross-build safe: no lambdas, no auto, no range-for, no enum class.

// FN_GL_STUB provides stub GL typedefs from gl_compat.h without pulling SDL/GL.
#define FN_GL_STUB

#include "render/BakedStringBox.h"
#include "render/FontCacheObjectTTF.h"
#include "render/FontInterface.h"
#include "render/MatrixManager.h"
#include "render/MatrixStack.h"
#include "render/Renderer.h"
#include "math/Colour.h"
#include "math/Matrix44.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>

// ---------------------------------------------------------------------------
// Test macros
// ---------------------------------------------------------------------------
#define CHECK_NEAR(actual, expected, tol) \
    do { \
        float _a = (float)(actual); \
        float _e = (float)(expected); \
        float _t = (float)(tol); \
        float _d = _a - _e; \
        if (_d < 0.0f) _d = -_d; \
        if (_d > _t) { \
            std::printf("FAIL (%s:%d): got %.6f expected %.6f (tol %.6f)\n", \
                __FILE__, __LINE__, (double)_a, (double)_e, (double)_t); \
            ::exit(1); \
        } \
    } while(0)

#define CHECK_DIFFERS(a, b, tol) \
    do { \
        float _a = (float)(a); \
        float _b = (float)(b); \
        float _t = (float)(tol); \
        float _d = _a - _b; \
        if (_d < 0.0f) _d = -_d; \
        if (_d <= _t) { \
            std::printf("FAIL (%s:%d): values %.6f and %.6f should differ by > %.6f\n", \
                __FILE__, __LINE__, (double)_a, (double)_b, (double)_t); \
            ::exit(1); \
        } \
    } while(0)

// ---------------------------------------------------------------------------
// GL stub definitions (link-time only; ComputeBaselineY never calls GL).
// ---------------------------------------------------------------------------
extern "C" {
    const GLubyte* glGetString(GLenum) { return reinterpret_cast<const GLubyte*>("stub"); }
    GLenum  glGetError(void) { return GL_NO_ERROR; }
    void glViewport(GLint, GLint, GLsizei, GLsizei) {}
    void glClearColor(GLfloat, GLfloat, GLfloat, GLfloat) {}
    void glClear(GLbitfield) {}
    void glEnable(GLenum) {}
    void glDisable(GLenum) {}
    void glBlendFunc(GLenum, GLenum) {}
    void glScissor(GLint, GLint, GLsizei, GLsizei) {}
    void glPixelStorei(GLenum, GLint) {}
    void glGenTextures(GLsizei, GLuint*) {}
    void glDeleteTextures(GLsizei, const GLuint*) {}
    void glBindTexture(GLenum, GLuint) {}
    void glTexParameteri(GLenum, GLenum, GLint) {}
    void glTexImage2D(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) {}
    void glTexSubImage2D(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*) {}
    void glCompressedTexImage2D(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void*) {}
    void glActiveTexture(GLenum) {}
    void glDrawArrays(GLenum, GLint, GLsizei) {}
    void glGenBuffers(GLsizei, GLuint*) {}
    void glDeleteBuffers(GLsizei, const GLuint*) {}
    void glBindBuffer(GLenum, GLuint) {}
    void glBufferData(GLenum, GLsizeiptr, const void*, GLenum) {}
    void glBufferSubData(GLenum, GLintptr, GLsizeiptr, const void*) {}
    void glDrawElements(GLenum, GLsizei, GLenum, const void*) {}
    void glDepthFunc(GLenum) {}
    void glDepthMask(GLboolean) {}
    void glClearDepthf(GLclampf) {}
    void glMatrixMode(GLenum) {}
    void glPushMatrix(void) {}
    void glPopMatrix(void) {}
    void glLoadMatrixf(const GLfloat*) {}
    void glMultMatrixf(const GLfloat*) {}
    void glLoadIdentity(void) {}
    void glFrustumf(GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat) {}
    void glEnableClientState(GLenum) {}
    void glDisableClientState(GLenum) {}
    void glVertexPointer(GLint, GLenum, GLsizei, const void*) {}
    void glNormalPointer(GLenum, GLsizei, const void*) {}
    void glColorPointer(GLint, GLenum, GLsizei, const void*) {}
    void glTexCoordPointer(GLint, GLenum, GLsizei, const void*) {}
    void glClientActiveTexture(GLenum) {}
    void glColor4ub(GLubyte, GLubyte, GLubyte, GLubyte) {}
    void glMaterialfv(GLenum, GLenum, const GLfloat*) {}
    void glLightfv(GLenum, GLenum, const GLfloat*) {}
    void glShadeModel(GLenum) {}
    void glTexEnvf(GLenum, GLenum, GLfloat) {}
    void glTexEnvi(GLenum, GLenum, GLint) {}
    void glPolygonMode(GLenum, GLenum) {}
}

// ---------------------------------------------------------------------------
// MatrixStack stubs
// ---------------------------------------------------------------------------
void MatrixStack::Push() {}
void MatrixStack::Pop() {}
void MatrixStack::Reset() {}
void MatrixStack::Scale(const _Vector3<float>&) {}
void MatrixStack::Translate(const _Vector3<float>&) {}
void MatrixStack::SetCurrentMatrix(const Matrix44&) {}
// RotZ/TranslateLocal/ScaleRows are referenced by BakedStringTTF::Draw (now linked in
// for the BakedStringBox -> FancyBakedString -> BakedStringTTF dispatcher chain) but
// never actually invoked by this test (it only exercises the static ComputeBaselineY).
// Link-only stubs.
void MatrixStack::RotZ(float) {}
void MatrixStack::TranslateLocal(const _Vector3<float>&) {}
void MatrixStack::ScaleRows(float, float, float) {}

// ---------------------------------------------------------------------------
// MatrixManager stubs
// ---------------------------------------------------------------------------
MatrixManager::MatrixManager()
    : m_ViewVersion(0)
    , m_ViewVersionUploaded(0)
    , m_WorldVersionUploaded(0)
    , m_TextureVersionUploaded(0)
    , m_ProjVersionUploaded(0)
{}
MatrixManager MatrixManager::s_instance;
MatrixManager::~MatrixManager() {}
void MatrixManager::SetupOrtho(float, float, float, float, float, float, Matrix44*) {}
void MatrixManager::SetupLookAt(const _Vector3<float>&, const _Vector3<float>&, const _Vector3<float>&, Matrix43*) {}
void MatrixManager::UploadAll() {}
void MatrixManager::UploadModelViewOnly() {}
void MatrixManager::ResetAllStacks() {}
Matrix44 MatrixManager::GetMVP() const { return Matrix44(); }
void MatrixManager::SetupPerspective(float, float, float, float, float, Matrix44*) {}

// ---------------------------------------------------------------------------
// Renderer stubs
// ---------------------------------------------------------------------------
Renderer* Renderer::s_instance = nullptr;
void Renderer::DrawTriStrip(QUADCUSTOMVERTEX*, int) {}
void Renderer::DrawColorQuad(const Colour&) {}
void Renderer::BindTexture2D(uint32_t) {}
// Link-only stub: BakedStringTTF::Draw calls this; never invoked here (s_instance is
// null and this test never calls BakedStringBox::Draw / FancyBakedString::Draw).
void Renderer::DrawTriList(QUADCUSTOMVERTEX*, int, bool) {}

// ---------------------------------------------------------------------------
// FontInterface stubs
// ---------------------------------------------------------------------------
namespace Mortar {

FontInterface::FontInterface()
    : m_CacheSize(100)
    , m_FontScale(1.0f)
    , m_InvFontScale(1.0f)
    , m_GlobalSizeScale(1.0f)
    , m_Size(512)
{}
FontInterface::~FontInterface() {}
void FontInterface::InitialiseData(float fs, float gs) {
    m_FontScale = fs;
    m_InvFontScale = (fs > 0.0f ? 1.0f / fs : 1.0f);
    m_GlobalSizeScale = gs;
}
bool FontInterface::PackGlyph(int, int, const uint8_t*, GlyphAtlasEntry* out) {
    if (out) out->pageTextureID = 0;
    return true;
}
void FontInterface::BuildPendingTextures() {}
void FontInterface::Clear() {}
GLuint FontInterface::GetPageTextureID(int) const { return 0; }
FontAtlasPage* FontInterface::AllocatePage() { return nullptr; }
void FontInterface::EnsurePageTexture(FontAtlasPage*) {}
void FontInterface::MarkPageDirty(FontAtlasPage*, int, int, int, int) {}

// ---------------------------------------------------------------------------
// FontCacheObjectTTF stubs
// ---------------------------------------------------------------------------
static GlyphAtlasEntry s_dummy_glyph;
static bool s_dummy_initialized = false;

static void init_dummy_glyph() {
    if (s_dummy_initialized) return;
    memset(&s_dummy_glyph, 0, sizeof(s_dummy_glyph));
    s_dummy_glyph.advanceX = 1.0f;
    s_dummy_initialized = true;
}

FontCacheObjectTTF::FontCacheObjectTTF(const char*, int pixelSize)
    : m_Face(nullptr)
    , m_DefaultPixelSize(pixelSize)
    , m_CurrentCharHeight(-1)
    , m_Atlas(nullptr)
{
    init_dummy_glyph();
}
FontCacheObjectTTF::~FontCacheObjectTTF() {}
bool FontCacheObjectTTF::IsValid() const { return true; }
const GlyphAtlasEntry* FontCacheObjectTTF::GetGlyph(uint32_t, float, FONT_EFFECT_ENUM, int) {
    return &s_dummy_glyph;
}
float FontCacheObjectTTF::GetKerningForPair(uint32_t, uint32_t, float) { return 0.0f; }
float FontCacheObjectTTF::GetAscender(float s) { return s; }
float FontCacheObjectTTF::GetDescender(float) { return 0.0f; }
float FontCacheObjectTTF::GetLineHeight(float s) { return s; }
bool FontCacheObjectTTF::SetCharSize(long) { return true; }

} // namespace Mortar

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// align=0x0e: bits 3:2 = 0b11 (0xc) -> center-V; bits 1:0 = 0b10 -> center-H.
// (The H-alignment bits don't affect ComputeBaselineY; any value with (align&0xc)==0xc works.)
static const int ALIGN_CENTER_V = 0x0e;
// align=0x00: bits 3:2 = 0b00 -> top-anchored.
static const int ALIGN_TOP = 0x00;
// align=0x08: bits 3:2 = 0b10 -> bottom-anchored (bit 3 set, bit 2 clear => (align&0xc)==0x8).
static const int ALIGN_BOTTOM = 0x08;

using Mortar::BakedStringBox;

static void test_single_line_center_v() {
    // v1.6.1 RebuildAlignments @0x00245c78: single-line center-V else-branch @0x00245e74.
    // formula: -boxH*0.5 - (fontSize+4.0)*0.5   (metric-INDEPENDENT, VFP const 4.0 = 0x40800000)
    // maxBearingY/minBottom are NOT used in this branch; step/maxSpan also unused.

    // Case A: boxH=30, fontSize=20 -> -30*0.5 - (20+4)*0.5 = -15 - 12 = -27.0
    float rA = BakedStringBox::ComputeBaselineY(ALIGN_CENTER_V, 1, 0, 12.0f, -3.0f, 30.0f, 0.0f, 0.0f, 20.0f);
    CHECK_NEAR(rA, -27.0f, 1e-5f);
    std::printf("  single-line center-V (boxH=30,fs=20): %.6f == -27.0 OK\n", (double)rA);

    // Case B: boxH=20, fontSize=16 -> -20*0.5 - (16+4)*0.5 = -10 - 10 = -20.0
    float rB = BakedStringBox::ComputeBaselineY(ALIGN_CENTER_V, 1, 0, 12.0f, -3.0f, 20.0f, 0.0f, 0.0f, 16.0f);
    CHECK_NEAR(rB, -20.0f, 1e-5f);
    std::printf("  single-line center-V (boxH=20,fs=16): %.6f == -20.0 OK\n", (double)rB);

    // Case C: boxH=30, fontSize=12 -> -30*0.5 - (12+4)*0.5 = -15 - 8 = -23.0
    float rC = BakedStringBox::ComputeBaselineY(ALIGN_CENTER_V, 1, 0, 12.0f, -3.0f, 30.0f, 0.0f, 0.0f, 12.0f);
    CHECK_NEAR(rC, -23.0f, 1e-5f);
    std::printf("  single-line center-V (boxH=30,fs=12): %.6f == -23.0 OK\n", (double)rC);

    // Metric-independence assertion: varying maxBearingY and minBottom must NOT change the result.
    // Same boxH=30, fontSize=20 but very different metric values -> identical baseline.
    float rA2 = BakedStringBox::ComputeBaselineY(ALIGN_CENTER_V, 1, 0, 99.0f, -50.0f, 30.0f, 0.0f, 0.0f, 20.0f);
    CHECK_NEAR(rA2, rA, 1e-5f);
    float rA3 = BakedStringBox::ComputeBaselineY(ALIGN_CENTER_V, 1, 0, 0.0f, 0.0f, 30.0f, 0.0f, 0.0f, 20.0f);
    CHECK_NEAR(rA3, rA, 1e-5f);
    std::printf("  single-line metric-independence (vary bearingY/bottom, same result): OK\n");

    std::printf("test_single_line_center_v: PASS\n");
}

static void test_multi_line_center_v() {
    // v1.6.1 RebuildAlignments @0x00245c78: multi-line center-V per line i:
    //   (step*nLines)*0.5 - step*0.5 - boxH*0.5 - maxSpan*0.5 - i*step
    // lineH=18, nLines=3, maxSpan=15, boxH=80
    // line0: (18*3)*0.5 - 18*0.5 - 80*0.5 - 15*0.5 - 0*18
    //       = 27 - 9 - 40 - 7.5 = -29.5
    // line1: -29.5 - 18 = -47.5
    // line2: -47.5 - 18 = -65.5

    const float step    = 18.0f;
    const float maxSpan = 15.0f;
    const float boxH    = 80.0f;
    const int   nLines  = 3;

    float r0 = BakedStringBox::ComputeBaselineY(ALIGN_CENTER_V, nLines, 0, 0.0f, 0.0f, boxH, step, maxSpan, 0.0f);
    float r1 = BakedStringBox::ComputeBaselineY(ALIGN_CENTER_V, nLines, 1, 0.0f, 0.0f, boxH, step, maxSpan, 0.0f);
    float r2 = BakedStringBox::ComputeBaselineY(ALIGN_CENTER_V, nLines, 2, 0.0f, 0.0f, boxH, step, maxSpan, 0.0f);

    CHECK_NEAR(r0, -29.5f, 1e-4f);
    CHECK_NEAR(r1, -47.5f, 1e-4f);
    CHECK_NEAR(r2, -65.5f, 1e-4f);

    // Adjacent lines must differ by exactly step.
    CHECK_NEAR(r0 - r1, step, 1e-5f);
    CHECK_NEAR(r1 - r2, step, 1e-5f);

    std::printf("  multi-line center-V line0: %.6f == -29.5 OK\n", (double)r0);
    std::printf("  multi-line center-V line1: %.6f == -47.5 OK\n", (double)r1);
    std::printf("  multi-line center-V line2: %.6f == -65.5 OK\n", (double)r2);

    // Verify that maxSpan contributes: doubling maxSpan shifts baseline down by maxSpan/2 = 7.5.
    float r0_big = BakedStringBox::ComputeBaselineY(ALIGN_CENTER_V, nLines, 0, 0.0f, 0.0f, boxH, step, maxSpan * 2.0f, 0.0f);
    CHECK_NEAR(r0 - r0_big, maxSpan * 0.5f, 1e-5f);
    std::printf("  multi-line maxSpan contribution (delta=7.5): OK\n");

    std::printf("test_multi_line_center_v: PASS\n");
}

static void test_top_anchored() {
    // top-anchored: -(ascentSpan*0.5) - step*0.5 + descent  (v1.6.1 RebuildAlignments
    //   @0x00245c78; task #31 fixed a flipped descent sign -- was `- descent`).
    // ascentSpan = maxBearingY - minBottom, descent = -minBottom
    // maxBearingY=10, minBottom=-2, step=12
    // ascentSpan=12, descent=2
    // expected = -(12*0.5) - 12*0.5 + 2 = -6 - 6 + 2 = -10
    //   -> ink-center = baselineY + (maxBearingY+minBottom)/2 = -10 + 4 = -6 = -step/2,
    //      i.e. ink-center = translationY - step/2 (metric terms cancel, as the binary does).
    //   The prior -14 expectation encoded the flipped-descent bug (ink 2*descent=4px too low).
    float r = BakedStringBox::ComputeBaselineY(ALIGN_TOP, 1, 0, 10.0f, -2.0f, 50.0f, 12.0f, 0.0f, 0.0f);
    CHECK_NEAR(r, -10.0f, 1e-5f);
    std::printf("  top-anchored: %.6f == -10.0 OK\n", (double)r);

    // nLines > 1 should give same formula (top-anchored uses line0 metrics only, no per-line term at call site).
    float r3 = BakedStringBox::ComputeBaselineY(ALIGN_TOP, 3, 0, 10.0f, -2.0f, 50.0f, 12.0f, 0.0f, 0.0f);
    CHECK_NEAR(r3, -10.0f, 1e-5f);
    std::printf("  top-anchored (nLines=3): %.6f == -10.0 OK\n", (double)r3);

    std::printf("test_top_anchored: PASS\n");
}

static void test_bottom_anchored() {
    // bottom-anchored: returns boxH (always).
    float r = BakedStringBox::ComputeBaselineY(ALIGN_BOTTOM, 1, 0, 10.0f, -2.0f, 50.0f, 12.0f, 5.0f, 0.0f);
    CHECK_NEAR(r, 50.0f, 1e-5f);
    std::printf("  bottom-anchored: %.6f == 50.0 OK\n", (double)r);

    float r2 = BakedStringBox::ComputeBaselineY(ALIGN_BOTTOM, 3, 2, 10.0f, -2.0f, 100.0f, 12.0f, 5.0f, 0.0f);
    CHECK_NEAR(r2, 100.0f, 1e-5f);
    std::printf("  bottom-anchored (boxH=100): %.6f == 100.0 OK\n", (double)r2);

    std::printf("test_bottom_anchored: PASS\n");
}

int main() {
    std::printf("BakedStringBox::ComputeBaselineY unit tests\n");
    std::printf("  binary: v1.6.1 RebuildAlignments @0x00245c78\n");

    test_single_line_center_v();
    test_multi_line_center_v();
    test_top_anchored();
    test_bottom_anchored();

    std::printf("All tests PASSED.\n");
    return 0;
}
