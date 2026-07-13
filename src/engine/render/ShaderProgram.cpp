// Port specific: GL 2.0 / GLES2 shader-program wrapper. See ShaderProgram.h.

#include "render/ShaderProgram.h"
#include "debug/Logger.h"

ShaderProgram::ShaderProgram()
    : m_Program(0), m_MVPLoc(-1), m_TexLoc(-1) {}

static GLuint CompileStage(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    if (!shader) {
        LOG_ERROR("GL/shader", "glCreateShader(%s) returned 0 -- GL 2.0 entry points unavailable?",
                  type == GL_VERTEX_SHADER ? "vertex" : "fragment");
        return 0;
    }
    const char* sources[1] = { src };
    glShaderSource(shader, 1, sources, 0);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        log[0] = 0;
        GLsizei len = 0;
        glGetShaderInfoLog(shader, (GLsizei)sizeof(log), &len, log);
        LOG_ERROR("GL/shader", "%s shader compile failed:\n%s",
                  type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool ShaderProgram::Compile(const char* vsSrc, const char* fsSrc) {
    GLuint vs = CompileStage(GL_VERTEX_SHADER, vsSrc);
    if (!vs) {
        return false;
    }
    GLuint fs = CompileStage(GL_FRAGMENT_SHADER, fsSrc);
    if (!fs) {
        glDeleteShader(vs);
        return false;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    // Fixed attribute slots -- must happen BEFORE glLinkProgram.
    glBindAttribLocation(prog, 0, "a_pos");
    glBindAttribLocation(prog, 1, "a_uv");
    glBindAttribLocation(prog, 2, "a_color");
    glLinkProgram(prog);
    // GL refcounts the program's reference to attached shaders; deleting the
    // shader objects here frees them with the program and avoids leak-on-relink.
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[1024];
        log[0] = 0;
        GLsizei len = 0;
        glGetProgramInfoLog(prog, (GLsizei)sizeof(log), &len, log);
        LOG_ERROR("GL/shader", "program link failed:\n%s", log);
        glDeleteProgram(prog);
        return false;
    }

    // Replace any previous program only after a fully successful link.
    Destroy();
    m_Program = prog;
    m_MVPLoc = glGetUniformLocation(prog, "u_mvp");
    m_TexLoc = glGetUniformLocation(prog, "u_tex");
    return true;
}

void ShaderProgram::Use() const {
    glUseProgram(m_Program);
}

void ShaderProgram::Destroy() {
    if (m_Program) {
        glDeleteProgram(m_Program);
        m_Program = 0;
    }
    m_MVPLoc = -1;
    m_TexLoc = -1;
}
