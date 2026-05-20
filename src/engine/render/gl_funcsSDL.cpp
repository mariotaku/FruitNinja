// SDL-bound GL helpers that survive the move to direct OS-library linkage.
//
// gl_load_functions() used to be a SDL_GL_GetProcAddress loader for every
// GL entry point; now those resolve through the OS linker (libGL on Linux,
// libGLESv1_CM on webOS, opengl32 on Windows for the GL 1.1 baseline).
// On Windows we still need a runtime loader for GL 1.2+ extensions because
// opengl32.lib doesn't export them — that's handled by gl_funcsWin32.cpp,
// which gl_load_functions() forwards to here.
//
// gl_check_runtime() detects the Microsoft 1.1 software ICD (the Windows
// fallback when the hardware GL driver isn't reachable) and prints an
// actionable diagnostic.

#include "render/gl_funcs.h"
#include "debug/Logger.h"

#if defined(_WIN32)
extern bool gl_load_extensions_win32();
#endif

// glFrustumf / glClearDepthf are ES1 entry points; on every desktop GL
// driver they're either absent (mingw / MSVC opengl32.lib, some libGL
// builds without ARB_ES2_compatibility) or marginally available. Forward
// to glFrustum / glClearDepth (GL 1.0 baseline -- always exported). The
// float -> double cast is exact for representable values.
//
// MSVC C4273 ("inconsistent dll linkage") fires because SDL_opengl.h
// declares these with dllimport. The link picks our definition; the
// warning is noise -- suppress it for this TU.
#if defined(FRUIT_GL_API_GL_COMPAT)
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4273)
#endif
extern "C" void APIENTRY glFrustumf(GLfloat l, GLfloat r, GLfloat b,
                                    GLfloat t, GLfloat n, GLfloat f) {
    glFrustum((GLdouble)l, (GLdouble)r, (GLdouble)b,
              (GLdouble)t, (GLdouble)n, (GLdouble)f);
}
extern "C" void APIENTRY glClearDepthf(GLclampf d) {
    glClearDepth((GLclampd)d);
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#endif

bool gl_load_functions() {
#if defined(_WIN32)
    return gl_load_extensions_win32();
#else
    // Linux libGL / libGLESv1_CM / libGLESv2 export everything statically.
    return true;
#endif
}

bool gl_check_runtime() {
    const char* version = (const char*)glGetString(GL_VERSION);
    const char* vendor  = (const char*)glGetString(GL_VENDOR);
    bool _ms_vendor = false;
    if (vendor) {
        for (const char* p = vendor; *p; ++p)
            if (p[0]=='M' && p[1]=='i' && p[2]=='c' && p[3]=='r' &&
                p[4]=='o' && p[5]=='s' && p[6]=='o' && p[7]=='f' && p[8]=='t')
            { _ms_vendor = true; break; }
    }
    const bool is_ms_software_icd =
        version && vendor &&
        version[0] == '1' && version[1] == '.' && version[2] == '1' &&
        _ms_vendor;

    if (is_ms_software_icd) {
        LOG_ERROR("GL/loader",
            "falling back to Microsoft 1.1 software ICD -- rendering will be incomplete.\n"
            "  GL_VENDOR=%s  GL_VERSION=%s\n"
            "  Possible causes: GPU driver crashed; RDP/VM without GPU passthrough; anti-cheat blocking ICD.",
            vendor ? vendor : "<null>",
            version ? version : "<null>");
        return false;
    }
    return true;
}
