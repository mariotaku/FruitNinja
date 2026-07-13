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
    #define GL_FRONT 0x0404
    #define GL_BACK 0x0405
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
    #define GL_DYNAMIC_DRAW 0x88E8
    // GL 2.0 shader objects (Renderer 2D shader path).
    #define GL_FRAGMENT_SHADER 0x8B30
    #define GL_VERTEX_SHADER 0x8B31
    #define GL_COMPILE_STATUS 0x8B81
    #define GL_LINK_STATUS 0x8B82
    #define GL_INFO_LOG_LENGTH 0x8B84
    #define GL_SCISSOR_TEST 0x0C11
    #define GL_UNPACK_ALIGNMENT 0x0CF5
    #define GL_VENDOR 0x1F00
    #define GL_RENDERER 0x1F01
    #define GL_VERSION 0x1F02
    #define GL_NO_ERROR 0
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
        void glCullFace(GLenum);
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
        void glPolygonMode(GLenum, GLenum);
        // GL 2.0 shader entry points (Renderer 2D shader path).
        GLuint glCreateShader(GLenum);
        void glShaderSource(GLuint, GLsizei, const GLchar* const*, const GLint*);
        void glCompileShader(GLuint);
        void glGetShaderiv(GLuint, GLenum, GLint*);
        void glGetShaderInfoLog(GLuint, GLsizei, GLsizei*, GLchar*);
        GLuint glCreateProgram(void);
        void glAttachShader(GLuint, GLuint);
        void glBindAttribLocation(GLuint, GLuint, const GLchar*);
        void glLinkProgram(GLuint);
        void glGetProgramiv(GLuint, GLenum, GLint*);
        void glGetProgramInfoLog(GLuint, GLsizei, GLsizei*, GLchar*);
        void glUseProgram(GLuint);
        GLint glGetUniformLocation(GLuint, const GLchar*);
        void glUniformMatrix4fv(GLint, GLsizei, GLboolean, const GLfloat*);
        void glUniform1i(GLint, GLint);
        void glVertexAttribPointer(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
        void glVertexAttrib4f(GLuint, GLfloat, GLfloat, GLfloat, GLfloat);
        void glEnableVertexAttribArray(GLuint);
        void glDisableVertexAttribArray(GLuint);
        void glDeleteShader(GLuint);
        void glDeleteProgram(GLuint);
    }
#elif defined(FRUIT_GL_API_EMSCRIPTEN)
    // Port specific: Emscripten LEGACY_GL_EMULATION -- fixed-function over WebGL.
    // The spike (tmp/glspike/spike.cpp) confirmed this header combination works:
    // include the GLES2 header for GL types/constants, then declare the
    // fixed-function entry points that the emcc FFP shim provides at link time.
    // DO NOT include <GL/gl.h> here -- it conflicts with GLES2 types under emcc.
    #include <GLES2/gl2.h>
    // FFP enum values absent from GLES2 headers (provided by emcc shim at link time).
    #define GL_FRONT                    0x0404
    #define GL_BACK                     0x0405
    #define GL_FRONT_AND_BACK           0x0408
    #define GL_LINE                     0x1B01
    #define GL_FILL                     0x1B02
    #define GL_UNSIGNED_SHORT_4_4_4_4   0x8033
    #define GL_UNSIGNED_SHORT_5_5_5_1   0x8034
    #define GL_UNSIGNED_SHORT_5_6_5     0x8363
    // Declared extern "C" so the C++ name-mangling doesn't interfere.
    extern "C" {
        void glClearDepthf(GLclampf depth);
    }
#elif defined(FRUIT_GL_API_ES1)
    // webOS / embedded Linux. libGLESv1_CM + libEGL.
    #include <GLES/gl.h>
    #include <GLES/glext.h>
    // Port specific: GL 2.0 shader entry points used by the Renderer 2D
    // shader path (ShaderProgram.cpp / Renderer.cpp). <GLES/gl.h> is ES 1.1
    // only, so declare them here to keep the TUs compiling. NOTE:
    // libGLESv1_CM does NOT export these -- the ES1 backend cannot run the
    // GLES2 2D path; it must move to an ES2 context (+libGLESv2) as part of
    // the GLES2 migration, and will fail at LINK time until it does.
    #ifndef GL_FRAGMENT_SHADER
    #define GL_FRAGMENT_SHADER 0x8B30
    #define GL_VERTEX_SHADER 0x8B31
    #define GL_COMPILE_STATUS 0x8B81
    #define GL_LINK_STATUS 0x8B82
    #define GL_INFO_LOG_LENGTH 0x8B84
    #endif
    #ifndef GL_DYNAMIC_DRAW
    #define GL_DYNAMIC_DRAW 0x88E8
    #endif
    extern "C" {
        GLuint glCreateShader(GLenum);
        void glShaderSource(GLuint, GLsizei, const char* const*, const GLint*);
        void glCompileShader(GLuint);
        void glGetShaderiv(GLuint, GLenum, GLint*);
        void glGetShaderInfoLog(GLuint, GLsizei, GLsizei*, char*);
        GLuint glCreateProgram(void);
        void glAttachShader(GLuint, GLuint);
        void glBindAttribLocation(GLuint, GLuint, const char*);
        void glLinkProgram(GLuint);
        void glGetProgramiv(GLuint, GLenum, GLint*);
        void glGetProgramInfoLog(GLuint, GLsizei, GLsizei*, char*);
        void glUseProgram(GLuint);
        GLint glGetUniformLocation(GLuint, const char*);
        void glUniformMatrix4fv(GLint, GLsizei, GLboolean, const GLfloat*);
        void glUniform1i(GLint, GLint);
        void glVertexAttribPointer(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
        void glVertexAttrib4f(GLuint, GLfloat, GLfloat, GLfloat, GLfloat);
        void glEnableVertexAttribArray(GLuint);
        void glDisableVertexAttribArray(GLuint);
        void glDeleteShader(GLuint);
        void glDeleteProgram(GLuint);
    }
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
