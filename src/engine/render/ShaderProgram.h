#ifndef FN_RENDER_SHADERPROGRAM_H
#define FN_RENDER_SHADERPROGRAM_H

// Port specific: minimal GL 2.0 / GLES2 shader-program wrapper, no binary
// counterpart (the binary is fixed-function ES 1.x).
//
// Usage / contract:
//   ShaderProgram prog;                       // inert; owns nothing yet
//   prog.Compile(vsSrc, fsSrc);               // needs a LIVE GL context;
//                                             // returns false + LOG_ERROR on
//                                             // compile/link failure
//   prog.Use();                               // glUseProgram(program)
//   glUniformMatrix4fv(prog.MVPLoc(), ...);   // cached "u_mvp" location
//   glUniform1i(prog.TexLoc(), 0);            // cached "u_tex" location
//   ...
//   prog.Destroy();                           // glDeleteProgram; safe to call
//                                             // repeatedly / when never compiled
//
// Invariants:
//   - Compile binds attribute locations BEFORE link:
//       0 = "a_pos", 1 = "a_uv", 2 = "a_color"
//     so every consumer can hardcode those slots (Renderer::DrawShaded2D does).
//   - Both shader objects are deleted right after link (GL keeps its own
//     reference on the program), so repeated Compile calls don't leak.
//   - Compile on an already-compiled instance replaces the program only on
//     success; on failure the previous program (if any) is left untouched.
//   - MVPLoc()/TexLoc() are -1 until a successful Compile (glUniform* on -1
//     is a GL no-op).
//   - Not copied around by the port; treat instances as engine-lifetime GL
//     resources (create in Renderer::init, Destroy in Renderer::shutdown).

#include "render/gl_funcs.h"

class ShaderProgram {
public:
    ShaderProgram();

    // Compiles both stages, binds attrib slots 0/1/2 (a_pos/a_uv/a_color),
    // links, caches u_mvp/u_tex uniform locations. Returns false (and
    // LOG_ERRORs the GL info log) on any compile/link failure.
    bool Compile(const char* vsSrc, const char* fsSrc);

    // glUseProgram(program). Call Compile successfully first.
    void Use() const;

    // glDeleteProgram + reset to the inert state. Safe on a never-compiled
    // or already-destroyed instance.
    void Destroy();

    GLuint Program() const { return m_Program; }
    GLint  MVPLoc()  const { return m_MVPLoc; }
    GLint  TexLoc()  const { return m_TexLoc; }

private:
    GLuint m_Program;
    GLint  m_MVPLoc;
    GLint  m_TexLoc;
};

#endif
