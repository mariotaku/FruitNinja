#include "render/Renderer.h"
#include "render/RendererInternal.h"
#include "render/MatrixManager.h"
#include "render/Shaders.h"
#include "asset/Texture.h"
#include "debug/Logger.h"
#include <cstddef>
#include <cstdio>
#include <cmath>

// GL-backend half of Renderer: the methods that talk to the GL/GLES2 API
// directly (shader compile/link, buffer upload, draw calls, GL state).
// Portable public API (ortho setup, quad-vertex builders) stays in
// Renderer.cpp -- see that file's header comment. Split is link-time only
// (mirrors GameSDL.cpp/GameWii.cpp); Renderer.h is unchanged and both TUs
// implement the same class.

bool Renderer::init() {
    s_instance = this;

    if (!m_Quad2D.Compile(FnShaders::Quad2D_VS, FnShaders::Quad2D_FS)) {
        LOG_ERROR("RENDERER/init", "2D shader program failed to build");
        return false;
    }

    if (!m_Mesh3D.Compile(FnShaders::Mesh3D_VS, FnShaders::Mesh3D_FS)) {
        LOG_ERROR("RENDERER/init", "3D mesh shader program failed to build");
        m_Quad2D.Destroy();
        return false;
    }

    glGenBuffers(1, &m_QuadVBO);

    // 1x1 opaque white texture: DrawColorQuad binds it so texture2D samples
    // 1.0 and the vertex colour passes through (the FF path drew untextured).
    glGenTextures(1, &m_WhiteTex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_WhiteTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    static const unsigned char kWhiteTexel[4] = { 255, 255, 255, 255 };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, kWhiteTexel);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void Renderer::shutdown() {
    m_Quad2D.Destroy();
    m_Mesh3D.Destroy();
    if (m_WhiteTex) {
        glDeleteTextures(1, &m_WhiteTex);
        m_WhiteTex = 0;
    }
    if (m_QuadVBO) {
        glDeleteBuffers(1, &m_QuadVBO);
        m_QuadVBO = 0;
    }
}

// See Renderer.h for the full contract. Never early-return between Use()
// and the trailing restore -- the glUseProgram(0) + attrib-disable is the
// coexistence guarantee for the still-fixed-function paths (Geometry::Render
// and the matrix-stack uploads).
void Renderer::DrawShaded2D(const void* verts, int vertCount, int stride,
                            int posOff, int uvOff, int colOff,
                            GLenum prim, GLuint tex, const Matrix44& mvp) {
    m_Quad2D.Use();
    glUniformMatrix4fv(m_Quad2D.MVPLoc(), 1, GL_FALSE, mvp.ptr());

    glActiveTexture(GL_TEXTURE0);
    if (tex) {
        glBindTexture(GL_TEXTURE_2D, tex);
    }
    glUniform1i(m_Quad2D.TexLoc(), 0);

    const GLsizeiptr size = (GLsizeiptr)((size_t)vertCount * (size_t)stride);
    glBindBuffer(GL_ARRAY_BUFFER, m_QuadVBO);
    glBufferData(GL_ARRAY_BUFFER, size, 0, GL_DYNAMIC_DRAW);      // orphan
    glBufferData(GL_ARRAY_BUFFER, size, verts, GL_DYNAMIC_DRAW);  // upload

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (const void*)(size_t)posOff);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (const void*)(size_t)uvOff);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (const void*)(size_t)colOff);

    glDrawArrays(prim, 0, vertCount);

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

// See Renderer.h for the full contract. Same coexistence discipline as
// DrawShaded2D: never early-return between Use() and the trailing restore.
void Renderer::DrawMesh3D(GLuint vbo, GLuint ibo, int vertCount, int indexCount, GLenum prim,
                          int stride, int posOff, int uvOff, int colOff, int uvSize, int colSize,
                          GLuint tex, const Matrix44& mvp) {
    m_Mesh3D.Use();
    glUniformMatrix4fv(m_Mesh3D.MVPLoc(), 1, GL_FALSE, mvp.ptr());

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex ? tex : m_WhiteTex);
    glUniform1i(m_Mesh3D.TexLoc(), 0);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    if (ibo) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    }

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (const void*)(size_t)posOff);

    if (uvSize > 0) {
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (const void*)(size_t)uvOff);
    } else {
        glDisableVertexAttribArray(1);
        glVertexAttrib4f(1, 0.0f, 0.0f, 0.0f, 0.0f);
    }

    if (colSize > 0) {
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (const void*)(size_t)colOff);
    } else {
        glDisableVertexAttribArray(2);
        // GLES2's generic-attrib default is (0,0,0,1) opaque black; the
        // fixed-function default colour this replaces (glColor4ub 255,255,
        // 255,255 in the old Geometry::Render) is white. Must set explicitly.
        glVertexAttrib4f(2, 1.0f, 1.0f, 1.0f, 1.0f);
    }

    if (ibo && indexCount > 0) {
        glDrawElements(prim, indexCount, GL_UNSIGNED_SHORT, (const void*)0);
    } else {
        glDrawArrays(prim, 0, vertCount);
    }

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

// Matches FruitNinja::InitGL (0x00181e54). Runs once at startup after
// the GL context exists, establishes baseline state. The binary's EGL
// Pbuffer path is omitted — SDL handles the surface.
//
// The enables here (DEPTH_TEST, CULL_FACE) all get flipped/overwritten by
// BeginFrame on the first frame — so they're effectively one-time hints to
// the driver. Mirrored anyway for strict 1:1 fidelity with the binary.
// The binary's fixed-pipeline SetPerspective() projection seed (glFrustumf
// via glMatrixMode(GL_PROJECTION)/glLoadIdentity()) is dead: the shader
// pipeline's first-frame SetupPerspective/SetupGameOrtho call always
// overwrites MatrixManager's projection matrix before any draw, so the
// GL-fixed-function seed here was never observably consumed. Removed with
// GLES2 migration phase 4.
void Renderer::InitGL(int width, int height) {
    glViewport(0, 0, width, height);
    glEnable(GL_CULL_FACE);
    // glCullFace(GL_BACK) — GL default, binary sets it explicitly but we
    // don't load glCullFace (binary never changes the face away from
    // GL_BACK anywhere else), so we rely on the default.
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClear(GL_COLOR_BUFFER_BIT);
}

// Port specific: no binary counterpart (GLES2 has no fixed-function user clip
// planes; the original clips via CPU-side ClipAgainstPlanes). World-space rect
// -> viewport-pixel glScissor, same centered-ortho convention as SetupGameOrtho
// (SetupOrtho(160,-160,-240,240,...)). Moved here verbatim from the duplicated
// UiDropdown.cpp / SettingsScreen.cpp inline blocks -- same math, same guard.
void Renderer::SetClipRect(float left, float top, float right, float bottom) {
#if !defined(__bada__) && !defined(FN_GL_STUB)
    const float orthoW = 480.0f;
    const float orthoH = 320.0f;
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    const GLint vpX = vp[0], vpY = vp[1];
    const GLsizei vpW = (GLsizei)vp[2], vpH = (GLsizei)vp[3];

    GLint sx = (GLint)((left + orthoW * 0.5f) / orthoW * (float)vpW) + vpX;
    GLint sy = (GLint)((bottom + orthoH * 0.5f) / orthoH * (float)vpH) + vpY;
    GLint sw = (GLint)((right - left) / orthoW * (float)vpW);
    GLint sh = (GLint)((top - bottom) / orthoH * (float)vpH);
    if (sw < 0) sw = 0;
    if (sh < 0) sh = 0;

    glEnable(GL_SCISSOR_TEST);
    glScissor(sx, sy, sw, sh);
#else
    (void)left; (void)top; (void)right; (void)bottom;
#endif
}

void Renderer::ClearClipRect() {
#if !defined(__bada__) && !defined(FN_GL_STUB)
    glDisable(GL_SCISSOR_TEST);
#endif
}

void Renderer::BindTexture2D(uint32_t texId) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)texId);
}

void Renderer::SetWireframe(bool enabled) {
#if !defined(__bada__) && !defined(__EMSCRIPTEN__)
    if (glPolygonMode != nullptr) {
        glPolygonMode(GL_FRONT_AND_BACK, enabled ? GL_LINE : GL_FILL);
    }
#else
    (void)enabled;
#endif
}
