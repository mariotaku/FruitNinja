// test_utf8_text.cpp -- Regression guard for the #216 mojibake fix.
//
// Two layers:
//   Layer 1: Utf8StringIterator + decode_next_unicode_character unit coverage.
//   Layer 2: THE GUARD -- BakedStringBox must pass DECODED codepoints to
//             GetGlyph(), not raw UTF-8 bytes. A recording FontCacheObjectTTF
//             stub captures every codepoint; the test asserts the exact set.
//
// This file also provides all stub definitions that BakedStringBox.cpp needs
// at link time (GL, MatrixManager, Renderer, FontInterface) so the test
// target does NOT link mortar_engine or fruit-ninja-game, avoiding GL/FreeType.
//
// Compile with FN_GL_STUB defined (CMakeLists sets it): gl_compat.h then
// provides stub GL types without pulling in SDL or system GL headers.
//
// Pure in-process: no GPU, no audio, no FreeType, no SDL.
// Cross-build safe: no lambdas, no auto, no range-for, no enum class.

#include "render/Utf8StringIterator.h"
#include "render/FontCacheObjectTTF.h"
#include "render/FontInterface.h"
#include "render/BakedStringBox.h"
#include "render/FancyBakedString.h"
#include "render/MatrixManager.h"
#include "render/Renderer.h"
#include "math/Colour.h"
#include "math/Matrix44.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cmath>

// ---------------------------------------------------------------------------
// Test macros
// ---------------------------------------------------------------------------
#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::printf("FAIL (%s:%d): %s\n", __FILE__, __LINE__, #cond); \
            ::exit(1); \
        } \
    } while(0)

#define CHECK_EQ(a, b) \
    do { \
        unsigned long long _a = (unsigned long long)(a); \
        unsigned long long _b = (unsigned long long)(b); \
        if (_a != _b) { \
            std::printf("FAIL (%s:%d): %llu != %llu\n", \
                __FILE__, __LINE__, _a, _b); \
            ::exit(1); \
        } \
    } while(0)

// ---------------------------------------------------------------------------
// GL stub definitions
// gl_compat.h with FN_GL_STUB provides the typedef stubs; we need function
// bodies for everything BakedStringBox.cpp references at link time.
// BakedStringBox::Draw() uses all the GL calls; we never call Draw() but
// the linker requires the symbols.
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
// MatrixStack out-of-line method stubs (Push/Pop/Reset/Scale/Translate/SetCurrentMatrix).
// BakedStringBox::Draw() calls Push()/Pop(); the others are referenced from MatrixManager.
// MatrixStack ctor is inline in the header so no stub needed.
// ---------------------------------------------------------------------------
void MatrixStack::Push() {}
void MatrixStack::Pop() {}
void MatrixStack::Reset() {}
void MatrixStack::Scale(const _Vector3<float>&) {}
void MatrixStack::Translate(const _Vector3<float>&) {}
void MatrixStack::SetCurrentMatrix(const Matrix44&) {}

// ---------------------------------------------------------------------------
// MatrixManager stubs.
// GetInstance() is inline in the header. Static s_instance defined here.
// The default ctor is private; we define it here to satisfy the linker.
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
// Renderer stub (s_instance pointer; Draw* methods no-op in this test).
// ---------------------------------------------------------------------------
Renderer* Renderer::s_instance = nullptr;
void Renderer::DrawTriStrip(QUADCUSTOMVERTEX*, int) {}
void Renderer::DrawColorQuad(const Colour&) {}

// ---------------------------------------------------------------------------
// FontInterface stub — multi-page API (updated for #263 multi-page atlas).
// GetAtlas() in FontCacheObjectTTF returns m_Atlas which is nullptr in our stub
// ctor, so BakedStringBox::Layout()'s "if (atlas) atlas->BuildPendingTextures()"
// guard skips all GL calls. We still need all the method bodies at link time.
// GetTextureID(), GetSize(), GetPageCount() are inline in FontInterface.h.
// GetPageTextureID(int) is out-of-line and needs a stub body here.
// Private helpers AllocatePage/EnsurePageTexture/MarkPageDirty also need bodies.
// ---------------------------------------------------------------------------
namespace Mortar {

FontInterface::FontInterface()
    : m_CacheSize(100)
    , m_FontScale(1.0f)
    , m_InvFontScale(1.0f)
    , m_GlobalSizeScale(1.0f)
    , m_Size(512)
    // m_Pages (std::vector) default-constructs to empty — no explicit init needed.
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
// FancyBakedString stub (link-only).
//
// BakedStringBox::RebuildMeshes constructs one FancyBakedString per wrapped
// line and calls its Apply*/GetBounds/Draw methods; SetText() below drives
// that path. FancyBakedString.cpp is deliberately NOT in this test's source
// list (it would cascade into BakedStringTTF.cpp -> Font/Mesh/... -- the
// whack-a-mole this isolated test exists to avoid), so this TU provides the
// only definition of the class body the linker sees, mirroring the
// FontCacheObjectTTF recording-stub pattern above.
//
// The ctor stores nothing real (the six BakedStringTTF layer ptrs stay
// null); GetBounds() returning null is a path BakedStringBox::RebuildMeshes
// already null-checks (see `bounds ? bounds->Width() : 0.0f`). Draw()/Apply*
// are no-ops -- this test never renders or recolours, only measures codepoint
// recording through SetText().
//
// MSVC (C2888) rejects `namespace Mortar { void Mortar::FancyBakedString::X() {} }`
// -- out-of-line member definitions must be at file/global scope with the
// fully-qualified name, so unlike the other stub blocks in this file these
// are NOT wrapped in `namespace Mortar { ... }`.
// ---------------------------------------------------------------------------

Mortar::FancyBakedString::FancyBakedString(Mortar::FontCacheObjectTTF*, const char*, float,
                                           Colour, int, float,
                                           float, Colour,
                                           float, Colour,
                                           float, Colour,
                                           int, float, int,
                                           Colour, Colour)
    : m_ShadowOffset(0.0f, 0.0f, 0.0f)
    , m_LineOffset(0.0f, 0.0f, 0.0f)
    , m_pShadow(nullptr)
    , m_pGlow(nullptr)
    , m_pMain(nullptr)
    , m_pStroke(nullptr)
    , m_pExtra1(nullptr)
    , m_pExtra2(nullptr)
    , m_ShadowColour()
    , m_GlowColour()
{}

Mortar::FancyBakedString::~FancyBakedString() {}

void Mortar::FancyBakedString::Draw(const _Vector3<float>&, _Vector2<float>, float, Mortar::ALIGNMENT_TYPE) {}

void Mortar::FancyBakedString::ApplyGradientSplit(Colour, float) {}
void Mortar::FancyBakedString::ApplyGradient(Colour) {}
void Mortar::FancyBakedString::ApplyGradient(Colour, Colour) {}
void Mortar::FancyBakedString::ApplyGradient(Colour, Colour, Colour) {}
void Mortar::FancyBakedString::ApplyMetallicGradient(Colour, Colour, Colour, Colour) {}
void Mortar::FancyBakedString::ApplyStrokeGradient(Colour, Colour) {}
void Mortar::FancyBakedString::ApplyStrokeGradient(Colour, Colour, Colour) {}

// ---------------------------------------------------------------------------
// RECORDING FontCacheObjectTTF stub
//
// FontCacheObjectTTF has no virtual methods, so the only injection seam is to
// provide a completely separate definition of the class body in this TU. The
// test target is built WITHOUT FontCacheObjectTTF.cpp in its source list, so
// this definition is the only one the linker sees.
//
// GetGlyph() records every codepoint. GetAtlas() returns nullptr so
// BuildPendingTextures() is never called (GL-free path through Layout()).
//
// The returned GlyphAtlasEntry has advanceX=1.0 (layout progresses) and
// width=height=0 (no vertex quads emitted -- avoids GL atlas calls).
// ---------------------------------------------------------------------------

} // namespace Mortar

// Global recording state -- in file scope so all test functions can access it.
static std::vector<uint32_t> g_recorded_codepoints;

static Mortar::GlyphAtlasEntry s_dummy_glyph;
static bool s_dummy_initialized = false;

static void init_dummy_glyph() {
    if (s_dummy_initialized) return;
    memset(&s_dummy_glyph, 0, sizeof(s_dummy_glyph));
    s_dummy_glyph.advanceX = 1.0f;
    s_dummy_glyph.bearingX = 0.0f;
    s_dummy_glyph.bearingY = 0.0f;
    s_dummy_glyph.width    = 0.0f;  // zero: no quad emitted
    s_dummy_glyph.height   = 0.0f;
    s_dummy_glyph.u0 = s_dummy_glyph.v0 = 0.0f;
    s_dummy_glyph.u1 = s_dummy_glyph.v1 = 0.0f;
    s_dummy_initialized = true;
}

namespace Mortar {

FontCacheObjectTTF::FontCacheObjectTTF(const char*, int pixelSize)
    : m_Face(nullptr)
    , m_DefaultPixelSize(pixelSize)
    , m_CurrentCharHeight(-1)
    , m_Atlas(nullptr)  // nullptr -> GetAtlas() returns nullptr -> no GL in Layout()
{
    init_dummy_glyph();
}

FontCacheObjectTTF::~FontCacheObjectTTF() {}
bool FontCacheObjectTTF::IsValid() const { return true; }

// THE RECORDING METHOD: captures every codepoint BakedStringBox asks about.
const GlyphAtlasEntry* FontCacheObjectTTF::GetGlyph(uint32_t cp, float /*requestedSize*/,
                                                     FONT_EFFECT_ENUM /*effect*/, int /*radius*/) {
    g_recorded_codepoints.push_back(cp);
    return &s_dummy_glyph;
}

float FontCacheObjectTTF::GetKerningForPair(uint32_t, uint32_t, float) { return 0.0f; }
float FontCacheObjectTTF::GetAscender(float s) { return s; }
float FontCacheObjectTTF::GetDescender(float) { return 0.0f; }
float FontCacheObjectTTF::GetLineHeight(float s) { return s; }
// Private method -- accessed internally by the real implementation.
// Our stub never calls it; we define it to satisfy the linker.
bool FontCacheObjectTTF::SetCharSize(long) { return true; }

} // namespace Mortar

// ---------------------------------------------------------------------------
// Layer 1: Utf8StringIterator unit tests
// ---------------------------------------------------------------------------

static std::vector<uint32_t> collect_via_iterator(const char* str) {
    std::vector<uint32_t> out;
    Mortar::Utf8StringIterator it(str);
    while (!it.IsEmpty()) {
        out.push_back(it.m_CurrentCodepoint);
        it++;
    }
    return out;
}

static std::vector<uint32_t> collect_via_decode_next(const char* str) {
    std::vector<uint32_t> out;
    const char* p = str;
    while (*p) {
        uint32_t cp = Mortar::utf8::decode_next_unicode_character(&p);
        if (cp == 0) break;
        out.push_back(cp);
    }
    return out;
}

static void test_utf8_ascii() {
    // "AB" -> 0x41, 0x42
    std::vector<uint32_t> via_it  = collect_via_iterator("AB");
    std::vector<uint32_t> via_raw = collect_via_decode_next("AB");

    CHECK_EQ(via_it.size(),  2u);
    CHECK_EQ(via_it[0], 0x41u);
    CHECK_EQ(via_it[1], 0x42u);
    CHECK_EQ(via_raw.size(), 2u);
    CHECK_EQ(via_raw[0], 0x41u);
    CHECK_EQ(via_raw[1], 0x42u);

    std::printf("  ASCII 'AB' -> {0x41,0x42}: OK\n");
}

static void test_utf8_2byte() {
    // U+00E9 "e with acute" = 0xC3 0xA9
    const char str[] = { (char)0xC3, (char)0xA9, '\0' };
    std::vector<uint32_t> via_it  = collect_via_iterator(str);
    std::vector<uint32_t> via_raw = collect_via_decode_next(str);

    CHECK_EQ(via_it.size(),  1u);
    CHECK_EQ(via_it[0], 0xE9u);
    CHECK_EQ(via_raw.size(), 1u);
    CHECK_EQ(via_raw[0], 0xE9u);

    std::printf("  2-byte U+00E9 (e-acute) 0xC3 0xA9 -> 0xE9: OK\n");
}

static void test_utf8_3byte_cjk() {
    // U+5956 = 0xE5 0xA5 0x96
    const char str1[] = { (char)0xE5, (char)0xA5, (char)0x96, '\0' };
    std::vector<uint32_t> r1 = collect_via_iterator(str1);
    CHECK_EQ(r1.size(), 1u);
    CHECK_EQ(r1[0], 0x5956u);

    std::vector<uint32_t> r1r = collect_via_decode_next(str1);
    CHECK_EQ(r1r.size(), 1u);
    CHECK_EQ(r1r[0], 0x5956u);

    // U+8FDE = 0xE8 0xBF 0x9E
    // Verify: 0x8FDE = 1000_1111_1101_1110 -> 4|6|6 = 1000 | 111111 | 011110
    //   lead  = 1110_1000 = 0xE8
    //   cont1 = 10_111111 = 0xBF
    //   cont2 = 10_011110 = 0x9E
    const char str2[] = { (char)0xE8, (char)0xBF, (char)0x9E, '\0' };
    std::vector<uint32_t> r2 = collect_via_iterator(str2);
    CHECK_EQ(r2.size(), 1u);
    CHECK_EQ(r2[0], 0x8FDEu);

    std::printf("  3-byte CJK U+5956 (E5A596) and U+8FDE (E8BF9E): OK\n");
}

static void test_utf8_4byte() {
    // U+1F600 (grinning face) = F0 9F 98 80
    const char str[] = { (char)0xF0, (char)0x9F, (char)0x98, (char)0x80, '\0' };
    std::vector<uint32_t> via_it  = collect_via_iterator(str);
    std::vector<uint32_t> via_raw = collect_via_decode_next(str);

    CHECK_EQ(via_it.size(),  1u);
    CHECK_EQ(via_it[0], 0x1F600u);
    CHECK_EQ(via_raw.size(), 1u);
    CHECK_EQ(via_raw[0], 0x1F600u);

    std::printf("  4-byte U+1F600 (F0 9F 98 80) -> 0x1F600: OK\n");
}

static void test_utf8_mixed() {
    // 'A' (0x41) + U+5956 (E5 A5 96) = 4 bytes total -> 2 codepoints
    const char str[] = { 'A', (char)0xE5, (char)0xA5, (char)0x96, '\0' };
    std::vector<uint32_t> via_it  = collect_via_iterator(str);
    std::vector<uint32_t> via_raw = collect_via_decode_next(str);

    CHECK_EQ(via_it.size(),  2u);
    CHECK_EQ(via_it[0], 0x41u);
    CHECK_EQ(via_it[1], 0x5956u);
    CHECK_EQ(via_raw.size(), 2u);
    CHECK_EQ(via_raw[0], 0x41u);
    CHECK_EQ(via_raw[1], 0x5956u);

    std::printf("  mixed 'A' + U+5956: iterator and raw both -> {0x41, 0x5956}: OK\n");
}

static void test_utf8_empty() {
    std::vector<uint32_t> via_it  = collect_via_iterator("");
    std::vector<uint32_t> via_raw = collect_via_decode_next("");
    CHECK_EQ(via_it.size(),  0u);
    CHECK_EQ(via_raw.size(), 0u);

    // decode_next_unicode_character on NUL string returns 0
    const char* p = "";
    uint32_t cp = Mortar::utf8::decode_next_unicode_character(&p);
    CHECK_EQ(cp, 0u);

    std::printf("  empty string: OK\n");
}

static void test_utf8_malformed() {
    // Lone continuation byte 0x80 -> 0xFFFD
    const char lone[] = { (char)0x80, '\0' };
    const char* p = lone;
    uint32_t cp = Mortar::utf8::decode_next_unicode_character(&p);
    CHECK_EQ(cp, 0xFFFDu);

    // Truncated 3-byte sequence: lead 0xE5 then NUL.
    // Binary returns 0 (not 0xFFFD) when a continuation byte is NUL.
    const char trunc[] = { (char)0xE5, '\0' };
    const char* p2 = trunc;
    uint32_t cp2 = Mortar::utf8::decode_next_unicode_character(&p2);
    CHECK_EQ(cp2, 0u);

    std::printf("  malformed: lone 0x80 -> 0xFFFD, truncated 3-byte (0xE5+NUL) -> 0: OK\n");
}

static void test_utf8_reset() {
    // Verify Reset() rewinds the iterator to the start.
    const char str[] = { 'A', 'B', '\0' };
    Mortar::Utf8StringIterator it(str);
    CHECK_EQ(it.m_CurrentCodepoint, 0x41u);
    it++;
    CHECK_EQ(it.m_CurrentCodepoint, 0x42u);
    it++;
    CHECK(it.IsEmpty());

    it.Reset();
    CHECK(!it.IsEmpty());
    CHECK_EQ(it.m_CurrentCodepoint, 0x41u);

    std::printf("  Reset() rewinds to first codepoint: OK\n");
}

// ---------------------------------------------------------------------------
// Layer 2: BakedStringBox recording guard (#216 mojibake regression)
//
// THE REGRESSION GUARD: BakedStringBox::Layout() must iterate the UTF-8 text
// using Utf8StringIterator (giving decoded codepoints) NOT by casting raw bytes.
//
// If someone reverts to "(unsigned char)byte -> GetGlyph", the string
// "A" + U+5956 (3 bytes 0xE5 0xA5 0x96) would produce 4 GetGlyph calls with
// {0x41, 0xE5, 0xA5, 0x96} instead of the correct {0x41, 0x5956}.
// The assertions below catch exactly that regression.
// ---------------------------------------------------------------------------

static Mortar::FontCacheObjectTTF* make_recording_font() {
    return new Mortar::FontCacheObjectTTF("stub.ttf", 9);
}

static void test_bakedstringbox_ascii_codepoints() {
    // Pure-ASCII "AB": GetGlyph must see only {0x41, 0x42}.
    // (Also verifies the test infrastructure works for the ASCII baseline.)
    Mortar::FontCacheObjectTTF* font = make_recording_font();
    Mortar::BakedStringBox box(font, 9.0f, 200, 30, (Mortar::ALIGNMENT_TYPE)0, 3, 3);

    box.SetText("AB");
    g_recorded_codepoints.clear();
    box.Update();  // triggers Layout() -> GetGlyph per codepoint

    bool saw_0x41 = false;
    bool saw_0x42 = false;
    bool saw_unexpected = false;
    for (size_t i = 0; i < g_recorded_codepoints.size(); ++i) {
        uint32_t cp = g_recorded_codepoints[i];
        if (cp == 0x41u)           { saw_0x41 = true;   continue; }
        if (cp == 0x42u)           { saw_0x42 = true;   continue; }
        if (cp == (uint32_t)' ')   continue;  // space probe is OK
        saw_unexpected = true;
        std::printf("  UNEXPECTED codepoint 0x%X in 'AB' test\n", (unsigned)cp);
    }
    CHECK(saw_0x41);
    CHECK(saw_0x42);
    CHECK(!saw_unexpected);

    delete font;
    std::printf("  ASCII 'AB': GetGlyph sees only {0x41,0x42}: OK\n");
}

static void test_bakedstringbox_mixed_utf8_codepoints() {
    // "A" + U+5956 (E5 A5 96): Layout must produce {0x41, 0x5956}, NOT {0x41, 0xE5, 0xA5, 0x96}.
    const char text[] = { 'A', (char)0xE5, (char)0xA5, (char)0x96, '\0' };

    Mortar::FontCacheObjectTTF* font = make_recording_font();
    Mortar::BakedStringBox box(font, 9.0f, 200, 30, (Mortar::ALIGNMENT_TYPE)0, 3, 3);

    box.SetText(text);
    g_recorded_codepoints.clear();
    box.Update();

    bool saw_0x41   = false;
    bool saw_0x5956 = false;
    bool saw_0xE5   = false;  // raw byte -- indicates byte iteration bug
    bool saw_0xA5   = false;
    bool saw_0x96   = false;

    for (size_t i = 0; i < g_recorded_codepoints.size(); ++i) {
        uint32_t cp = g_recorded_codepoints[i];
        if (cp == 0x41u)          { saw_0x41   = true; }
        if (cp == 0x5956u)        { saw_0x5956 = true; }
        if (cp == (uint32_t)' ')  continue;
        if (cp == 0xE5u) { saw_0xE5 = true; }
        if (cp == 0xA5u) { saw_0xA5 = true; }
        if (cp == 0x96u) { saw_0x96 = true; }
    }

    // THE REGRESSION GUARD assertions:
    if (!saw_0x5956) {
        std::printf("FAIL: GetGlyph never saw U+5956 (0x5956) -- byte iteration bug!\n");
        if (saw_0xE5 || saw_0xA5 || saw_0x96) {
            std::printf(
                "  (raw bytes 0xE5=%d 0xA5=%d 0x96=%d present -- confirms byte iteration)\n",
                (int)saw_0xE5, (int)saw_0xA5, (int)saw_0x96);
        }
        ::exit(1);
    }
    CHECK(!saw_0xE5);   // raw bytes must NOT appear
    CHECK(!saw_0xA5);
    CHECK(!saw_0x96);
    CHECK(saw_0x41);
    CHECK(saw_0x5956);

    delete font;
    std::printf("  'A'+U+5956: GetGlyph sees {0x41,0x5956} not raw bytes: OK\n");
}

static void test_bakedstringbox_two_cjk_codepoints() {
    // U+5956 (E5 A5 96) + U+8FDE (E8 BF 9E) -> {0x5956, 0x8FDE}
    // U+8FDE: 1000_1111_1101_1110 -> 4|6|6 = 1000|111111|011110 -> E8 BF 9E
    const char text[] = {
        (char)0xE5, (char)0xA5, (char)0x96,
        (char)0xE8, (char)0xBF, (char)0x9E,
        '\0'
    };

    Mortar::FontCacheObjectTTF* font = make_recording_font();
    Mortar::BakedStringBox box(font, 9.0f, 200, 30, (Mortar::ALIGNMENT_TYPE)0, 3, 3);

    box.SetText(text);
    g_recorded_codepoints.clear();
    box.Update();

    bool saw_0x5956  = false;
    bool saw_0x8FDE  = false;
    bool saw_raw_byte = false;

    for (size_t i = 0; i < g_recorded_codepoints.size(); ++i) {
        uint32_t cp = g_recorded_codepoints[i];
        if (cp == 0x5956u)        { saw_0x5956 = true;  continue; }
        if (cp == 0x8FDEu)        { saw_0x8FDE = true;  continue; }
        if (cp == (uint32_t)' ')  continue;
        // Any codepoint in [0x80..0xFF] that isn't a decoded value from
        // the test string indicates raw-byte iteration.
        if (cp >= 0x80u && cp <= 0xFFu) {
            saw_raw_byte = true;
            std::printf("  RAW BYTE in two-CJK test: 0x%X\n", (unsigned)cp);
        }
    }

    CHECK(saw_0x5956);
    CHECK(saw_0x8FDE);
    CHECK(!saw_raw_byte);

    delete font;
    std::printf("  two CJK U+5956+U+8FDE: only decoded codepoints passed: OK\n");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    std::printf("test_utf8_text: start\n");

    std::printf("\n[Layer 1: Utf8StringIterator + decode_next_unicode_character]\n");
    test_utf8_ascii();
    test_utf8_2byte();
    test_utf8_3byte_cjk();
    test_utf8_4byte();
    test_utf8_mixed();
    test_utf8_empty();
    test_utf8_malformed();
    test_utf8_reset();

    std::printf("\n[Layer 2: BakedStringBox codepoint recording guard (#216)]\n");
    test_bakedstringbox_ascii_codepoints();
    test_bakedstringbox_mixed_utf8_codepoints();
    test_bakedstringbox_two_cjk_codepoints();

    std::printf("\ntest_utf8_text: PASS\n");
    return 0;
}
