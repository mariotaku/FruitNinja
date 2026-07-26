#include "render/Renderer.h"
#include "render/RendererInternal.h"
#include "render/MatrixManager.h"
#include "render/Shaders.h"
#include "asset/Texture.h"
#include "debug/Logger.h"
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cmath>

// GL-backend half of Renderer: the methods that talk to the GL/GLES2 API
// directly (shader compile/link, buffer upload, draw calls, GL state).
// Portable public API (ortho setup, quad-vertex builders) stays in
// Renderer.cpp -- see that file's header comment. Split is link-time only
// (mirrors GameSDL.cpp/GameWii.cpp); Renderer.h is unchanged and both TUs
// implement the same class.

// Port specific: 2D ring VBO capacity. Multiple of sizeof(Shaded2DVertex)
// (24) so the per-draw cursor stays vertex-aligned and glDrawArrays can
// address a draw as first = base/24. ~256KB; the largest single 2D draw
// (full particle flush: 1024 particles * 6 verts * 24B = ~144KB) fits, and
// anything bigger falls back to a one-shot glBufferData.
static const GLsizeiptr kRingSize = (GLsizeiptr)(24 * 10920);

// Ring attrib config: attribs 0/1/2 point at the canonical Shaded2DVertex
// layout inside the ring. Pointer bindings capture the buffer OBJECT, so
// they survive later glBindBuffer changes; re-issued only after the config
// went dirty (3D draw, external buffer churn, orphan).
static void ConfigureRingAttribs() {
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(Shaded2DVertex),
                          (const void*)(size_t)offsetof(Shaded2DVertex, x));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(Shaded2DVertex),
                          (const void*)(size_t)offsetof(Shaded2DVertex, u));
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, (GLsizei)sizeof(Shaded2DVertex),
                          (const void*)(size_t)offsetof(Shaded2DVertex, color));
}

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

    // Port specific: state-cache init. u_tex is 0 (GL_TEXTURE0) for the
    // lifetime of both programs, and the blend func is the one constant the
    // binary ever uses -- set once here, never per draw.
    m_Quad2D.Use();
    glUniform1i(m_Quad2D.TexLoc(), 0);
    m_Mesh3D.Use();
    glUniform1i(m_Mesh3D.TexLoc(), 0);
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Port specific: allocate the persistent 2D ring storage once.
    glGenBuffers(1, &m_QuadVBO);
    glBindBuffer(GL_ARRAY_BUFFER, m_QuadVBO);
    glBufferData(GL_ARRAY_BUFFER, kRingSize, 0, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    m_RingCursor = 0;
    memset(&m_GLState, 0, sizeof(m_GLState));

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
        NotifyTextureDeleted((uint32_t)m_WhiteTex);
        m_WhiteTex = 0;
    }
    if (m_QuadVBO) {
        glDeleteBuffers(1, &m_QuadVBO);
        m_QuadVBO = 0;
    }
}

// See Renderer.h for the full contract. Port specific: state-cached + ring
// VBO -- no per-draw pipeline teardown (nothing fixed-function remains to
// coexist with; MatrixManager no longer issues GL).
void Renderer::DrawShaded2D(const void* verts, int vertCount, int stride,
                            int posOff, int uvOff, int colOff,
                            GLenum prim, GLuint tex, const Matrix44& mvp) {
    if (vertCount <= 0) return;
    GLStateShadow& st = m_GLState;

    if (!st.programValid || st.program != m_Quad2D.Program()) {
        m_Quad2D.Use();
        st.program = m_Quad2D.Program();
        st.programValid = true;
    }

    if (!st.mvp2DValid || memcmp(st.mvp2D, mvp.ptr(), sizeof(st.mvp2D)) != 0) {
        glUniformMatrix4fv(m_Quad2D.MVPLoc(), 1, GL_FALSE, mvp.ptr());
        memcpy(st.mvp2D, mvp.ptr(), sizeof(st.mvp2D));
        st.mvp2DValid = true;
    }

    // Resolve the sampling texture: an explicit tex wins, else the lazily-
    // requested one (which may be 0 -- and 0 must still be BOUND to keep the
    // old incomplete-texture sampling semantics). The requested value then
    // mirrors what the eager path would have left bound on unit 0.
    const GLuint effectiveTex = tex ? tex : (GLuint)st.requestedTex;
    if (!st.boundTexValid || st.boundTex != effectiveTex) {
        glBindTexture(GL_TEXTURE_2D, effectiveTex);
        st.boundTex = effectiveTex;
        st.boundTexValid = true;
    }
    st.requestedTex = (uint32_t)effectiveTex;

    // Repack to the canonical Shaded2DVertex layout (QUADCUSTOMVERTEX carries
    // normals the shader never reads -- attribs 0/1/2 skip them). Vertex bit
    // patterns (pos/uv/colour) are copied verbatim.
    const GLsizeiptr size = (GLsizeiptr)((size_t)vertCount * sizeof(Shaded2DVertex));
    const void* packed;
    if (stride == (int)sizeof(Shaded2DVertex) &&
        posOff == (int)offsetof(Shaded2DVertex, x) &&
        uvOff  == (int)offsetof(Shaded2DVertex, u) &&
        colOff == (int)offsetof(Shaded2DVertex, color)) {
        packed = verts;
    } else {
        if ((GLsizeiptr)m_Staging2D.size() < size) {
            m_Staging2D.resize((size_t)size);
        }
        Shaded2DVertex* dst = reinterpret_cast<Shaded2DVertex*>(&m_Staging2D[0]);
        const unsigned char* src = static_cast<const unsigned char*>(verts);
        for (int i = 0; i < vertCount; ++i, src += stride) {
            memcpy(&dst[i].x,     src + posOff, 3 * sizeof(float));
            memcpy(&dst[i].u,     src + uvOff,  2 * sizeof(float));
            memcpy(&dst[i].color, src + colOff, sizeof(uint32_t));
        }
        packed = dst;
    }

    if (!st.ringReady) {
        glBindBuffer(GL_ARRAY_BUFFER, m_QuadVBO);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);
        ConfigureRingAttribs();
        st.ringReady = true;
    }

    if (size > kRingSize) {
        // Oversize fallback: one-shot upload replaces the ring storage; the
        // forced wrap below re-allocates it before the next ring draw.
        glBufferData(GL_ARRAY_BUFFER, size, packed, GL_DYNAMIC_DRAW);
        glDrawArrays(prim, 0, vertCount);
        m_RingCursor = kRingSize;
        st.ringReady = false;
        return;
    }

    if (m_RingCursor + size > kRingSize) {
        // Orphan: fresh storage, rewind. Avoids stalling on verts still in
        // flight (the WebGL-safe streaming idiom).
        glBufferData(GL_ARRAY_BUFFER, kRingSize, 0, GL_DYNAMIC_DRAW);
        m_RingCursor = 0;
        ConfigureRingAttribs();
    }
    glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)m_RingCursor, size, packed);
    glDrawArrays(prim, (GLint)(m_RingCursor / (GLsizeiptr)sizeof(Shaded2DVertex)), vertCount);
    m_RingCursor += size;   // size is a multiple of 24, cursor stays aligned
}

// See Renderer.h for the full contract. Port specific: state-cached; no
// trailing restore. Marks the 2D ring attrib config dirty so the next 2D
// draw rebinds the ring and re-issues its pointers.
void Renderer::DrawMesh3D(GLuint vbo, GLuint ibo, int vertCount, int indexCount, GLenum prim,
                          int stride, int posOff, int uvOff, int colOff, int uvSize, int colSize,
                          GLuint tex, const Matrix44& mvp) {
    GLStateShadow& st = m_GLState;

    if (!st.programValid || st.program != m_Mesh3D.Program()) {
        m_Mesh3D.Use();
        st.program = m_Mesh3D.Program();
        st.programValid = true;
    }

    if (!st.mvp3DValid || memcmp(st.mvp3D, mvp.ptr(), sizeof(st.mvp3D)) != 0) {
        glUniformMatrix4fv(m_Mesh3D.MVPLoc(), 1, GL_FALSE, mvp.ptr());
        memcpy(st.mvp3D, mvp.ptr(), sizeof(st.mvp3D));
        st.mvp3DValid = true;
    }

    const GLuint effectiveTex = tex ? tex : m_WhiteTex;
    if (!st.boundTexValid || st.boundTex != effectiveTex) {
        glBindTexture(GL_TEXTURE_2D, effectiveTex);
        st.boundTex = effectiveTex;
        st.boundTexValid = true;
    }
    st.requestedTex = (uint32_t)effectiveTex;  // mirror the legacy eager binding

    // Mesh layouts vary per draw -- bind + point per call; the next 2D draw
    // restores the ring config.
    st.ringReady = false;
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

// Port specific: lazy -- records the requested sampling texture only; the
// real bind happens at the next draw (see Renderer.h doc).
void Renderer::BindTexture2D(uint32_t texId) {
    m_GLState.requestedTex = texId;
}

void Renderer::SetWireframe(bool enabled) {
#if defined(FRUIT_GL_API_GL_COMPAT)
    // glPolygonMode is desktop-GL-only -- not in the GLES2 (ES2/webOS,
    // Emscripten/WebGL) API. Wireframe debug is a no-op everywhere else.
    if (glPolygonMode != nullptr) {
        glPolygonMode(GL_FRONT_AND_BACK, enabled ? GL_LINE : GL_FILL);
    }
#else
    (void)enabled;
#endif
}
