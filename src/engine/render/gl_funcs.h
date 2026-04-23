#ifndef MORTAR_GL_FUNCS_H
#define MORTAR_GL_FUNCS_H

#include <SDL.h>
#include <cstdint>
#include <cstdio>

// GL types
typedef unsigned int GLenum;
typedef unsigned int GLbitfield;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLboolean;
typedef signed char GLbyte;
typedef short GLshort;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned long GLulong;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLdouble;
typedef double GLclampd;
typedef void GLvoid;
typedef char GLchar;
typedef intptr_t GLintptr;
typedef intptr_t GLsizeiptr;

// GL constants
#define GL_FALSE                 0
#define GL_TRUE                  1
#define GL_NO_ERROR              0
#define GL_TRIANGLES             0x0004
#define GL_TRIANGLE_STRIP        0x0005
#define GL_BLEND                 0x0BE2
#define GL_SRC_ALPHA             0x0302
#define GL_ONE_MINUS_SRC_ALPHA   0x0303
#define GL_ONE                   0x0001
#define GL_TEXTURE_2D            0x0DE1
#define GL_UNSIGNED_BYTE         0x1401
#define GL_UNSIGNED_SHORT        0x1403
#define GL_FLOAT                 0x1406
#define GL_RGB                   0x1907
#define GL_RGBA                  0x1908
#define GL_VENDOR                0x1F00
#define GL_RENDERER              0x1F01
#define GL_VERSION               0x1F02
#define GL_NEAREST               0x2600
#define GL_LINEAR                0x2601
#define GL_TEXTURE_MAG_FILTER    0x2800
#define GL_TEXTURE_MIN_FILTER    0x2801
#define GL_TEXTURE_WRAP_S        0x2802
#define GL_TEXTURE_WRAP_T        0x2803
#define GL_CLAMP_TO_EDGE         0x812F
#define GL_REPEAT                0x2901
#define GL_TEXTURE0              0x84C0
#define GL_FRAGMENT_SHADER       0x8B30
#define GL_VERTEX_SHADER         0x8B31
#define GL_COMPILE_STATUS        0x8B81
#define GL_LINK_STATUS           0x8B82
#define GL_COLOR_BUFFER_BIT      0x4000
#define GL_DEPTH_BUFFER_BIT      0x0100
#define GL_DEPTH_TEST            0x0B71
#define GL_LESS                  0x0201
#define GL_LEQUAL                0x0203
#define GL_ARRAY_BUFFER          0x8892
#define GL_ELEMENT_ARRAY_BUFFER  0x8893
#define GL_STATIC_DRAW           0x88E4
#define GL_SCISSOR_TEST          0x0C11
#define GL_UNPACK_ALIGNMENT      0x0CF5
#define GL_CULL_FACE             0x0B44
#define GL_UNSIGNED_SHORT_4_4_4_4 0x8033
#define GL_UNSIGNED_SHORT_5_5_5_1 0x8034
#define GL_UNSIGNED_SHORT_5_6_5  0x8363

// Fixed-function constants (used by GL_COMPAT / ES1 backends).
// Harmless to always define — the values are universal across ES 1.x
// and desktop compat profiles.
#define GL_MODELVIEW             0x1700
#define GL_PROJECTION            0x1701
#define GL_VERTEX_ARRAY          0x8074
#define GL_NORMAL_ARRAY          0x8075
#define GL_COLOR_ARRAY           0x8076
#define GL_TEXTURE_COORD_ARRAY   0x8078
#define GL_TEXTURE_ENV           0x2300
#define GL_TEXTURE_ENV_MODE      0x2200
#define GL_MODULATE              0x2100
#define GL_REPLACE               0x1E01
#define GL_LIGHTING              0x0B50
#define GL_LIGHT0                0x4000
#define GL_AMBIENT               0x1200
#define GL_DIFFUSE               0x1201
#define GL_SPECULAR              0x1202
#define GL_EMISSION              0x1600
#define GL_POSITION              0x1203
#define GL_SMOOTH                0x1D01

// Function pointer types and declarations
#define GL_FUNC(ret, name, ...) typedef ret (*PFN_##name)(__VA_ARGS__); extern PFN_##name name;

GL_FUNC(const GLubyte*, glGetString, GLenum)
GL_FUNC(GLenum, glGetError, void)
GL_FUNC(void, glViewport, GLint, GLint, GLsizei, GLsizei)
GL_FUNC(void, glClearColor, GLfloat, GLfloat, GLfloat, GLfloat)
GL_FUNC(void, glClear, GLbitfield)
GL_FUNC(void, glEnable, GLenum)
GL_FUNC(void, glDisable, GLenum)
GL_FUNC(void, glBlendFunc, GLenum, GLenum)
GL_FUNC(void, glScissor, GLint, GLint, GLsizei, GLsizei)
GL_FUNC(void, glPixelStorei, GLenum, GLint)
GL_FUNC(void, glGenTextures, GLsizei, GLuint*)
GL_FUNC(void, glDeleteTextures, GLsizei, const GLuint*)
GL_FUNC(void, glBindTexture, GLenum, GLuint)
GL_FUNC(void, glTexParameteri, GLenum, GLenum, GLint)
GL_FUNC(void, glTexImage2D, GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)
GL_FUNC(void, glCompressedTexImage2D, GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void*)
GL_FUNC(void, glActiveTexture, GLenum)
GL_FUNC(void, glDrawArrays, GLenum, GLint, GLsizei)
GL_FUNC(void, glGenBuffers, GLsizei, GLuint*)
GL_FUNC(void, glDeleteBuffers, GLsizei, const GLuint*)
GL_FUNC(void, glBindBuffer, GLenum, GLuint)
GL_FUNC(void, glBufferData, GLenum, GLsizeiptr, const void*, GLenum)
GL_FUNC(void, glDrawElements, GLenum, GLsizei, GLenum, const void*)
GL_FUNC(void, glDepthFunc, GLenum)
GL_FUNC(void, glDepthMask, GLboolean)

// Fixed-function pipeline (ES 1.x / desktop-GL compat). These are the
// only draw-path entry points — no shader/attribute symbols exist in
// this build.
GL_FUNC(void, glMatrixMode, GLenum)
GL_FUNC(void, glPushMatrix, void)
GL_FUNC(void, glPopMatrix, void)
GL_FUNC(void, glLoadMatrixf, const GLfloat*)
GL_FUNC(void, glMultMatrixf, const GLfloat*)
GL_FUNC(void, glLoadIdentity, void)
GL_FUNC(void, glEnableClientState, GLenum)
GL_FUNC(void, glDisableClientState, GLenum)
GL_FUNC(void, glVertexPointer, GLint, GLenum, GLsizei, const void*)
GL_FUNC(void, glNormalPointer, GLenum, GLsizei, const void*)
GL_FUNC(void, glColorPointer, GLint, GLenum, GLsizei, const void*)
GL_FUNC(void, glTexCoordPointer, GLint, GLenum, GLsizei, const void*)
GL_FUNC(void, glClientActiveTexture, GLenum)
GL_FUNC(void, glColor4f, GLfloat, GLfloat, GLfloat, GLfloat)
GL_FUNC(void, glMaterialfv, GLenum, GLenum, const GLfloat*)
GL_FUNC(void, glLightfv, GLenum, GLenum, const GLfloat*)
GL_FUNC(void, glShadeModel, GLenum)
GL_FUNC(void, glTexEnvi, GLenum, GLenum, GLint)

#undef GL_FUNC

// Load all GL function pointers via SDL_GL_GetProcAddress. Call after context creation.
bool gl_load_functions();

#endif
