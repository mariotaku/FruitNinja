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
static void  (APIENTRYP s_glCompressedTexImage2D) (GLenum, GLint, GLenum,
                                                   GLsizei, GLsizei, GLint,
                                                   GLsizei, const void*) = nullptr;
static void  (APIENTRYP s_glGenBuffers)           (GLsizei, GLuint*) = nullptr;
static void  (APIENTRYP s_glDeleteBuffers)        (GLsizei, const GLuint*) = nullptr;
static void  (APIENTRYP s_glBindBuffer)           (GLenum, GLuint) = nullptr;
static void  (APIENTRYP s_glBufferData)           (GLenum, GLsizeiptr,
                                                   const void*, GLenum) = nullptr;
static void  (APIENTRYP s_glBufferSubData)        (GLenum, GLintptr, GLsizeiptr,
                                                   const void*) = nullptr;
// GL 2.0 shader entry points (Renderer 2D shader path).
static GLuint (APIENTRYP s_glCreateShader)        (GLenum) = nullptr;
static void  (APIENTRYP s_glShaderSource)         (GLuint, GLsizei,
                                                   const GLchar* const*,
                                                   const GLint*) = nullptr;
static void  (APIENTRYP s_glCompileShader)        (GLuint) = nullptr;
static void  (APIENTRYP s_glGetShaderiv)          (GLuint, GLenum, GLint*) = nullptr;
static void  (APIENTRYP s_glGetShaderInfoLog)     (GLuint, GLsizei, GLsizei*,
                                                   GLchar*) = nullptr;
static GLuint (APIENTRYP s_glCreateProgram)       (void) = nullptr;
static void  (APIENTRYP s_glAttachShader)         (GLuint, GLuint) = nullptr;
static void  (APIENTRYP s_glBindAttribLocation)   (GLuint, GLuint, const GLchar*) = nullptr;
static void  (APIENTRYP s_glLinkProgram)          (GLuint) = nullptr;
static void  (APIENTRYP s_glGetProgramiv)         (GLuint, GLenum, GLint*) = nullptr;
static void  (APIENTRYP s_glGetProgramInfoLog)    (GLuint, GLsizei, GLsizei*,
                                                   GLchar*) = nullptr;
static void  (APIENTRYP s_glUseProgram)           (GLuint) = nullptr;
static GLint (APIENTRYP s_glGetUniformLocation)   (GLuint, const GLchar*) = nullptr;
static void  (APIENTRYP s_glUniformMatrix4fv)     (GLint, GLsizei, GLboolean,
                                                   const GLfloat*) = nullptr;
static void  (APIENTRYP s_glUniform1i)            (GLint, GLint) = nullptr;
static void  (APIENTRYP s_glVertexAttribPointer)  (GLuint, GLint, GLenum,
                                                   GLboolean, GLsizei,
                                                   const void*) = nullptr;
static void  (APIENTRYP s_glVertexAttrib4f)       (GLuint, GLfloat, GLfloat,
                                                   GLfloat, GLfloat) = nullptr;
static void  (APIENTRYP s_glEnableVertexAttribArray)  (GLuint) = nullptr;
static void  (APIENTRYP s_glDisableVertexAttribArray) (GLuint) = nullptr;
static void  (APIENTRYP s_glDeleteShader)         (GLuint) = nullptr;
static void  (APIENTRYP s_glDeleteProgram)        (GLuint) = nullptr;

extern "C" {

void APIENTRY glActiveTexture(GLenum t) {
    if (s_glActiveTexture) s_glActiveTexture(t);
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
void APIENTRY glBufferSubData(GLenum target, GLintptr offset,
                              GLsizeiptr size, const void* data) {
    if (s_glBufferSubData) s_glBufferSubData(target, offset, size, data);
}

GLuint APIENTRY glCreateShader(GLenum type) {
    return s_glCreateShader ? s_glCreateShader(type) : 0;
}
void APIENTRY glShaderSource(GLuint shader, GLsizei count,
                             const GLchar* const* string, const GLint* length) {
    if (s_glShaderSource) s_glShaderSource(shader, count, string, length);
}
void APIENTRY glCompileShader(GLuint shader) {
    if (s_glCompileShader) s_glCompileShader(shader);
}
void APIENTRY glGetShaderiv(GLuint shader, GLenum pname, GLint* params) {
    if (s_glGetShaderiv) s_glGetShaderiv(shader, pname, params);
}
void APIENTRY glGetShaderInfoLog(GLuint shader, GLsizei bufSize,
                                 GLsizei* length, GLchar* infoLog) {
    if (s_glGetShaderInfoLog) s_glGetShaderInfoLog(shader, bufSize, length, infoLog);
}
GLuint APIENTRY glCreateProgram(void) {
    return s_glCreateProgram ? s_glCreateProgram() : 0;
}
void APIENTRY glAttachShader(GLuint program, GLuint shader) {
    if (s_glAttachShader) s_glAttachShader(program, shader);
}
void APIENTRY glBindAttribLocation(GLuint program, GLuint index, const GLchar* name) {
    if (s_glBindAttribLocation) s_glBindAttribLocation(program, index, name);
}
void APIENTRY glLinkProgram(GLuint program) {
    if (s_glLinkProgram) s_glLinkProgram(program);
}
void APIENTRY glGetProgramiv(GLuint program, GLenum pname, GLint* params) {
    if (s_glGetProgramiv) s_glGetProgramiv(program, pname, params);
}
void APIENTRY glGetProgramInfoLog(GLuint program, GLsizei bufSize,
                                  GLsizei* length, GLchar* infoLog) {
    if (s_glGetProgramInfoLog) s_glGetProgramInfoLog(program, bufSize, length, infoLog);
}
void APIENTRY glUseProgram(GLuint program) {
    if (s_glUseProgram) s_glUseProgram(program);
}
GLint APIENTRY glGetUniformLocation(GLuint program, const GLchar* name) {
    return s_glGetUniformLocation ? s_glGetUniformLocation(program, name) : -1;
}
void APIENTRY glUniformMatrix4fv(GLint location, GLsizei count,
                                 GLboolean transpose, const GLfloat* value) {
    if (s_glUniformMatrix4fv) s_glUniformMatrix4fv(location, count, transpose, value);
}
void APIENTRY glUniform1i(GLint location, GLint v0) {
    if (s_glUniform1i) s_glUniform1i(location, v0);
}
void APIENTRY glVertexAttribPointer(GLuint index, GLint size, GLenum type,
                                    GLboolean normalized, GLsizei stride,
                                    const void* pointer) {
    if (s_glVertexAttribPointer)
        s_glVertexAttribPointer(index, size, type, normalized, stride, pointer);
}
void APIENTRY glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y,
                               GLfloat z, GLfloat w) {
    if (s_glVertexAttrib4f) s_glVertexAttrib4f(index, x, y, z, w);
}
void APIENTRY glEnableVertexAttribArray(GLuint index) {
    if (s_glEnableVertexAttribArray) s_glEnableVertexAttribArray(index);
}
void APIENTRY glDisableVertexAttribArray(GLuint index) {
    if (s_glDisableVertexAttribArray) s_glDisableVertexAttribArray(index);
}
void APIENTRY glDeleteShader(GLuint shader) {
    if (s_glDeleteShader) s_glDeleteShader(shader);
}
void APIENTRY glDeleteProgram(GLuint program) {
    if (s_glDeleteProgram) s_glDeleteProgram(program);
}

} // extern "C"

bool gl_load_extensions_win32() {
    // C-style cast handles the function-pointer type in one step; the
    // destination static's declared type does the type-checking.
#define LOAD(name) \
    *(void**)&s_##name = (void*)SDL_GL_GetProcAddress(#name)
    LOAD(glActiveTexture);
    LOAD(glCompressedTexImage2D);
    LOAD(glGenBuffers);
    LOAD(glDeleteBuffers);
    LOAD(glBindBuffer);
    LOAD(glBufferData);
    LOAD(glBufferSubData);
    LOAD(glCreateShader);
    LOAD(glShaderSource);
    LOAD(glCompileShader);
    LOAD(glGetShaderiv);
    LOAD(glGetShaderInfoLog);
    LOAD(glCreateProgram);
    LOAD(glAttachShader);
    LOAD(glBindAttribLocation);
    LOAD(glLinkProgram);
    LOAD(glGetProgramiv);
    LOAD(glGetProgramInfoLog);
    LOAD(glUseProgram);
    LOAD(glGetUniformLocation);
    LOAD(glUniformMatrix4fv);
    LOAD(glUniform1i);
    LOAD(glVertexAttribPointer);
    LOAD(glVertexAttrib4f);
    LOAD(glEnableVertexAttribArray);
    LOAD(glDisableVertexAttribArray);
    LOAD(glDeleteShader);
    LOAD(glDeleteProgram);
#undef LOAD
    return true;
}

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#endif // _WIN32
