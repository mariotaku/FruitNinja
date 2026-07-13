#ifndef MORTAR_RENDERER_H
#define MORTAR_RENDERER_H

#include "render/gl_funcs.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "render/MatrixManager.h"
#include "math/Colour.h"
#include "core/MortarTypes.h"

// Fixed-function GL renderer — targets the same pipeline as the binary
// (ES 1.x / desktop-GL compatibility profile). No shaders, no attrib
// locations, no programs. Every draw call goes through
// glMatrixMode / glLoadMatrixf / glEnableClientState / glVertexPointer
// and friends.
struct Renderer {
    static Renderer* s_instance;
    static Renderer* GetInstance() { return s_instance; }

    bool init();
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
};

#endif
