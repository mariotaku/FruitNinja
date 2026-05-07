#ifndef MORTAR_GL_FUNCS_H
#define MORTAR_GL_FUNCS_H

// GL header dispatch + small runtime helpers.
//
// History: this header used to declare every GL entry point as a
// function-pointer variable (PFN_*) loaded at runtime via
// SDL_GL_GetProcAddress. That scheme produced an STT_FUNC vs STT_OBJECT
// link-time symbol-type collision on platforms where the OS GL library
// is statically linked (Linux libGL.so via SDL2's transitive deps, libGLESv1_CM
// on webOS, etc.) — every variable redefined a libGL exported function.
//
// Now: include the OS GL header directly (via gl_compat.h) so functions
// resolve through normal linker symbol resolution. CMake links the right
// system library per FRUIT_GL_API. GL 1.2+ extensions on Windows
// (opengl32.lib only exports 1.1) are provided by gl_funcsWin32.cpp,
// which defines real C functions that lazy-load via SDL_GL_GetProcAddress.

#define GL_GLEXT_PROTOTYPES 1
#include "render/gl_compat.h"

#include <cstdint>
#include <cstdio>

// glClearDepthf / glFrustumf are ES1-only entry points absent from desktop
// GL headers (and not always exported by libGL even when the driver advertises
// ARB_ES2_compatibility). gl_funcsSDL.cpp provides real wrappers for them
// on every GL_COMPAT build -- forwarded to glFrustum / glClearDepth (GL 1.0
// baseline, always exported).
//
// SDL_opengl.h on Windows declares these with __declspec(dllimport); we
// re-declare them as plain extern "C" to match our definitions. C4273
// ("inconsistent dll linkage") is suppressed for the small region.
#if defined(FRUIT_GL_API_GL_COMPAT)
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4273)
#endif
extern "C" {
    void APIENTRY glClearDepthf(GLclampf depth);
    void APIENTRY glFrustumf(GLfloat l, GLfloat r, GLfloat b,
                             GLfloat t, GLfloat n, GLfloat f);
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#endif

// Optional entry point — desktop GL only, may not exist under GLES.
// Used by the F2 wireframe debug toggle. On Windows MSYS2 / MSVC builds
// this is provided by gl_funcsWin32.cpp via wglGetProcAddress; on Linux
// it's exported directly by libGL when running on Mesa or proprietary
// drivers; on GLES it's truly absent and the wrapper returns without
// doing anything.
//
// (Declared via <GL/glext.h> on desktop GL; declared as a real wrapper
// by gl_funcsWin32.cpp when targeting Windows.)

// Optional GL types missing on some legacy GL headers (Microsoft's
// <GL/gl.h> ships GL 1.1 only — no GLsizeiptr / GLintptr / GLclampf
// for ES). gl_compat.h pulls <GL/glext.h> on desktop which fills these
// in; on ES1 they come from <GLES/gl.h>.

bool gl_load_functions();   // No-op except on Windows where it loads
                            // 1.2+ extension wrappers via SDL_GL_GetProcAddress.
bool gl_check_runtime();    // MS software-ICD diagnostic — prints to stderr
                            // and returns false when the driver is broken.

#endif
