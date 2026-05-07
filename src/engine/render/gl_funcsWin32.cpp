// Windows-only wrapper TU for GL 1.2+ extensions.
//
// On Linux libGL.so exports everything statically; on webOS libGLESv1_CM
// exports the ES 1.x set; both resolve through the normal linker. Windows
// is the odd one: opengl32.lib only exports GL 1.1, and 1.2+ entry points
// must be fetched via wglGetProcAddress (wrapped here through SDL).
//
// We provide real C symbols for the 1.2+ functions we use, so call sites
// stay portable (they just call glActiveTexture(...) etc.). The function
// pointers are file-static; the wrappers are extern "C" globals.

#if defined(_WIN32)

#include "render/gl_funcs.h"
#include <SDL.h>

// SDL_opengl.h declares these GL entry points with __declspec(dllimport)
// (the standard Windows convention, even for entry points that aren't in
// opengl32.dll). We DEFINE them here -- redefining a dllimport-declared
// function emits MSVC C4273 ("inconsistent dll linkage"). The link picks
// our definition (opengl32.dll doesn't export 1.2+), so the warning is
// noise; suppress it for this TU.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4273)
#endif

// Type-erased function-pointer slots (avoid PFN* typedef compatibility
// games across SDL_opengl.h vs SDL_opengl_glext.h vs Khronos glext.h).
static void  (APIENTRYP s_glActiveTexture)        (GLenum) = nullptr;
static void  (APIENTRYP s_glClientActiveTexture)  (GLenum) = nullptr;
static void  (APIENTRYP s_glCompressedTexImage2D) (GLenum, GLint, GLenum,
                                                   GLsizei, GLsizei, GLint,
                                                   GLsizei, const void*) = nullptr;
static void  (APIENTRYP s_glGenBuffers)           (GLsizei, GLuint*) = nullptr;
static void  (APIENTRYP s_glDeleteBuffers)        (GLsizei, const GLuint*) = nullptr;
static void  (APIENTRYP s_glBindBuffer)           (GLenum, GLuint) = nullptr;
static void  (APIENTRYP s_glBufferData)           (GLenum, GLsizeiptr,
                                                   const void*, GLenum) = nullptr;

extern "C" {

void APIENTRY glActiveTexture(GLenum t) {
    if (s_glActiveTexture) s_glActiveTexture(t);
}
void APIENTRY glClientActiveTexture(GLenum t) {
    if (s_glClientActiveTexture) s_glClientActiveTexture(t);
}
void APIENTRY glCompressedTexImage2D(GLenum target, GLint level,
                                     GLenum internalformat,
                                     GLsizei width, GLsizei height,
                                     GLint border, GLsizei imageSize,
                                     const void* data) {
    if (s_glCompressedTexImage2D)
        s_glCompressedTexImage2D(target, level, internalformat,
                                 width, height, border, imageSize, data);
}
void APIENTRY glGenBuffers(GLsizei n, GLuint* b) {
    if (s_glGenBuffers) s_glGenBuffers(n, b);
}
void APIENTRY glDeleteBuffers(GLsizei n, const GLuint* b) {
    if (s_glDeleteBuffers) s_glDeleteBuffers(n, b);
}
void APIENTRY glBindBuffer(GLenum target, GLuint b) {
    if (s_glBindBuffer) s_glBindBuffer(target, b);
}
void APIENTRY glBufferData(GLenum target, GLsizeiptr size,
                           const void* data, GLenum usage) {
    if (s_glBufferData) s_glBufferData(target, size, data, usage);
}

} // extern "C"

bool gl_load_extensions_win32() {
    // C-style cast handles the function-pointer type in one step; the
    // destination static's declared type does the type-checking.
#define LOAD(name) \
    *(void**)&s_##name = (void*)SDL_GL_GetProcAddress(#name)
    LOAD(glActiveTexture);
    LOAD(glClientActiveTexture);
    LOAD(glCompressedTexImage2D);
    LOAD(glGenBuffers);
    LOAD(glDeleteBuffers);
    LOAD(glBindBuffer);
    LOAD(glBufferData);
#undef LOAD
    return true;
}

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#endif // _WIN32
