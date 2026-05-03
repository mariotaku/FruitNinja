#include "render/gl_funcs.h"

// Define all function pointers
#define GL_FUNC(ret, name, ...) PFN_##name name = nullptr;

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
GL_FUNC(void, glClearDepthf, GLfloat)

GL_FUNC(void, glMatrixMode, GLenum)
GL_FUNC(void, glPushMatrix, void)
GL_FUNC(void, glPopMatrix, void)
GL_FUNC(void, glLoadMatrixf, const GLfloat*)
GL_FUNC(void, glMultMatrixf, const GLfloat*)
GL_FUNC(void, glLoadIdentity, void)
GL_FUNC(void, glFrustumf, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat)
GL_FUNC(void, glEnableClientState, GLenum)
GL_FUNC(void, glDisableClientState, GLenum)
GL_FUNC(void, glVertexPointer, GLint, GLenum, GLsizei, const void*)
GL_FUNC(void, glNormalPointer, GLenum, GLsizei, const void*)
GL_FUNC(void, glColorPointer, GLint, GLenum, GLsizei, const void*)
GL_FUNC(void, glTexCoordPointer, GLint, GLenum, GLsizei, const void*)
GL_FUNC(void, glClientActiveTexture, GLenum)
GL_FUNC(void, glColor4ub, GLubyte, GLubyte, GLubyte, GLubyte)
GL_FUNC(void, glMaterialfv, GLenum, GLenum, const GLfloat*)
GL_FUNC(void, glLightfv, GLenum, GLenum, const GLfloat*)
GL_FUNC(void, glShadeModel, GLenum)
GL_FUNC(void, glTexEnvf, GLenum, GLenum, GLfloat)

// Optional — see header.
GL_FUNC(void, glPolygonMode, GLenum, GLenum)

#undef GL_FUNC

// ---------------------------------------------------------------------------
// Stubs for OPTIONAL functions that may be absent on the Microsoft 1.1
// software ICD (the fallback Windows uses when the hardware GL driver isn't
// reachable). Real-world this happens when (a) the user's GPU driver has
// crashed/reset, (b) the WDDM compositor isn't yet associated with the
// process, or (c) the test runner spawns subprocesses too fast for the ICD
// lookup to complete. Using stubs keeps gl_load_functions() returning true
// in those environments so the test harness can still smoke-test gameplay
// logic (which doesn't actually exercise the GPU). The main exe should
// call gl_check_runtime() afterwards and surface a clear diagnostic.
// ---------------------------------------------------------------------------

static void stub_glActiveTexture(GLenum) {}
static void stub_glClientActiveTexture(GLenum) {}
static void stub_glCompressedTexImage2D(GLenum, GLint, GLenum, GLsizei, GLsizei,
                                        GLint, GLsizei, const void*) {}
static void stub_glGenBuffers(GLsizei, GLuint*) {}
static void stub_glDeleteBuffers(GLsizei, const GLuint*) {}
static void stub_glBindBuffer(GLenum, GLuint) {}
static void stub_glBufferData(GLenum, GLsizeiptr, const void*, GLenum) {}
static void stub_glClearDepthf(GLfloat) {}
static void stub_glFrustumf(GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat) {}

bool gl_load_functions() {
#define LOAD_REQ(name) \
    name = (PFN_##name)SDL_GL_GetProcAddress(#name); \
    if (!name) { fprintf(stderr, "Failed to load REQUIRED GL function: %s\n", #name); return false; }
#define LOAD_OPT(name) \
    name = (PFN_##name)SDL_GL_GetProcAddress(#name); \
    if (!name) { name = stub_##name; }

    // Required: GL 1.1 baseline. The Microsoft software ICD on Windows
    // provides exactly these (and nothing later); they're the floor below
    // which we genuinely can't run.
    LOAD_REQ(glGetString)
    LOAD_REQ(glGetError)
    LOAD_REQ(glViewport)
    LOAD_REQ(glClearColor)
    LOAD_REQ(glClear)
    LOAD_REQ(glEnable)
    LOAD_REQ(glDisable)
    LOAD_REQ(glBlendFunc)
    LOAD_REQ(glScissor)
    LOAD_REQ(glPixelStorei)
    LOAD_REQ(glGenTextures)
    LOAD_REQ(glDeleteTextures)
    LOAD_REQ(glBindTexture)
    LOAD_REQ(glTexParameteri)
    LOAD_REQ(glTexImage2D)
    LOAD_REQ(glDrawArrays)
    LOAD_REQ(glDrawElements)
    LOAD_REQ(glDepthFunc)
    LOAD_REQ(glDepthMask)

    // Required: ES 1.x fixed-pipeline (also present in desktop GL 1.1 compat).
    LOAD_REQ(glMatrixMode)
    LOAD_REQ(glPushMatrix)
    LOAD_REQ(glPopMatrix)
    LOAD_REQ(glLoadMatrixf)
    LOAD_REQ(glMultMatrixf)
    LOAD_REQ(glLoadIdentity)
    LOAD_REQ(glEnableClientState)
    LOAD_REQ(glDisableClientState)
    LOAD_REQ(glVertexPointer)
    LOAD_REQ(glNormalPointer)
    LOAD_REQ(glColorPointer)
    LOAD_REQ(glTexCoordPointer)
    LOAD_REQ(glColor4ub)
    LOAD_REQ(glMaterialfv)
    LOAD_REQ(glLightfv)
    LOAD_REQ(glShadeModel)
    LOAD_REQ(glTexEnvf)

    // Optional (GL 1.2+ / ES extensions). Stubbed if absent so the rest of
    // the pipeline can still initialise. gl_check_runtime() should be called
    // by interactive entry points (the main exe) to warn when stubs are
    // active; non-rendering smoke tests can ignore.
    LOAD_OPT(glActiveTexture)              // GL 1.3
    LOAD_OPT(glClientActiveTexture)        // GL 1.3
    LOAD_OPT(glCompressedTexImage2D)       // GL 1.3
    LOAD_OPT(glGenBuffers)                 // GL 1.5
    LOAD_OPT(glDeleteBuffers)              // GL 1.5
    LOAD_OPT(glBindBuffer)                 // GL 1.5
    LOAD_OPT(glBufferData)                 // GL 1.5
    LOAD_OPT(glClearDepthf)                // ES core; desktop ARB ext
    LOAD_OPT(glFrustumf)                   // ES; desktop has glFrustum

    // Optional, no stub -- callers null-check. F2 wireframe debug toggle.
    glPolygonMode = (PFN_glPolygonMode)SDL_GL_GetProcAddress("glPolygonMode");

#undef LOAD_REQ
#undef LOAD_OPT
    return true;
}

bool gl_check_runtime() {
    // Detect the Microsoft software ICD (GL_VERSION starts with "1.1.0",
    // GL_VENDOR contains "Microsoft Corporation") and complain loudly. The
    // resulting render is going to be black; better to tell the user
    // up-front than to ship a broken-looking window.
    if (!glGetString) return true;
    const char* version = (const char*)glGetString(0x1F02 /*GL_VERSION*/);
    const char* vendor  = (const char*)glGetString(0x1F00 /*GL_VENDOR */);
    const bool is_ms_software_icd =
        version && vendor &&
        version[0] == '1' && version[1] == '.' && version[2] == '1' &&
        // strstr-free contains check
        [&]() -> bool {
            for (const char* p = vendor; *p; ++p)
                if (p[0]=='M' && p[1]=='i' && p[2]=='c' && p[3]=='r' &&
                    p[4]=='o' && p[5]=='s' && p[6]=='o' && p[7]=='f' && p[8]=='t')
                    return true;
            return false;
        }();

    if (is_ms_software_icd) {
        fprintf(stderr,
            "\n========================================================================\n"
            " GL: Falling back to Microsoft 1.1 software ICD.\n"
            "     GL_VENDOR=%s\n"
            "     GL_VERSION=%s\n"
            " The hardware GL driver isn't reachable -- rendering will be incomplete.\n"
            " Most extension entry points (glActiveTexture, glGenBuffers, etc.) are\n"
            " stubbed. Possible causes:\n"
            "   - GPU driver crashed; reboot or reinstall it.\n"
            "   - Running over RDP / inside a VM without GPU passthrough.\n"
            "   - Anti-cheat / driver overlay blocking ICD lookup.\n"
            " To diagnose: open the Windows Display Settings -> Advanced display ->\n"
            " note the active adapter; verify with GPU-Z / dxdiag that an HW driver\n"
            " is actually loaded.\n"
            "========================================================================\n",
            vendor ? vendor : "<null>",
            version ? version : "<null>");
        return false;
    }
    return true;
}
