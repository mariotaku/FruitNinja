// Port specific: Wii-native GX implementation of the Renderer backend.
//
// Companion to RendererGL.cpp (the GL/GLES2 backend) -- same class
// (`Renderer`), same public method signatures (Renderer.h), same private
// member access (m_Quad2D / m_Mesh3D / m_QuadVBO / m_WhiteTex). GX has no
// binary counterpart (the Bada original is fixed-function OpenGL ES 1.x), so
// every method here is a from-scratch Wii port decision -- no binary
// fidelity applies, all of it is `// Port specific:`.
//
// WHY A SEPARATE proj/modelview LOAD (not the gl_funcsWii.cpp shim path):
// gl_funcsWii.cpp's glUniformMatrix4fv shim jams the app's already-combined
// MVP into GX's position-matrix slot and fakes an identity "pass-through"
// projection (see its SetupGxDrawState comment). That works for the affine
// 2D-ortho UI (4th row of the MVP is [0,0,0,1], so a 3x4 affine copy loses
// nothing) but silently mangles anything with real perspective: 3D fruit/
// mesh draws carry a nonzero perspective row, and GX_LoadProjectionMtx only
// extracts ~6 frustum coefficients out of a projection matrix -- it does not
// accept an arbitrary combined 4x4, and a 3x4 position matrix has no `w` row
// to carry the divide at all. The symptom is warped/collapsed 3D geometry.
//
// RendererGX fixes this at the source: it never touches the `mvp` parameter
// GL-shader-style TUs pass in. Instead it reads the SEPARATE projection and
// modelview (view*world) matrices straight out of MatrixManager -- the same
// matrices the caller multiplied together to produce that `mvp` just before
// the draw call -- and loads them into GX's two independent slots:
//   1. Modelview (view*world) -> GX_PNMTX0 as a 3x4 affine position matrix
//      (exact: modelview is always affine, only the projection carries the
//      perspective divide).
//   2. Projection -> reconstructed as a native GX projection (GX_PERSPECTIVE
//      or GX_ORTHOGRAPHIC, correct [-1,0] clip-z) via libogc's guFrustum /
//      guOrtho, with the frustum parameters recovered algebraically from the
//      GL-convention projection matrix's coefficients. GX then does the
//      perspective divide in hardware, exactly like it would for any native
//      GX title -- no CPU-side divide, no lost precision, no warped meshes.
//
// guFrustum/guOrtho were chosen over the "load the GL matrix + pre-multiply
// a z-remap" fallback the task spec allows: the frustum-recovery route
// produces a GX projection matrix built the way libogc expects (verified
// correct for both GX_PERSPECTIVE and GX_ORTHOGRAPHIC clip conventions),
// whereas hand-remapping a foreign GL matrix's z/w rows risks subtly wrong
// near/far clipping that would only show up as a hard-to-diagnose clip-plane
// bug later. If frustum recovery ever proves fragile for some projection
// this port issues, fall back to the z-remap approach described in the task
// spec -- not implemented here because guFrustum/guOrtho covers every
// projection MatrixManager::SetupPerspective / SetupOrtho actually builds.
//
// Texture upload/state (ShimTexture registry, GX_TexObj creation, TileRGBA8)
// stays entirely in gl_funcsWii.cpp -- RendererGX only BINDS an
// already-uploaded GX_TexObj via the new Wii_GetTexObj accessor. Likewise
// vertex/index buffer bytes are read back out of the shim's buffer registry
// via Wii_GetBufferData (extended in this pass to retain buffer bytes after
// upload -- see gl_funcsWii.cpp/.h changes noted at the top of that file).
//
// Only compiled when FRUIT_PLATFORM_WII is set (see src/engine/CMakeLists.txt).
// NOT wired into CMake this pass -- RendererGL.cpp remains the active Wii
// TU until a later pass switches the target.
#ifdef FRUIT_PLATFORM_WII

#include "render/Renderer.h"
#include "render/RendererInternal.h"
#include "render/MatrixManager.h"
#include "debug/Logger.h"

#include <gccore.h>
#include <ogc/gu.h>
#include <cstring>
#include <cmath>
#include <cstdint>

#include "render/gl_funcsWii.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

// Column-major GL 4x4 (Matrix44::ptr() layout, m[col*4+row]) -> GX row-major
// 3x4 affine position matrix. Only valid for affine input (row 3 == [0,0,0,1]);
// modelview (view*world) is always affine so this is exact, never an
// approximation. Same transpose the gl_funcsWii.cpp shim uses for its 2D path.
void Gl44ToGxPosMtx(const Matrix44& modelview, Mtx mv) {
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c) {
            mv[r][c] = modelview.m[c * 4 + r];
        }
    }
}

// True when `proj` has a nonzero perspective divide row (col-major GL layout:
// row 3 is indices [3,7,11,15]). An orthographic projection's row 3 is
// exactly [0,0,0,1]; a perspective projection's m[11] is -1 (GL convention).
bool IsPerspective(const Matrix44& proj) {
    return proj.m[11] != 0.0f;
}

// Recover a symmetric perspective frustum (fovy/aspect/near/far) from a GL
// column-major perspective projection and load it into `out` as a native GX
// projection via guPerspective. GL perspective coefficients (see
// _Matrix44::OrthoW's sibling -- the perspective-building equivalent used by
// MatrixManager::SetupPerspective):
//   m[0]  = 2n / (r-l)            m[5]  = 2n / (t-b)
//   m[8]  = (r+l) / (r-l)         m[9]  = (t+b) / (t-b)
//   m[10] = -(f+n) / (f-n)        m[11] = -1
//   m[14] = -2fn / (f-n)
// MatrixManager::SetupPerspective always builds a SYMMETRIC frustum (l=-r,
// b=-t), so m[8]==m[9]==0 in every projection this port issues; fovy/aspect
// recovery from m[5]/m[0] alone is exact for that case. near/far come from
// m[10]/m[14] (2 equations, 2 unknowns).
void LoadGxPerspective(const Matrix44& proj, Mtx44 out) {
    float m0  = proj.m[0];
    float m5  = proj.m[5];
    float m10 = proj.m[10];
    float m14 = proj.m[14];

    // From m[5] = 2n/(t-b) with symmetric b=-t: m[5] = n/t -> t = n / m5.
    // From m[10] = -(f+n)/(f-n) and m[14] = -2fn/(f-n):
    //   f-n = -2fn/m14  ;  f+n = -m10*(f-n) = 2*m10*fn/m14
    //   Solve: n = m14 / (m10 - 1), f = m14 / (m10 + 1)   [standard GL-frustum inverse]
    float n = m14 / (m10 - 1.0f);
    float f = m14 / (m10 + 1.0f);
    float top = n / m5;
    float aspect = m5 / m0;   // (2n/aspect/w-scale)/(2n/h-scale) -> h/w = fovy/aspect relation

    float fovyRad = 2.0f * atanf(top / n);
    float fovyDeg = fovyRad * (180.0f / (float)M_PI);

    guPerspective(out, fovyDeg, aspect, n, f);
}

// Recover an orthographic box (l,r,b,t,n,f) from a GL column-major ortho
// projection and load it into `out` via guOrtho. GL ortho coefficients
// (matches _Matrix44::OrthoW above, same convention MatrixManager::SetupOrtho
// uses):
//   m[0]=2/(r-l)  m[5]=2/(t-b)  m[10]=1/(f-n) [note: OrthoW's sign, see below]
//   m[12]=-(r+l)/(r-l)  m[13]=-(t+b)/(t-b)  m[14]=n/(n-f)
// _Matrix44::OrthoW (the builder MatrixManager::SetupOrtho/SetupPerspective's
// sibling actually uses in this port) writes m[10]=1/(far-near) and
// m[14]=near/(near-far) -- NOT the canonical GL -2/(f-n) form -- so recover
// against those exact coefficients rather than textbook GL ortho.
void LoadGxOrtho(const Matrix44& proj, Mtx44 out) {
    float invRL = proj.m[0] * 0.5f;   // = 1/(r-l)
    float invTB = proj.m[5] * 0.5f;   // = 1/(t-b)
    float rl = proj.m[0] != 0.0f ? 1.0f / invRL : 0.0f; // (r-l)
    float tb = proj.m[5] != 0.0f ? 1.0f / invTB : 0.0f; // (t-b)

    // m[12] = -(r+l)/(r-l)  =>  r+l = -m[12]*(r-l)
    float rPlusL = -proj.m[12] * rl;
    float tPlusB = -proj.m[13] * tb;
    float right = (rl + rPlusL) * 0.5f;
    float left  = right - rl;
    float top   = (tb + tPlusB) * 0.5f;
    float bottom = top - tb;

    // m[10] = 1/(far-near), m[14] = near/(near-far) = -near/(far-near)
    float farMinusNear = (proj.m[10] != 0.0f) ? (1.0f / proj.m[10]) : 0.0f;
    float near_ = -proj.m[14] * farMinusNear;
    float far_  = near_ + farMinusNear;

    guOrtho(out, top, bottom, left, right, near_, far_);
}

// Loads MatrixManager's current projection + modelview (view*world) into GX's
// two independent matrix slots, replacing the combined-MVP-as-posmtx approach
// the gl_funcsWii.cpp shim uses. Must be called before GX_Begin for every
// DrawShaded2D / DrawMesh3D.
void LoadGxMatricesFromMatrixManager() {
    MatrixManager& mm = MatrixManager::GetInstance();
    const Matrix44& proj = mm.GetProjectionStack().m_Current;
    Matrix44 modelview = mm.GetViewStack().m_Current * mm.GetWorldStack().m_Current;

    Mtx mv;
    Gl44ToGxPosMtx(modelview, mv);
    GX_LoadPosMtxImm(mv, GX_PNMTX0);
    GX_SetCurrentMtx(GX_PNMTX0);

    Mtx44 gxProj;
    if (IsPerspective(proj)) {
        LoadGxPerspective(proj, gxProj);
        GX_LoadProjectionMtx(gxProj, GX_PERSPECTIVE);
    } else {
        LoadGxOrtho(proj, gxProj);
        GX_LoadProjectionMtx(gxProj, GX_ORTHOGRAPHIC);
    }
}

// GL primitive-mode -> GX primitive-type. Mirrors gl_funcsWii.cpp's GxPrim.
u8 GxPrimFromGL(GLenum mode) {
    switch (mode) {
        case GL_TRIANGLES:      return GX_TRIANGLES;
        case GL_TRIANGLE_STRIP: return GX_TRIANGLESTRIP;
        case GL_TRIANGLE_FAN:   return GX_TRIANGLEFAN;
        default:                return GX_TRIANGLES;
    }
}

// Sets up the GX vertex descriptor, attribute formats, colour channel and TEV
// stage for a draw. `haveUV` gates whether TEX0 is in the descriptor at all
// (GX errors if a texgen references a coordinate the descriptor doesn't
// carry); `textured` additionally gates GX_MODULATE vs GX_PASSCLR.
void SetupGxVertexAndTev(bool haveUV, bool textured) {
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);

    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);

    if (haveUV) {
        GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
        GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    }

    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX,
                   0, GX_DF_NONE, GX_AF_NONE);

    if (textured) {
        GX_SetNumTexGens(1);
        GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
        GX_SetNumTevStages(1);
        GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
        GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    } else {
        GX_SetNumTexGens(0);
        GX_SetNumTevStages(1);
        GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
        GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    }
}

// Blend mode matching RendererGL::DrawShaded2D / DrawMesh3D -- both rely on
// GL_BLEND being left enabled by the caller with the standard
// (SRCALPHA, INVSRCALPHA) factors (see Renderer::DrawTriList's setBlendFunc
// comment: most callers rely on this default). GX has no per-draw enable
// flag separate from the blend mode itself, so just (re)issue the mode here.
void SetStandardAlphaBlend() {
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
}

// One logical vertex's worth of pos/uv/colour, read out of CPU-side bytes at
// `stride`-separated offsets -- same interleaved-vertex convention
// DrawShaded2D/DrawMesh3D's GL siblings use (Shaded2DVertex / QUADCUSTOMVERTEX
// layouts, byte offsets passed in by the caller).
void EmitInterleavedVertex(const unsigned char* base, int stride,
                           int posOff, bool haveUV, int uvOff,
                           bool haveColor, int colOff) {
    const float* p = (const float*)(base + posOff);
    GX_Position3f32(p[0], p[1], p[2]);

    if (haveColor) {
        // Packed uint32 (Colour::PlatformColour() -- LE bytes r,g,b,a), same
        // as gl_funcsWii.cpp's EmitVertex: reconstruct via memcpy so this is
        // correct regardless of host endianness rather than reading 4 raw
        // bytes positionally.
        uint32_t v;
        memcpy(&v, base + colOff, 4);
        GX_Color4u8((u8)(v & 0xFFu), (u8)((v >> 8) & 0xFFu),
                   (u8)((v >> 16) & 0xFFu), (u8)((v >> 24) & 0xFFu));
    } else {
        GX_Color4u8(255, 255, 255, 255);
    }

    if (haveUV) {
        const float* uv = (const float*)(base + uvOff);
        GX_TexCoord2f32(uv[0], uv[1]);
    }
}

} // namespace

// Port specific: Wii boot -- GX FIFO/video are already brought up by
// mainWii.cpp before the render loop starts, so init() only needs the same
// non-GL bookkeeping RendererGL::init() does (instance pointer, shader
// "program" handles via the gl_funcsWii.cpp no-op stubs, the streaming quad
// VBO id, and the 1x1 white texture used by DrawColorQuad/untextured meshes).
// ShaderProgram::Compile talks to the shim's no-op glCreateShader/
// glLinkProgram stubs (GX has no GLSL -- TEV stages replace it, set up per
// draw in SetupGxVertexAndTev), so it always "succeeds" here; kept for API
// parity so Renderer.cpp's shared code (DrawQuad/DrawTriList/etc., which
// route through DrawShaded2D/DrawMesh3D) doesn't need a Wii-specific branch.
bool Renderer::init() {
    s_instance = this;

    if (!m_Quad2D.Compile(nullptr, nullptr)) {
        LOG_ERROR("RENDERER/init", "GX: 2D shader stub failed to build");
        return false;
    }
    if (!m_Mesh3D.Compile(nullptr, nullptr)) {
        LOG_ERROR("RENDERER/init", "GX: 3D mesh shader stub failed to build");
        m_Quad2D.Destroy();
        return false;
    }

    glGenBuffers(1, &m_QuadVBO);

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

// Port specific: GX FIFO/video teardown is owned by mainWii.cpp; this only
// releases the shim-side handles init() allocated (mirrors RendererGL::shutdown).
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

// Port specific: GX equivalents of RendererGL::InitGL's one-time GL state
// (viewport, blend mode, depth). GX's viewport origin is already handled by
// mainWii's boot setup (see gl_funcsWii.cpp's glViewport TODO note); this
// call additionally seeds the scissor to full-EFB and the default Z-mode.
// Depth-test default matches the GL path (enabled, LEQUAL -- see
// DrawMesh3D's per-draw toggle note below for why 2D vs 3D differ).
void Renderer::InitGL(int width, int height) {
    GX_SetViewport(0.0f, 0.0f, (f32)width, (f32)height, 0.0f, 1.0f);
    GX_SetScissor(0, 0, (u32)width, (u32)height);
    GX_SetCullMode(GX_CULL_BACK);
    SetStandardAlphaBlend();
    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
}

// Port specific: 2D quad/UI draw path (DrawQuad / DrawColorQuad /
// draw_fullscreen_quad / DrawTriList / DrawTriStrip all funnel here via
// Renderer.cpp's shared code). Ignores the passed `mvp` -- see the file-top
// comment for why -- and instead loads MatrixManager's current
// projection/modelview split into GX directly. 2D UI draws are unlit and
// typically depth-test-off in the binary's fixed-function equivalent
// (BeginFrame toggles depth per phase); RendererGL doesn't touch depth state
// inside DrawShaded2D either (caller's responsibility per Renderer.h), so
// this mirrors that -- no GX_SetZMode call here.
void Renderer::DrawShaded2D(const void* verts, int vertCount, int stride,
                            int posOff, int uvOff, int colOff,
                            GLenum prim, GLuint tex, const Matrix44& /*mvp*/) {
    if (vertCount <= 0) return;

    LoadGxMatricesFromMatrixManager();

    // Renderer.h contract: tex==0 means "use whatever is already bound on
    // unit 0" (RendererGL::DrawShaded2D skips glBindTexture entirely in that
    // case, leaving the caller's prior Texture::Set/glBindTexture in place --
    // see DrawQuad's tex=0 call, the common textured-quad path). Falling back
    // to "untextured" here was the bug: every DrawQuad-routed draw (wood
    // background, logo, banner, ring-label plates) passes tex=0 and rendered
    // solid white (GX_PASSCLR = vertex colour only, which is opaque white for
    // an untinted quad) instead of sampling the bound texture.
    GLuint boundTex = tex ? tex : Wii_GetBoundTexture();
    bool textured = false;
    if (boundTex) {
        GXTexObj* texObj = Wii_GetTexObj(boundTex);
        if (texObj) {
            GX_LoadTexObj(texObj, GX_TEXMAP0);
            textured = true;
        }
    }

    SetStandardAlphaBlend();
    SetupGxVertexAndTev(/*haveUV=*/true, textured);

    const unsigned char* base = (const unsigned char*)verts;
    GX_Begin(GxPrimFromGL(prim), GX_VTXFMT0, (u16)vertCount);
    for (int i = 0; i < vertCount; ++i) {
        EmitInterleavedVertex(base + (size_t)stride * i, stride,
                              posOff, /*haveUV=*/true, uvOff,
                              /*haveColor=*/true, colOff);
    }
    GX_End();
}

// Port specific: 3D mesh draw path (Geometry::Render). Same matrix-load
// discipline as DrawShaded2D -- MatrixManager's projection/modelview split,
// not the passed `mvp` -- but the geometry lives in the gl_funcsWii.cpp shim's
// buffer-object registry (glGenBuffers/glBufferData), so the vertex (and
// optional index) bytes are pulled back out via the Wii_GetBufferData
// accessor added in this pass. 3D meshes DO need real depth testing (unlike
// the 2D path above) since fruit/background geometry must occlude correctly;
// RendererGL leaves depth state to the caller too, but the binary's
// equivalent (Geometry::Render under the fixed pipeline) always drew with
// depth-test+write on for 3D, so it's set explicitly here rather than trusted
// to whatever the previous 2D draw left GX in.
void Renderer::DrawMesh3D(GLuint vbo, GLuint ibo, int vertCount, int indexCount, GLenum prim,
                          int stride, int posOff, int uvOff, int colOff, int uvSize, int colSize,
                          GLuint tex, const Matrix44& /*mvp*/) {
    if (vertCount <= 0) return;

    unsigned vboSize = 0;
    const unsigned char* vboData = (const unsigned char*)Wii_GetBufferData(vbo, &vboSize);
    if (!vboData) return;

    LoadGxMatricesFromMatrixManager();

    GLuint boundTex = tex ? tex : m_WhiteTex;
    bool haveUV = uvSize > 0;
    bool haveColor = colSize > 0;
    bool textured = false;
    if (boundTex && haveUV) {
        GXTexObj* texObj = Wii_GetTexObj(boundTex);
        if (texObj) {
            GX_LoadTexObj(texObj, GX_TEXMAP0);
            textured = true;
        }
    }
    if (!textured) haveUV = false;

    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    SetStandardAlphaBlend();
    SetupGxVertexAndTev(haveUV, textured);

    if (ibo && indexCount > 0) {
        unsigned iboSize = 0;
        const unsigned char* iboData = (const unsigned char*)Wii_GetBufferData(ibo, &iboSize);
        if (!iboData) return;
        // Renderer.h contract: DrawMesh3D's index buffer is always
        // GL_UNSIGNED_SHORT (matches RendererGL::DrawMesh3D's glDrawElements
        // call, which hardcodes GL_UNSIGNED_SHORT -- see RendererGL.cpp).
        const unsigned short* indices = (const unsigned short*)iboData;

        GX_Begin(GxPrimFromGL(prim), GX_VTXFMT0, (u16)indexCount);
        for (int i = 0; i < indexCount; ++i) {
            unsigned short vi = indices[i];
            EmitInterleavedVertex(vboData + (size_t)stride * vi, stride,
                                  posOff, haveUV, uvOff, haveColor, colOff);
        }
        GX_End();
    } else {
        GX_Begin(GxPrimFromGL(prim), GX_VTXFMT0, (u16)vertCount);
        for (int i = 0; i < vertCount; ++i) {
            EmitInterleavedVertex(vboData + (size_t)stride * i, stride,
                                  posOff, haveUV, uvOff, haveColor, colOff);
        }
        GX_End();
    }
}

// Port specific: records the bound texture id; GX_LoadTexObj happens at draw
// time (DrawShaded2D/DrawMesh3D resolve `tex` -> GX_TexObj themselves), so
// this only needs to route through the shim's glBindTexture bookkeeping —
// matches RendererGL::BindTexture2D's raw-GLuint debug-overlay use case
// (Renderer.h: "no binary counterpart... raw GLuint textures, not
// Mortar::Texture").
void Renderer::BindTexture2D(uint32_t texId) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)texId);
}

// Port specific: world-space rect (same centered-ortho convention as
// RendererGL::SetClipRect / SetupGameOrtho) -> GX_SetScissor in EFB pixels.
// GX scissor origin is top-left (same as GL's glScissor once RendererGL
// converts through glGetIntegerv(GL_VIEWPORT), which is also stored
// top-left-agnostic there -- see gl_funcsWii.cpp's glScissor Y-flip TODO,
// which notes the flip is deferred pending DisplayManagerWii owning xfb
// height). Mirrors RendererGL::SetClipRect's pixel math using the shim's
// g_ShimViewport bookkeeping via glGetIntegerv/glViewport equivalents.
void Renderer::SetClipRect(float left, float top, float right, float bottom) {
    const float orthoW = 480.0f;
    const float orthoH = 320.0f;
    int vp[4];
    Wii_GetViewport(vp);
    const int vpX = vp[0], vpY = vp[1];
    const int vpW = vp[2], vpH = vp[3];

    int sx = (int)((left + orthoW * 0.5f) / orthoW * (float)vpW) + vpX;
    int sy = (int)((bottom + orthoH * 0.5f) / orthoH * (float)vpH) + vpY;
    int sw = (int)((right - left) / orthoW * (float)vpW);
    int sh = (int)((top - bottom) / orthoH * (float)vpH);
    if (sw < 0) sw = 0;
    if (sh < 0) sh = 0;

    GX_SetScissor((u32)sx, (u32)sy, (u32)sw, (u32)sh);
}

// Port specific: restores the full-EFB scissor (undoes SetClipRect).
void Renderer::ClearClipRect() {
    int vp[4];
    Wii_GetViewport(vp);
    GX_SetScissor((u32)vp[0], (u32)vp[1], (u32)vp[2], (u32)vp[3]);
}

// Port specific: GX has no polygon-mode wireframe toggle (no equivalent of
// GL_LINE fill mode for GX_Begin'd primitives short of re-emitting every
// batch as GX_LINESTRIP, which this pass doesn't implement). One-line no-op
// stub, same as gl_funcsWii.cpp's glPolygonMode shim.
void Renderer::SetWireframe(bool /*enabled*/) {
    // Port specific: no GX equivalent: the F2 debug wireframe toggle is a
    // no-op on the Wii backend.
}

#endif // FRUIT_PLATFORM_WII
