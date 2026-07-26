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
    m_Batch2D.clear();   // drop, don't draw -- the GL objects are going away
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

// Pack one source vertex (pos 3xfloat @posOff, uv 2xfloat @uvOff, colour
// 4 bytes @colOff) into the canonical Shaded2DVertex layout. Bit patterns
// are copied verbatim (QUADCUSTOMVERTEX's normals are skipped -- the shader
// never reads them).
static inline void PackShaded2DVert(Shaded2DVertex& dst, const unsigned char* src,
                                    int posOff, int uvOff, int colOff) {
    memcpy(&dst.x,     src + posOff, 3 * sizeof(float));
    memcpy(&dst.u,     src + uvOff,  2 * sizeof(float));
    memcpy(&dst.color, src + colOff, sizeof(uint32_t));
}

// See Renderer.h for the full contract. Stage 2: submit-only -- expands
// strips to triangles, optionally CPU pre-transforms by the MVP, and appends
// to the pending batch; Flush2D() issues the GL calls.
void Renderer::DrawShaded2D(const void* verts, int vertCount, int stride,
                            int posOff, int uvOff, int colOff,
                            GLenum prim, GLuint tex, const Matrix44& mvp) {
    if (vertCount <= 0) return;
    GLStateShadow& st = m_GLState;

    // Resolve the sampling texture: an explicit tex wins, else the lazily-
    // requested one (which may be 0 -- and 0 must still be BOUND at flush to
    // keep the old incomplete-texture sampling semantics). The requested
    // value then mirrors what the eager path would have left bound on unit 0.
    const GLuint effectiveTex = tex ? tex : (GLuint)st.requestedTex;
    st.requestedTex = (uint32_t)effectiveTex;

    // MVP merge eligibility: only an affine MVP (projective bottom row
    // (0,0,0,1), i.e. clip w == 1 for every vertex) can be CPU
    // pre-transformed -- the vec3 position attrib cannot carry a w.
    const float* M = mvp.ptr();
    const bool preXf = m_Merge2DPreTransform &&
                       M[3] == 0.0f && M[7] == 0.0f && M[11] == 0.0f && M[15] == 1.0f;

    // Vertex count after strip -> list expansion (`prim` is GL_TRIANGLES or
    // GL_TRIANGLE_STRIP; no other primitive reaches this path).
    int outCount;
    if (prim == GL_TRIANGLE_STRIP) {
        if (vertCount < 3) return;
        outCount = (vertCount - 2) * 3;
    } else {
        outCount = vertCount;
    }
    const size_t newBytes = (size_t)outCount * sizeof(Shaded2DVertex);

    // Batch break: texture change, pre-transform mode change, MVP change
    // (only when not pre-transforming), or ring overflow.
    if (!m_Batch2D.empty()) {
        bool brk = (m_BatchTex != effectiveTex) || (m_BatchPreXf != preXf);
        if (!brk && !preXf) {
            brk = (memcmp(m_BatchMVP, M, sizeof(m_BatchMVP)) != 0);
        }
        if (!brk && m_Batch2D.size() + newBytes > (size_t)kRingSize) {
            brk = true;
        }
        if (brk) {
            Flush2D();
        }
    }
    if (m_Batch2D.empty()) {
        m_BatchTex   = effectiveTex;
        m_BatchPreXf = preXf;
        if (!preXf) {
            memcpy(m_BatchMVP, M, sizeof(m_BatchMVP));
        }
    }

    const size_t base = m_Batch2D.size();
    m_Batch2D.resize(base + newBytes);
    Shaded2DVertex* dst = reinterpret_cast<Shaded2DVertex*>(&m_Batch2D[base]);
    const unsigned char* src = static_cast<const unsigned char*>(verts);

    if (prim == GL_TRIANGLE_STRIP) {
        // Strip triangle t = (t, t+1, t+2), odd t swaps the leading pair to
        // preserve winding. Fonts' degenerate connector verts become
        // zero-area triangles -- zero pixels, identical output.
        int o = 0;
        for (int t = 0; t + 2 < vertCount; ++t) {
            const int i0 = (t & 1) ? t + 1 : t;
            const int i1 = (t & 1) ? t     : t + 1;
            PackShaded2DVert(dst[o++], src + (size_t)i0 * stride, posOff, uvOff, colOff);
            PackShaded2DVert(dst[o++], src + (size_t)i1 * stride, posOff, uvOff, colOff);
            PackShaded2DVert(dst[o++], src + (size_t)(t + 2) * stride, posOff, uvOff, colOff);
        }
    } else {
        for (int i = 0; i < vertCount; ++i) {
            PackShaded2DVert(dst[i], src + (size_t)i * stride, posOff, uvOff, colOff);
        }
    }

    if (preXf) {
        // Full affine transform (x,y,z; w==1 guaranteed by the check above).
        // Same CPU pre-transform idiom the TTF glyph path uses (Font.cpp);
        // column-major m[col*4+row], matching the GL_FALSE-transpose upload.
        for (int i = 0; i < outCount; ++i) {
            const float x = dst[i].x, y = dst[i].y, z = dst[i].z;
            dst[i].x = M[0] * x + M[4] * y + M[8]  * z + M[12];
            dst[i].y = M[1] * x + M[5] * y + M[9]  * z + M[13];
            dst[i].z = M[2] * x + M[6] * y + M[10] * z + M[14];
        }
    }
}

// See Renderer.h for the full contract. Draws the pending 2D batch: one ring
// upload + one glDrawArrays(GL_TRIANGLES), state-cached, no trailing restore.
void Renderer::Flush2D() {
    const size_t bytes = m_Batch2D.size();
    if (bytes == 0) return;
    const int vertCount = (int)(bytes / sizeof(Shaded2DVertex));
    GLStateShadow& st = m_GLState;

    if (!st.programValid || st.program != m_Quad2D.Program()) {
        m_Quad2D.Use();
        st.program = m_Quad2D.Program();
        st.programValid = true;
    }

    // Pre-transformed batches draw with u_mvp = identity (constant, so the
    // memcmp cache reduces uniformMatrix4fv to ~one upload per frame).
    static const float kIdentityMVP[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    const float* mvpPtr = m_BatchPreXf ? kIdentityMVP : m_BatchMVP;
    if (!st.mvp2DValid || memcmp(st.mvp2D, mvpPtr, sizeof(st.mvp2D)) != 0) {
        glUniformMatrix4fv(m_Quad2D.MVPLoc(), 1, GL_FALSE, mvpPtr);
        memcpy(st.mvp2D, mvpPtr, sizeof(st.mvp2D));
        st.mvp2DValid = true;
    }

    if (!st.boundTexValid || st.boundTex != m_BatchTex) {
        glBindTexture(GL_TEXTURE_2D, m_BatchTex);
        st.boundTex = m_BatchTex;
        st.boundTexValid = true;
    }

    // Bind the ring unconditionally: GL_ARRAY_BUFFER may have been rebound
    // (mesh uploads) since the batch opened. The attrib POINTERS captured
    // the ring buffer OBJECT, so only the upload target needs re-asserting;
    // ringReady still gates the enable + pointer config.
    glBindBuffer(GL_ARRAY_BUFFER, m_QuadVBO);
    if (!st.ringReady) {
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);
        ConfigureRingAttribs();
        st.ringReady = true;
    }

    const GLsizeiptr size = (GLsizeiptr)bytes;
    const void* packed = &m_Batch2D[0];
    if (size > kRingSize) {
        // Oversize fallback (cannot occur via DrawShaded2D, which breaks the
        // batch before it outgrows the ring; kept for the degenerate case of
        // a single submission larger than the ring): one-shot upload replaces
        // the ring storage; the forced wrap below re-allocates it.
        glBufferData(GL_ARRAY_BUFFER, size, packed, GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, vertCount);
        m_RingCursor = kRingSize;
        st.ringReady = false;
        m_Batch2D.clear();
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
    glDrawArrays(GL_TRIANGLES, (GLint)(m_RingCursor / (GLsizeiptr)sizeof(Shaded2DVertex)), vertCount);
    m_RingCursor += size;   // size is a multiple of 24, cursor stays aligned
    m_Batch2D.clear();      // keeps capacity -- no per-frame reallocation
}

// See Renderer.h for the full contract. Port specific: state-cached; no
// trailing restore. Marks the 2D ring attrib config dirty so the next 2D
// draw rebinds the ring and re-issues its pointers.
void Renderer::DrawMesh3D(GLuint vbo, GLuint ibo, int vertCount, int indexCount, GLenum prim,
                          int stride, int posOff, int uvOff, int colOff, int uvSize, int colSize,
                          GLuint tex, const Matrix44& mvp) {
    // Stage-2 barrier: 2D verts submitted before this mesh must be drawn
    // before it (2D/3D interleave via the depth buffer -- order matters).
    Flush2D();
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
    // Stage-2 barrier: pending 2D verts were submitted under the current
    // scissor state -- draw them before it changes.
    Flush2D();
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
    // Stage-2 barrier: see SetClipRect.
    Flush2D();
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
