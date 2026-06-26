#ifndef MORTAR_GL_COMPAT_H
#define MORTAR_GL_COMPAT_H

// Picks the right OS GL / GLES header based on the FRUIT_GL_API CMake
// variable. Keep this include-only — all the declarations we actually
// link against live in gl_funcs.h (loaded via SDL_GL_GetProcAddress).
//
// Why not just include everywhere? The ES1/ES2/Desktop-GL headers
// collide on enum values and function typedefs when mixed. This file
// is the single point of truth.

#if defined(__bada__) || defined(FN_GL_STUB)
    // asm-verify cross-build (__bada__) or unit-test stub (FN_GL_STUB):
    // no system GL headers available. Provide a minimal stub of just the
    // GL types + constants the port code touches. sizeof static_asserts
    // are guarded #ifdef __bada__ only (not FN_GL_STUB) so they don't
    // fire on the x64 host where struct sizes legitimately differ.
    typedef unsigned int  GLenum;
    typedef unsigned int  GLbitfield;
    typedef unsigned int  GLuint;
    typedef int           GLint;
    typedef int           GLsizei;
    typedef unsigned char GLboolean;
    typedef signed char   GLbyte;
    typedef short         GLshort;
    typedef unsigned char GLubyte;
    typedef unsigned short GLushort;
    typedef float         GLfloat;
    typedef float         GLclampf;
    typedef double        GLdouble;
    typedef double        GLclampd;
    typedef void          GLvoid;
    typedef char          GLchar;
    typedef long          GLintptr;
    typedef long          GLsizeiptr;
    #define APIENTRY
    #define APIENTRYP *
    #define GLAPI extern
    // Constants the port code references at compile time.
    #define GL_FALSE 0
    #define GL_TRUE 1
    #define GL_TRIANGLES 4
    #define GL_TRIANGLE_STRIP 5
    #define GL_TEXTURE_2D 0x0DE1
    #define GL_TEXTURE0 0x84C0
    #define GL_BLEND 0x0BE2
    #define GL_DEPTH_TEST 0x0B71
    #define GL_CULL_FACE 0x0B44
    #define GL_LEQUAL 0x0203
    #define GL_LESS 0x0201
    #define GL_SRC_ALPHA 0x0302
    #define GL_ONE_MINUS_SRC_ALPHA 0x0303
    #define GL_ONE 1
    #define GL_RGB 0x1907
    #define GL_RGBA 0x1908
    #define GL_UNSIGNED_BYTE 0x1401
    #define GL_UNSIGNED_SHORT 0x1403
    #define GL_FLOAT 0x1406
    #define GL_NEAREST 0x2600
    #define GL_LINEAR 0x2601
    #define GL_TEXTURE_MAG_FILTER 0x2800
    #define GL_TEXTURE_MIN_FILTER 0x2801
    #define GL_TEXTURE_WRAP_S 0x2802
    #define GL_TEXTURE_WRAP_T 0x2803
    #define GL_CLAMP_TO_EDGE 0x812F
    #define GL_REPEAT 0x2901
    #define GL_COLOR_BUFFER_BIT 0x4000
    #define GL_DEPTH_BUFFER_BIT 0x0100
    #define GL_ARRAY_BUFFER 0x8892
    #define GL_ELEMENT_ARRAY_BUFFER 0x8893
    #define GL_STATIC_DRAW 0x88E4
    #define GL_SCISSOR_TEST 0x0C11
    #define GL_UNPACK_ALIGNMENT 0x0CF5
    #define GL_VENDOR 0x1F00
    #define GL_RENDERER 0x1F01
    #define GL_VERSION 0x1F02
    #define GL_NO_ERROR 0
    #define GL_MODELVIEW 0x1700
    #define GL_PROJECTION 0x1701
    #define GL_TEXTURE_MATRIX 0x0BA8
    #define GL_VERTEX_ARRAY 0x8074
    #define GL_NORMAL_ARRAY 0x8075
    #define GL_COLOR_ARRAY 0x8076
    #define GL_TEXTURE_COORD_ARRAY 0x8078
    #define GL_TEXTURE_ENV 0x2300
    #define GL_TEXTURE_ENV_MODE 0x2200
    #define GL_MODULATE 0x2100
    #define GL_REPLACE 0x1E01
    // GL_COMBINE texenv constants (GL 1.3 / GL ES 1.1 extension).
    #define GL_COMBINE              0x8570
    #define GL_COMBINE_RGB          0x8571
    #define GL_SRC0_RGB             0x8580
    #define GL_PRIMARY_COLOR        0x8577
    #define GL_LIGHTING 0x0B50
    #define GL_LIGHT0 0x4000
    #define GL_AMBIENT 0x1200
    #define GL_DIFFUSE 0x1201
    #define GL_SPECULAR 0x1202
    #define GL_EMISSION 0x1600
    #define GL_POSITION 0x1203
    #define GL_SMOOTH 0x1D01
    #define GL_FRONT_AND_BACK 0x0408
    #define GL_LINE 0x1B01
    #define GL_FILL 0x1B02
    #define GL_UNSIGNED_SHORT_4_4_4_4 0x8033
    #define GL_UNSIGNED_SHORT_5_5_5_1 0x8034
    #define GL_UNSIGNED_SHORT_5_6_5  0x8363
    // GL entry points the port references. These don't have to link in
    // the cross-build (no link step happens) -- declarations only.
    extern "C" {
        const GLubyte* glGetString(GLenum);
        GLenum glGetError(void);
        void glViewport(GLint, GLint, GLsizei, GLsizei);
        void glClearColor(GLfloat, GLfloat, GLfloat, GLfloat);
        void glClear(GLbitfield);
        void glEnable(GLenum);
        void glDisable(GLenum);
        void glBlendFunc(GLenum, GLenum);
        void glScissor(GLint, GLint, GLsizei, GLsizei);
        void glPixelStorei(GLenum, GLint);
        void glGenTextures(GLsizei, GLuint*);
        void glDeleteTextures(GLsizei, const GLuint*);
        void glBindTexture(GLenum, GLuint);
        void glTexParameteri(GLenum, GLenum, GLint);
        void glTexImage2D(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
        void glTexSubImage2D(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*);
        void glCompressedTexImage2D(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void*);
        void glActiveTexture(GLenum);
        void glDrawArrays(GLenum, GLint, GLsizei);
        void glGenBuffers(GLsizei, GLuint*);
        void glDeleteBuffers(GLsizei, const GLuint*);
        void glBindBuffer(GLenum, GLuint);
        void glBufferData(GLenum, GLsizeiptr, const void*, GLenum);
        void glDrawElements(GLenum, GLsizei, GLenum, const void*);
        void glDepthFunc(GLenum);
        void glDepthMask(GLboolean);
        void glClearDepthf(GLclampf);
        void glMatrixMode(GLenum);
        void glPushMatrix(void);
        void glPopMatrix(void);
        void glLoadMatrixf(const GLfloat*);
        void glMultMatrixf(const GLfloat*);
        void glLoadIdentity(void);
        void glFrustumf(GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat);
        void glEnableClientState(GLenum);
        void glDisableClientState(GLenum);
        void glVertexPointer(GLint, GLenum, GLsizei, const void*);
        void glNormalPointer(GLenum, GLsizei, const void*);
        void glColorPointer(GLint, GLenum, GLsizei, const void*);
        void glTexCoordPointer(GLint, GLenum, GLsizei, const void*);
        void glClientActiveTexture(GLenum);
        void glColor4ub(GLubyte, GLubyte, GLubyte, GLubyte);
        void glMaterialfv(GLenum, GLenum, const GLfloat*);
        void glLightfv(GLenum, GLenum, const GLfloat*);
        void glShadeModel(GLenum);
        void glTexEnvf(GLenum, GLenum, GLfloat);
        void glTexEnvi(GLenum, GLenum, GLint);
        void glPolygonMode(GLenum, GLenum);
    }
#elif defined(FRUIT_GL_API_EMSCRIPTEN)
    // Port specific: Emscripten LEGACY_GL_EMULATION -- fixed-function over WebGL.
    // The spike (tmp/glspike/spike.cpp) confirmed this header combination works:
    // include the GLES2 header for GL types/constants, then declare the
    // fixed-function entry points that the emcc FFP shim provides at link time.
    // DO NOT include <GL/gl.h> here -- it conflicts with GLES2 types under emcc.
    #include <GLES2/gl2.h>
    // FFP enum values absent from GLES2 headers (provided by emcc shim at link time).
    #define GL_MODELVIEW                0x1700
    #define GL_PROJECTION               0x1701
    #define GL_TEXTURE_MATRIX           0x1702
    #define GL_MATRIX_MODE              0x0BA0
    #define GL_LIGHTING                 0x0B50
    #define GL_LIGHT0                   0x4000
    #define GL_LIGHT_MODEL_AMBIENT      0x0B53
    #define GL_NORMALIZE                0x0BA1
    #define GL_SMOOTH                   0x1D01
    #define GL_FLAT                     0x1D00
    #define GL_SHADE_MODEL              0x0B54
    #define GL_AMBIENT                  0x1200
    #define GL_DIFFUSE                  0x1201
    #define GL_SPECULAR                 0x1202
    #define GL_EMISSION                 0x1600
    #define GL_POSITION                 0x1203
    #define GL_SHININESS                0x1601
    #define GL_FRONT                    0x0404
    #define GL_BACK                     0x0405
    #define GL_FRONT_AND_BACK           0x0408
    #define GL_TEXTURE_ENV              0x2300
    #define GL_TEXTURE_ENV_MODE         0x2200
    #define GL_MODULATE                 0x2100
    #define GL_REPLACE                  0x1E01
    #define GL_VERTEX_ARRAY             0x8074
    #define GL_NORMAL_ARRAY             0x8075
    #define GL_COLOR_ARRAY              0x8076
    #define GL_TEXTURE_COORD_ARRAY      0x8078
    #define GL_QUADS                    0x0007
    #define GL_LINE                     0x1B01
    #define GL_FILL                     0x1B02
    #define GL_UNSIGNED_SHORT_4_4_4_4   0x8033
    #define GL_UNSIGNED_SHORT_5_5_5_1   0x8034
    #define GL_UNSIGNED_SHORT_5_6_5     0x8363
    // FFP entry points provided by the emcc LEGACY_GL_EMULATION shim.
    // Declared extern "C" so the C++ name-mangling doesn't interfere.
    extern "C" {
        void glMatrixMode(GLenum mode);
        void glPushMatrix(void);
        void glPopMatrix(void);
        void glLoadIdentity(void);
        void glLoadMatrixf(const GLfloat* m);
        void glMultMatrixf(const GLfloat* m);
        void glFrustumf(GLfloat l, GLfloat r, GLfloat b,
                        GLfloat t, GLfloat n, GLfloat f);
        void glShadeModel(GLenum mode);
        void glLightfv(GLenum light, GLenum pname, const GLfloat* params);
        void glMaterialfv(GLenum face, GLenum pname, const GLfloat* params);
        void glLightModelfv(GLenum pname, const GLfloat* params);
        void glTexEnvf(GLenum target, GLenum pname, GLfloat param);
        void glTexEnvi(GLenum target, GLenum pname, GLint param);
        void glEnableClientState(GLenum array);
        void glDisableClientState(GLenum array);
        void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* ptr);
        void glNormalPointer(GLenum type, GLsizei stride, const GLvoid* ptr);
        void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* ptr);
        void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* ptr);
        void glClientActiveTexture(GLenum texture);
        void glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a);
        void glClearDepthf(GLclampf depth);
        // glFrustum (double variant) -- used by gl_funcsSDL glFrustumf shim.
        // Under LEGACY_GL_EMULATION glFrustumf IS provided directly, but the
        // shim in gl_funcsSDL.cpp is compiled unconditionally for GL_COMPAT;
        // under EMSCRIPTEN path gl_funcsSDL's shim is excluded by the #if guard.
    }
#elif defined(FRUIT_GL_API_ES1)
    // webOS / embedded Linux. libGLESv1_CM + libEGL.
    #include <GLES/gl.h>
    #include <GLES/glext.h>
#elif defined(FRUIT_GL_API_GL_COMPAT)
    // Windows / macOS / Linux desktop. Mesa llvmpipe provides a
    // compatibility profile with full fixed-function support.
    //
    // SDL2 ships a redistributed Khronos <GL/gl.h> + <GL/glext.h> via
    // <SDL_opengl.h>. Use that on every desktop target so the build
    // works with toolchains (MSVC, mingw-w64) whose Windows SDKs ship
    // GL 1.1-only headers without glext.
    #if defined(_WIN32)
        // Tame <windows.h>: SDL_opengl.h pulls it in transitively, and
        // it pollutes the global namespace with NEAR / FAR / min / max /
        // CreateFont / SendMessage / etc. macros that wreck downstream
        // templates. Set the lean-and-mean flags BEFORE SDL_opengl.h
        // includes <windows.h>, and undef the worst macros after.
        #ifndef WIN32_LEAN_AND_MEAN
            #define WIN32_LEAN_AND_MEAN
        #endif
        #ifndef NOMINMAX
            #define NOMINMAX
        #endif
        #ifndef NOGDI
            #define NOGDI
        #endif
        #ifndef NOUSER
            #define NOUSER
        #endif
    #endif
    #include <SDL_opengl.h>
    #if defined(_WIN32)
        // Belt-and-braces: SDL pulls <windows.h> indirectly, so undef
        // the GDI/USER/macro identifiers that survived the lean flags.
        #undef SendMessage
        #undef SendMessageA
        #undef SendMessageW
        #undef CreateFont
        #undef CreateFontA
        #undef CreateFontW
        #undef GetCurrentTime
        #undef DrawText
        #undef DrawTextA
        #undef DrawTextW
        #undef PlaySound
        #undef PlaySoundA
        #undef PlaySoundW
        #undef LoadImage
        #undef LoadImageA
        #undef LoadImageW
        #undef GetObject
        #undef GetObjectA
        #undef GetObjectW
        #undef near
        #undef far
    #endif
#else
    // Default: OpenGL ES 2.0 (current shader-based port).
    #include <GLES2/gl2.h>
    #include <GLES2/gl2ext.h>
#endif

#endif
