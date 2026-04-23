#ifndef MORTAR_GL_COMPAT_H
#define MORTAR_GL_COMPAT_H

// Picks the right OS GL / GLES header based on the FRUIT_GL_API CMake
// variable. Keep this include-only — all the declarations we actually
// link against live in gl_funcs.h (loaded via SDL_GL_GetProcAddress).
//
// Why not just include everywhere? The ES1/ES2/Desktop-GL headers
// collide on enum values and function typedefs when mixed. This file
// is the single point of truth.

#if defined(FRUIT_GL_API_ES1)
    // webOS / embedded Linux. libGLESv1_CM + libEGL.
    #include <GLES/gl.h>
    #include <GLES/glext.h>
#elif defined(FRUIT_GL_API_GL_COMPAT)
    // Windows / macOS / Linux desktop. Mesa llvmpipe provides a
    // compatibility profile with full fixed-function support.
    #if defined(_WIN32)
        #include <windows.h>
    #endif
    #include <GL/gl.h>
    #include <GL/glext.h>
#else
    // Default: OpenGL ES 2.0 (current shader-based port).
    #include <GLES2/gl2.h>
    #include <GLES2/gl2ext.h>
#endif

#endif
