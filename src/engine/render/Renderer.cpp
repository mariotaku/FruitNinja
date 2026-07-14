#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/Shaders.h"
#include "asset/Texture.h"
#include "debug/Logger.h"
#include <cstddef>
#include <cstdio>
#include <cmath>

// 2D draws (DrawQuad / DrawTriList / DrawTriStrip / DrawColorQuad /
// draw_fullscreen_quad) go through the GLES2 Quad2D shader via
// DrawShaded2D; the MVP that the fixed-function path used to upload via
// glLoadMatrixf(GL_PROJECTION) is fed to u_mvp instead — same 16 floats
// (MatrixManager::GetMVP composes proj * view * world). The shader's
// texture2D(u_tex) * v_color reproduces the old GL_MODULATE texenv.
// Font / particle / HUD drawing funnels through DrawTriList/DrawTriStrip,
// so it rides the shader path too. Phase 3: 3D Geometry::Render draws
// through the Mesh3D program via DrawMesh3D (unlit -- all meshes are
// IsLit=false, so the same texture2D * v_color modulate applies).

Renderer* Renderer::s_instance = nullptr;

// Interleaved vertex for the quad paths (DrawQuad / DrawColorQuad /
// draw_fullscreen_quad). QUADCUSTOMVERTEX keeps its own binary layout and
// is fed to DrawShaded2D with its own offsets.
struct Shaded2DVertex {
    float x, y, z;
    float u, v;
    uint32_t color;   // Colour::PlatformColour() packing (LE bytes r,g,b,a)
};

Renderer::Renderer() : m_QuadVBO(0), m_WhiteTex(0) {}

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

void Renderer::SetupGameOrtho() {
    // Verified from binary: SetupOrtho(160, -160, -240, 240, 2000, -6000)
    MatrixManager& mm = MatrixManager::GetInstance();
    mm.SetupOrtho(160.0f, -160.0f, -240.0f, 240.0f, 2000.0f, -6000.0f);
    mm.GetViewStack().Reset();
    mm.GetWorldStack().Reset();
}

void Renderer::draw_fullscreen_quad(GLuint tex, float alpha) {
    // Clip-space fullscreen quad, identity MVP.
    const uint32_t c =
        Colour(255, 255, 255, (uint8_t)(alpha * 255.0f)).PlatformColour();
    Shaded2DVertex verts[4] = {
        { -1.0f, -1.0f, 0.0f,  0.0f, 1.0f,  c },
        {  1.0f, -1.0f, 0.0f,  1.0f, 1.0f,  c },
        { -1.0f,  1.0f, 0.0f,  0.0f, 0.0f,  c },
        {  1.0f,  1.0f, 0.0f,  1.0f, 0.0f,  c },
    };
    Matrix44 identity;

    // Port specific: fullscreen quad, no binary counterpart -- enable blend explicitly.
    // Relies on no ambient GL_BLEND state (per-draw DrawQuad no longer leaves it on).
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    DrawShaded2D(verts, 4, (int)sizeof(Shaded2DVertex),
                 (int)offsetof(Shaded2DVertex, x),
                 (int)offsetof(Shaded2DVertex, u),
                 (int)offsetof(Shaded2DVertex, color),
                 GL_TRIANGLE_STRIP, tex, identity);
}

// ASM-spec v1.6.1 Mesh::DrawQuadUnCached @0x00240a70: UV args are (uMin,uMax,vMin,vMax), U-pair then V-pair.
void Renderer::DrawQuad(const Colour& tint, float uMin, float uMax, float vMin, float vMax) {
    // Unit quad (-0.5..0.5) transformed by current matrix stack MVP.
    // Vertex UV table matches binary DrawQuadUnCached: BL=(uMin,vMax), BR=(uMax,vMax), TL=(uMin,vMin), TR=(uMax,vMin).
    // The old glColor4ub(tint) uniform tint is now per-vertex a_color -- same
    // texel * colour modulate the GL_MODULATE texenv performed.
    const uint32_t c = tint.PlatformColour();
    Shaded2DVertex verts[4] = {
        { -0.5f, -0.5f, 0.0f,  uMin, vMax,  c },  // BL
        {  0.5f, -0.5f, 0.0f,  uMax, vMax,  c },  // BR
        { -0.5f,  0.5f, 0.0f,  uMin, vMin,  c },  // TL
        {  0.5f,  0.5f, 0.0f,  uMax, vMin,  c },  // TR
    };
    Matrix44 mvp = MatrixManager::GetInstance().GetMVP();

    // Defensive check: if no texture has been Set() recently, GL falls back
    // to default texture 0, which samples white -- modulating the tint then
    // produces a stray white quad. Tracked via Mortar::Texture::s_LastBound.
    // Once-warn + skip-draw so callers can identify the bug from logs.
#if !defined(__bada__)
    if (Mortar::Texture::s_LastBoundTexId == 0) {
        static bool s_warned = false;
        if (!s_warned) {
            LOG_WARN("RENDERER/DrawQuad",
                "no texture bound; tint=(%u,%u,%u,%u) uv=(%g,%g..%g,%g) -- caller missing Texture::Set?",
                tint.r, tint.g, tint.b, tint.a, uMin, vMin, uMax, vMax);
            s_warned = true;
        }
        return;
    }
#endif

    // ASM-spec v1.6.1 Mesh::DrawQuadUnCached @0x00240a70: 2D quad sets cull off +
    // per-draw blend -- OFF only if tint.a==255 && (no texture || texture has no alpha),
    // else ON. No trailing restore -- every draw path owns its own state (this replaces
    // relying on a global BeginFrame-time glEnable(GL_BLEND) that other draws, e.g.
    // Geometry::Render, correctly disable per-draw and would otherwise leak into here).
    glDisable(GL_CULL_FACE);
    if (tint.a == 255 && (!Mortar::Texture::s_CurrentlySetTexture ||
                          !Mortar::Texture::s_CurrentlySetTexture->m_HasAlpha)) {
        glDisable(GL_BLEND);
    } else {
        glEnable(GL_BLEND);
    }
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // tex=0: sample whatever the caller bound on unit 0 (Texture::Set, or
    // draw_sprite's raw glBindTexture) -- same contract as the FF path.
    DrawShaded2D(verts, 4, (int)sizeof(Shaded2DVertex),
                 (int)offsetof(Shaded2DVertex, x),
                 (int)offsetof(Shaded2DVertex, u),
                 (int)offsetof(Shaded2DVertex, color),
                 GL_TRIANGLE_STRIP, 0, mvp);
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

void Renderer::DrawColorQuad(const Colour& tint) {
    // Untextured in the FF path (texture disabled -> fragment = tint).
    // Shader path: bind the 1x1 white texture so texture2D samples 1.0 and
    // the vertex colour passes through -- identical output.
    const uint32_t c = tint.PlatformColour();
    Shaded2DVertex verts[4] = {
        { -0.5f, -0.5f, 0.0f,  0.0f, 0.0f,  c },
        {  0.5f, -0.5f, 0.0f,  0.0f, 0.0f,  c },
        { -0.5f,  0.5f, 0.0f,  0.0f, 0.0f,  c },
        {  0.5f,  0.5f, 0.0f,  0.0f, 0.0f,  c },
    };
    Matrix44 mvp = MatrixManager::GetInstance().GetMVP();

    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    DrawShaded2D(verts, 4, (int)sizeof(Shaded2DVertex),
                 (int)offsetof(Shaded2DVertex, x),
                 (int)offsetof(Shaded2DVertex, u),
                 (int)offsetof(Shaded2DVertex, color),
                 GL_TRIANGLE_STRIP, m_WhiteTex, mvp);
}

void Renderer::draw_sprite(GLuint tex, float x, float y, float w, float h,
                           float angle, float alpha) {
    MatrixStack& stack = MatrixManager::GetInstance().GetWorldStack();
    stack.Reset();
    Matrix44 mat = Matrix44::MakeScale(w, h, 1.0f);
    if (angle != 0.0f) {
        mat.RotZ44(sinf(angle), cosf(angle));
    }
    mat.GlobalTranslate44(_Vector3<float>(x + w * 0.5f, y + h * 0.5f, 0.0f));
    stack.SetCurrentMatrix(mat);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    DrawQuad(Colour(255, 255, 255, (uint8_t)(alpha * 255.0f)));
}

// Matches DrawTriList (0x00240e34) — QUADCUSTOMVERTEX stride 0x24 with
// per-vertex RGBA colour in `verts->colour` (Colour::PlatformColour packing).
void Renderer::DrawTriList(QUADCUSTOMVERTEX* verts, int vertCount, bool setBlendFunc) {
    Matrix44 mvp = MatrixManager::GetInstance().GetMVP();

    // Binary Mesh::DrawTris @0x240c30: glState<2884,false> (cull off) + glState<3042,true>
    // (blend on, primType!=0). Blade/2D-layer alpha blend; opaque geometry (vertex/texel
    // alpha=255) is unaffected by enabling blend.
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    // v1.6.1 Mesh::DrawTris @0x240c30 toggles GL_BLEND only, never glBlendFunc;
    // particle caller owns the func (Material::Set).
    if (setBlendFunc) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    // tex=0: sample the caller's unit-0 binding (Texture::Set) -- the FF
    // path never re-bound here either.
    DrawShaded2D(verts, vertCount, (int)sizeof(QUADCUSTOMVERTEX),
                 (int)offsetof(QUADCUSTOMVERTEX, x),
                 (int)offsetof(QUADCUSTOMVERTEX, u),
                 (int)offsetof(QUADCUSTOMVERTEX, colour),
                 GL_TRIANGLES, 0, mvp);
}

void Renderer::DrawTriStrip(QUADCUSTOMVERTEX* verts, int vertCount) {
    Matrix44 mvp = MatrixManager::GetInstance().GetMVP();

    // Tex-env note (was: "do NOT TexEnvModulate here"): GLES2 has no texenv;
    // the Quad2D shader always computes texture2D * v_color (MODULATE). The
    // per-texture REPLACE-vs-MODULATE distinction that v1.6.1
    // Texture2D_Bada::Set @0x229788 owned no longer applies. Per the
    // ASM-verified note in gl_funcs.h, the blade's GL_COMBINE env resolves to
    // texture.rgb x vertex.rgb with vertex alpha (== MODULATE, blade texel
    // alpha is uniformly opaque), so the fixed modulate shader is equivalent
    // for the blade path too.

    // Binary Mesh::DrawTris @0x240c30: glState<2884,false> (cull off) + glState<3042,true>
    // (blend on, primType!=0). Blade/2D-layer alpha blend; opaque geometry (vertex/texel
    // alpha=255) is unaffected by enabling blend.
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    DrawShaded2D(verts, vertCount, (int)sizeof(QUADCUSTOMVERTEX),
                 (int)offsetof(QUADCUSTOMVERTEX, x),
                 (int)offsetof(QUADCUSTOMVERTEX, u),
                 (int)offsetof(QUADCUSTOMVERTEX, colour),
                 GL_TRIANGLE_STRIP, 0, mvp);
}
