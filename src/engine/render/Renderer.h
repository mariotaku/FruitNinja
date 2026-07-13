#ifndef MORTAR_RENDERER_H
#define MORTAR_RENDERER_H

#include "render/gl_funcs.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "render/MatrixManager.h"
#include "render/ShaderProgram.h"
#include "math/Colour.h"
#include "core/MortarTypes.h"

// GL renderer — targets the same behaviour as the binary's ES 1.x pipeline.
//
// GLES2 migration phase 2: the 2D quad/UI draws (DrawQuad, DrawTriList,
// DrawTriStrip, DrawColorQuad, draw_fullscreen_quad) render through a real
// GLES2 shader program (Shaders.h / ShaderProgram.h) via DrawShaded2D.
// Everything that funnels through these (font, particles, HUD, screens)
// rides the shader path with them.
// Phase 3: the 3D mesh path (Geometry::Render) renders through the Mesh3D
// program via DrawMesh3D (same unlit texture2D * v_color modulate -- all
// meshes are IsLit=false). Both shader draws restore glUseProgram(0),
// disable their generic attrib arrays and unbind buffers after EVERY draw
// so any still-fixed-function path in the frame coexists.
struct Renderer {
    static Renderer* s_instance;
    static Renderer* GetInstance() { return s_instance; }

    Renderer();

    // Sets s_instance and creates the GL resources for the 2D shader path
    // (program, streaming VBO, 1x1 white texture). Requires a LIVE GL
    // context + gl_load_functions() already done. Returns false if the
    // shader program fails to compile/link (GL 2.0 unavailable).
    bool init();
    // Destroys the GL resources created by init(). Call while the GL
    // context is still current.
    void shutdown();

    // One-shot GL state initialisation, matches FruitNinja::InitGL
    // (binary 0x00181e54). Call once after the GL context and
    // function pointers are available, before the main render loop.
    void InitGL(int width, int height);

    // Setup ortho projection matching original game.
    // Verified constants: top=160, bottom=-160, left=-240, right=240.
    void SetupGameOrtho();

    // Draw a textured quad filling the entire screen (clip space).
    void draw_fullscreen_quad(GLuint tex, float alpha = 1.0f);

    // Matches DrawQuadUnCached — draws unit quad transformed by current MVP.
    // ASM-spec v1.6.1 Mesh::DrawQuadUnCached @0x00240a70: args are (uMin,uMax,vMin,vMax), U-pair then V-pair.
    void DrawQuad(const Colour& tint, float uMin = 0.0f, float uMax = 1.0f,
                  float vMin = 0.0f, float vMax = 1.0f);

    // Draw a textured sprite at game coordinates.
    void draw_sprite(GLuint tex, float x, float y, float w, float h,
                     float angle = 0.0f, float alpha = 1.0f);

    // Port specific: untextured tinted quad (binary inlines this in the crit-flash; no standalone symbol).
    void DrawColorQuad(const Colour& tint);

    // Port specific: no binary counterpart (GLES2 has no fixed-function user
    // clip planes; the original clips via CPU-side geometry math). Enables
    // GL_SCISSOR_TEST and sets glScissor from a WORLD-space rect in the same
    // centered-ortho convention as SetupGameOrtho (SetupOrtho(160,-160,-240,
    // 240,...); see docs/engine/coordinate-system.md). `top`/`bottom` are Y
    // world coords (top > bottom, +Y is up); `left`/`right` are X world coords.
    // No-op on __bada__ / FN_GL_STUB builds (glGetIntegerv(GL_VIEWPORT) isn't
    // available there). GL scissor is not stacked -- only one region can be
    // active; call ClearClipRect() to disable before drawing unclipped content.
    void SetClipRect(float left, float top, float right, float bottom);

    // Disables GL_SCISSOR_TEST (undoes SetClipRect). No-op on __bada__ / FN_GL_STUB.
    void ClearClipRect();

    // Port specific: no binary counterpart. Sets GL_TEXTURE_ENV_MODE = GL_MODULATE
    // on the active texture unit -- thin wrapper around TexEnvModulate() (gl_funcs.h)
    // so non-engine TUs (entities/, screens/) don't call raw GL directly.
    void SetTextureModulate();

    // Port specific: no binary counterpart. glActiveTexture(GL_TEXTURE0) +
    // glBindTexture(GL_TEXTURE_2D, texId) -- thin wrapper so debug-overlay code
    // (raw GLuint textures, not Mortar::Texture) doesn't call raw GL directly.
    void BindTexture2D(uint32_t texId);

    // Port specific: no binary counterpart. Wraps glPolygonMode(GL_FRONT_AND_BACK,
    // GL_LINE/GL_FILL) for the F2 wireframe debug toggle. No-op where glPolygonMode
    // is unavailable (GLES / Emscripten) -- same guard the call site used to have.
    void SetWireframe(bool enabled);

    // Path B rendering with QUADCUSTOMVERTEX (stride 0x24).
    // Matches original DrawTriList (0x00240e34) / DrawTriStrip.
    // setBlendFunc: binary Mesh::DrawTris @0x240c30 never touches glBlendFunc --
    // only the per-template Material sets it, before the draw call, and it must
    // survive the draw. Most callers don't set their own func and rely on the
    // default alpha blend below; pass false when the caller (e.g. additive
    // particle templates) has already set its own glBlendFunc.
    void DrawTriList(QUADCUSTOMVERTEX* verts, int vertCount, bool setBlendFunc = true);
    void DrawTriStrip(QUADCUSTOMVERTEX* verts, int vertCount);

    // GLES2 3D mesh draw (Geometry::Render). Draws a static VBO (+ optional
    // IBO, GL_UNSIGNED_SHORT indices) through the Mesh3D program with `mvp`
    // uploaded to u_mvp. Vertex layout is passed as plain ints (byte offsets
    // into `stride`-sized vertices) so the Renderer stays independent of
    // asset headers: pos = 3 floats @posOff (always present), uv = 2 floats
    // @uvOff when uvSize > 0, colour = 4 normalized ubytes @colOff when
    // colSize > 0. When a channel is absent its attrib array is disabled and
    // a constant generic value is set instead -- colour defaults to WHITE
    // (GLES2's built-in generic default is (0,0,0,1) opaque black, but the
    // fixed-function default colour this path replaces was white).
    // tex == 0 binds m_WhiteTex so texture2D samples 1.0 (untextured FF
    // behaviour). Blend / cull / depth state is the CALLER's responsibility.
    // Coexistence guarantee: exits via attrib 0/1/2 disable, both buffer
    // bindings cleared, glUseProgram(0) -- same contract as DrawShaded2D.
    void DrawMesh3D(GLuint vbo, GLuint ibo, int vertCount, int indexCount, GLenum prim,
                    int stride, int posOff, int uvOff, int colOff, int uvSize, int colSize,
                    GLuint tex, const Matrix44& mvp);

private:
    // Shared GLES2 draw for the 2D paths. Uploads `mvp` to u_mvp, streams
    // `verts` (vertCount * stride bytes) into m_QuadVBO (orphan + upload),
    // points attribs 0/1/2 (pos 3xfloat @posOff, uv 2xfloat @uvOff, colour
    // 4x normalized ubyte @colOff) and glDrawArrays(prim).
    // tex != 0: binds it on unit 0. tex == 0: keeps the caller's current
    // unit-0 binding (Texture::Set / draw_sprite's raw glBindTexture own it —
    // same contract the fixed-function path had).
    // Blend / cull / scissor state is the CALLER's responsibility, set before
    // calling (matches the old per-draw FF state decisions).
    // Coexistence guarantee: always exits via glUseProgram(0) + attrib 0/1/2
    // disable + GL_ARRAY_BUFFER unbind so the still-fixed-function paths
    // (Geometry::Render, matrix-stack uploads) see clean state.
    void DrawShaded2D(const void* verts, int vertCount, int stride,
                      int posOff, int uvOff, int colOff,
                      GLenum prim, GLuint tex, const Matrix44& mvp);

    ShaderProgram m_Quad2D;  // the Shaders.h Quad2D program
    ShaderProgram m_Mesh3D;  // the Shaders.h Mesh3D program (3D mesh path)
    GLuint m_QuadVBO;        // streaming VBO for DrawShaded2D
    GLuint m_WhiteTex;       // 1x1 opaque white — lets DrawColorQuad reuse the textured shader
};

#endif
