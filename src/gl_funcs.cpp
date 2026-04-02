#include "gl_funcs.h"

// Define all function pointers
#define GL_FUNC(ret, name, ...) PFN_##name name = nullptr;

GL_FUNC(const GLubyte*, glGetString, GLenum)
GL_FUNC(void, glViewport, GLint, GLint, GLsizei, GLsizei)
GL_FUNC(void, glClearColor, GLfloat, GLfloat, GLfloat, GLfloat)
GL_FUNC(void, glClear, GLbitfield)
GL_FUNC(void, glEnable, GLenum)
GL_FUNC(void, glDisable, GLenum)
GL_FUNC(void, glBlendFunc, GLenum, GLenum)
GL_FUNC(void, glGenTextures, GLsizei, GLuint*)
GL_FUNC(void, glDeleteTextures, GLsizei, const GLuint*)
GL_FUNC(void, glBindTexture, GLenum, GLuint)
GL_FUNC(void, glTexParameteri, GLenum, GLenum, GLint)
GL_FUNC(void, glTexImage2D, GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)
GL_FUNC(void, glActiveTexture, GLenum)
GL_FUNC(void, glDrawArrays, GLenum, GLint, GLsizei)
GL_FUNC(GLuint, glCreateShader, GLenum)
GL_FUNC(void, glShaderSource, GLuint, GLsizei, const GLchar**, const GLint*)
GL_FUNC(void, glCompileShader, GLuint)
GL_FUNC(void, glGetShaderiv, GLuint, GLenum, GLint*)
GL_FUNC(void, glGetShaderInfoLog, GLuint, GLsizei, GLsizei*, GLchar*)
GL_FUNC(void, glDeleteShader, GLuint)
GL_FUNC(GLuint, glCreateProgram, void)
GL_FUNC(void, glAttachShader, GLuint, GLuint)
GL_FUNC(void, glBindAttribLocation, GLuint, GLuint, const GLchar*)
GL_FUNC(void, glLinkProgram, GLuint)
GL_FUNC(void, glGetProgramiv, GLuint, GLenum, GLint*)
GL_FUNC(void, glGetProgramInfoLog, GLuint, GLsizei, GLsizei*, GLchar*)
GL_FUNC(void, glDeleteProgram, GLuint)
GL_FUNC(void, glUseProgram, GLuint)
GL_FUNC(GLint, glGetUniformLocation, GLuint, const GLchar*)
GL_FUNC(void, glUniform1i, GLint, GLint)
GL_FUNC(void, glUniform1f, GLint, GLfloat)
GL_FUNC(void, glUniform4f, GLint, GLfloat, GLfloat, GLfloat, GLfloat)
GL_FUNC(void, glEnableVertexAttribArray, GLuint)
GL_FUNC(void, glDisableVertexAttribArray, GLuint)
GL_FUNC(void, glVertexAttribPointer, GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)

#undef GL_FUNC

bool gl_load_functions() {
#define LOAD(name) \
    name = (PFN_##name)SDL_GL_GetProcAddress(#name); \
    if (!name) { fprintf(stderr, "Failed to load GL function: %s\n", #name); return false; }

    LOAD(glGetString)
    LOAD(glViewport)
    LOAD(glClearColor)
    LOAD(glClear)
    LOAD(glEnable)
    LOAD(glDisable)
    LOAD(glBlendFunc)
    LOAD(glGenTextures)
    LOAD(glDeleteTextures)
    LOAD(glBindTexture)
    LOAD(glTexParameteri)
    LOAD(glTexImage2D)
    LOAD(glActiveTexture)
    LOAD(glDrawArrays)
    LOAD(glCreateShader)
    LOAD(glShaderSource)
    LOAD(glCompileShader)
    LOAD(glGetShaderiv)
    LOAD(glGetShaderInfoLog)
    LOAD(glDeleteShader)
    LOAD(glCreateProgram)
    LOAD(glAttachShader)
    LOAD(glBindAttribLocation)
    LOAD(glLinkProgram)
    LOAD(glGetProgramiv)
    LOAD(glGetProgramInfoLog)
    LOAD(glDeleteProgram)
    LOAD(glUseProgram)
    LOAD(glGetUniformLocation)
    LOAD(glUniform1i)
    LOAD(glUniform1f)
    LOAD(glUniform4f)
    LOAD(glEnableVertexAttribArray)
    LOAD(glDisableVertexAttribArray)
    LOAD(glVertexAttribPointer)

#undef LOAD
    return true;
}
