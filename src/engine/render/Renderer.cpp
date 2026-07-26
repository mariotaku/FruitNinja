#include "render/Renderer.h"
#include "render/RendererInternal.h"
#include "render/MatrixManager.h"
#include "asset/Texture.h"
#include "debug/Logger.h"
#include <cstddef>
#include <cstdio>
#include <cstring>
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
//
// This file holds the portable public API (ortho setup + quad-vertex
// builders) that feeds DrawShaded2D. The GL/GLES2-internal draw calls
// (init/shutdown/InitGL/DrawShaded2D/DrawMesh3D/BindTexture2D/SetClipRect/
// ClearClipRect/SetWireframe) live in RendererGL.cpp -- link-time split,
// mirrors the GameSDL.cpp/GameWii.cpp platform convention. Renderer.h is
// unchanged; both TUs implement the same class.

Renderer* Renderer::s_instance = nullptr;

#if defined(FRUIT_PLATFORM_WII)
Renderer::Renderer() : m_WhiteTexBuf(nullptr), m_WhiteTex(kWhiteTexSentinel) {}
#else
Renderer::Renderer() : m_QuadVBO(0), m_WhiteTex(0), m_RingCursor(0) {
    memset(&m_GLState, 0, sizeof(m_GLState));
}
#endif

// ---------------------------------------------------------------------------
// Port specific: GL state shadow helpers (see Renderer.h "state cache" doc).
// Portable bodies so shared call sites (Geometry, particles, texture/font
// upload paths) link on every backend; on Wii they forward to the GX shim
// exactly as the raw calls they replaced did.
// ---------------------------------------------------------------------------

void Renderer::SetBlendEnabled(bool enabled) {
#if defined(FRUIT_PLATFORM_WII)
    if (enabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
#else
    if (!m_GLState.blendValid || m_GLState.blendOn != enabled) {
        if (enabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        m_GLState.blendOn = enabled;
        m_GLState.blendValid = true;
    }
#endif
}

void Renderer::SetCullFaceEnabled(bool enabled) {
#if defined(FRUIT_PLATFORM_WII)
    if (enabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
#else
    if (!m_GLState.cullValid || m_GLState.cullOn != enabled) {
        if (enabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
        m_GLState.cullOn = enabled;
        m_GLState.cullValid = true;
    }
#endif
}

void Renderer::BindTextureForUpload(uint32_t texId) {
#if defined(FRUIT_PLATFORM_WII)
    glBindTexture(GL_TEXTURE_2D, (GLuint)texId);
#else
    glBindTexture(GL_TEXTURE_2D, (GLuint)texId);
    m_GLState.boundTex = (GLuint)texId;
    m_GLState.boundTexValid = true;
    m_GLState.requestedTex = texId;   // mirror the legacy eager binding
#endif
}

void Renderer::NotifyTextureDeleted(uint32_t texId) {
#if defined(FRUIT_PLATFORM_WII)
    (void)texId;
#else
    // GL rebinds 0 when the currently-bound texture is deleted; mirror that,
    // and never leave the requested sampling texture pointing at a name that
    // may get recycled by a later glGenTextures.
    if (m_GLState.boundTexValid && m_GLState.boundTex == (GLuint)texId) {
        m_GLState.boundTex = 0;
    }
    if (m_GLState.requestedTex == texId) {
        m_GLState.requestedTex = 0;
    }
#endif
}

void Renderer::InvalidateStateCache() {
#if !defined(FRUIT_PLATFORM_WII)
    m_GLState.programValid  = false;
    m_GLState.boundTexValid = false;
    m_GLState.blendValid    = false;
    m_GLState.cullValid     = false;
    m_GLState.mvp2DValid    = false;
    m_GLState.mvp3DValid    = false;
    m_GLState.ringReady     = false;
    // The only texture unit the port ever samples from; re-assert in case
    // external GL code moved it.
    glActiveTexture(GL_TEXTURE0);
#endif
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
    SetBlendEnabled(true);

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

    // tex=0 (no Texture::Set since the last bind) is the binary's untextured
    // flat-colour quad: fixed-function "texturing off" -> fragment = tint.
    // DrawCritHit's super-fruit crit-flash draws exactly this way (Mesh::
    // DrawQuadUnCached with texturing disabled). Route it through the same
    // 1x1 white texture DrawColorQuad uses so texel(1)*tint == tint --
    // matches the FF output instead of dropping the draw.
    GLuint quadTex = 0;
    if (Mortar::Texture::s_LastBoundTexId == 0) {
        quadTex = m_WhiteTex;
    }

    // ASM-spec v1.6.1 Mesh::DrawQuadUnCached @0x00240a70: 2D quad sets cull off +
    // per-draw blend -- OFF only if tint.a==255 && (no texture || texture has no alpha),
    // else ON. No trailing restore -- every draw path owns its own state (this replaces
    // relying on a global BeginFrame-time glEnable(GL_BLEND) that other draws, e.g.
    // Geometry::Render, correctly disable per-draw and would otherwise leak into here).
    SetCullFaceEnabled(false);
    SetBlendEnabled(!(tint.a == 255 && (!Mortar::Texture::s_CurrentlySetTexture ||
                                        !Mortar::Texture::s_CurrentlySetTexture->m_HasAlpha)));

    // quadTex != 0 (i.e. no texture bound): m_WhiteTex forces flat colour.
    // quadTex == 0: sample whatever the caller requested via BindTexture2D
    // (Texture::Set, draw_sprite) -- same contract as the FF path.
    DrawShaded2D(verts, 4, (int)sizeof(Shaded2DVertex),
                 (int)offsetof(Shaded2DVertex, x),
                 (int)offsetof(Shaded2DVertex, u),
                 (int)offsetof(Shaded2DVertex, color),
                 GL_TRIANGLE_STRIP, quadTex, mvp);
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

    SetCullFaceEnabled(false);
    SetBlendEnabled(true);

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

    BindTexture2D((uint32_t)tex);
    DrawQuad(Colour(255, 255, 255, (uint8_t)(alpha * 255.0f)));
}

// Matches DrawTriList (0x00240e34) — QUADCUSTOMVERTEX stride 0x24 with
// per-vertex RGBA colour in `verts->colour` (Colour::PlatformColour packing).
void Renderer::DrawTriList(QUADCUSTOMVERTEX* verts, int vertCount, bool setBlendFunc) {
    Matrix44 mvp = MatrixManager::GetInstance().GetMVP();

    // Binary Mesh::DrawTris @0x240c30: glState<2884,false> (cull off) + glState<3042,true>
    // (blend on, primType!=0). Blade/2D-layer alpha blend; opaque geometry (vertex/texel
    // alpha=255) is unaffected by enabling blend.
    SetCullFaceEnabled(false);
    SetBlendEnabled(true);
    // setBlendFunc is inert: the func is the init-time constant everywhere
    // (see the Renderer.h doc for this method).
    (void)setBlendFunc;

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
    SetCullFaceEnabled(false);
    SetBlendEnabled(true);

    DrawShaded2D(verts, vertCount, (int)sizeof(QUADCUSTOMVERTEX),
                 (int)offsetof(QUADCUSTOMVERTEX, x),
                 (int)offsetof(QUADCUSTOMVERTEX, u),
                 (int)offsetof(QUADCUSTOMVERTEX, colour),
                 GL_TRIANGLE_STRIP, 0, mvp);
}
