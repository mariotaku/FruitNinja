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
