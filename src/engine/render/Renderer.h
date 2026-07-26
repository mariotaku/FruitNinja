#ifndef MORTAR_RENDERER_H
#define MORTAR_RENDERER_H

#include "render/gl_funcs.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "render/MatrixManager.h"
#include "render/ShaderProgram.h"
#include "math/Colour.h"
#include "core/MortarTypes.h"
#include <vector>

#if defined(FRUIT_PLATFORM_WII)
#include <gccore.h>  // GXTexObj (m_WhiteTexObj)
#endif

// Port specific (stage 2): compile-time default for m_Merge2DPreTransform
// (MVP pre-transform merging; see the stage-2 class comment). Define to 0 to
// build with same-MVP-only merging by default.
#ifndef FN_2D_PRETRANSFORM_DEFAULT
#define FN_2D_PRETRANSFORM_DEFAULT 1
#endif

// GL renderer — targets the same behaviour as the binary's ES 1.x pipeline.
//
// GLES2 migration phase 2: the 2D quad/UI draws (DrawQuad, DrawTriList,
// DrawTriStrip, DrawColorQuad, draw_fullscreen_quad) render through a real
// GLES2 shader program (Shaders.h / ShaderProgram.h) via DrawShaded2D.
// Everything that funnels through these (font, particles, HUD, screens)
// rides the shader path with them.
// Phase 3: the 3D mesh path (Geometry::Render) renders through the Mesh3D
// program via DrawMesh3D (same unlit texture2D * v_color modulate -- all
// meshes are IsLit=false).
//
// Port specific: GL state cache + persistent ring VBO (performance; output is
// pixel-identical). No fixed-function path remains, so the draws no longer
// tear the pipeline down after every call. Instead:
//   - Redundant GL state calls (program, texture bind, blend/cull enable,
//     MVP upload) are elided through a shadow copy of the GL state
//     (m_GLState). Blend FUNC is (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
//     everywhere in the binary (set once at init / DisplayManager frame top;
//     draws only toggle the GL_BLEND enable) so it is never re-issued
//     per draw.
//   - 2D vertex data streams through a persistent ring inside m_QuadVBO via
//     glBufferSubData (orphan + rewind on wrap); attribs 0/1/2 stay
//     configured for the canonical Shaded2DVertex layout between 2D draws.
//   - RULE: any code that binds unit-0 GL_TEXTURE_2D directly must go
//     through BindTexture2D (sampling intent, lazy) or BindTextureForUpload
//     (texture create/upload, immediate) so the shadow never desyncs; any
//     code that glDeleteTextures a texture the shadow may consider bound
//     calls NotifyTextureDeleted. External GL churn between frames is
//     absorbed by DisplayManager::BeginFrame -> InvalidateStateCache().
//
// Stage 2 (deferred adjacent-run merger): 2D submissions no longer issue one
// glBufferSubData + glDrawArrays each. DrawShaded2D accumulates packed
// vertices CPU-side (m_Batch2D) and the whole run is uploaded + drawn in ONE
// glBufferSubData + ONE glDrawArrays at the next flush. Submission order is
// preserved exactly -- only ADJACENT compatible submissions merge, never
// reordered (2D alpha blending is order-dependent, and the HUD block depth-
// tests against 3D fruit depth).
//   - GL_TRIANGLE_STRIP submissions are converted to GL_TRIANGLES at submit
//     time (winding-preserving (i,i+1,i+2)/(i+1,i,i+2) expansion; the fonts'
//     degenerate connector triangles stay zero-area = zero pixels) so strips
//     (fonts, blades, shadows, bars) and lists (particles) share one batch.
//   - Batch-breaking axes: effective sampling texture, and -- when MVP
//     pre-transform is off or the MVP is non-affine -- the MVP itself.
//     Blend / cull / scissor / depth / program are NOT part of the key:
//     every path that changes them (SetBlendEnabled, SetCullFaceEnabled,
//     SetClipRect/ClearClipRect, DisplayManager::SetDepthBuffer/
//     SetDepthBufferWrite, DrawMesh3D, BakedStringBox's raw scissor block)
//     calls Flush2D() BEFORE touching GL, so pending vertices always land
//     under the state they were submitted with.
//   - MVP merging (m_Merge2DPreTransform, default ON): vertex positions are
//     CPU pre-transformed by the full MVP at submit time and the flush draws
//     with u_mvp = identity, so draws with different world matrices share a
//     batch (precedent: the TTF glyph path in Font.cpp pre-transforms the
//     same way). Identity multiply is IEEE-exact but CPU mul-add vs GPU
//     (FMA / dot ordering) is NOT guaranteed bit-identical, so an edge-exact
//     pixel could differ; turn the switch off to fall back to same-MVP-only
//     merging (pixel-identical to stage 1). Non-affine MVPs (projective
//     bottom row) are never pre-transformed -- a vec3 attrib cannot carry
//     the resulting w -- and batch per-MVP instead.
//   - RULE: any pixel readback (glReadPixels), buffer swap, or GL state
//     change outside the shadowed setters MUST be preceded by Flush2D().
//     Wired barriers: DrawMesh3D entry, SetClipRect/ClearClipRect,
//     DisplayManager::SetDepthBuffer/SetDepthBufferWrite, InvalidateState-
//     Cache (covers DisplayManager::BeginFrame before the clear),
//     BindTextureForUpload / NotifyTextureDeleted, GameSDL.cpp before the
//     F12 screenshot + SDL_GL_SwapWindow, and the test harness / render
//     tests before every glReadPixels.
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

    // Port specific: no binary counterpart. Records `texId` as the requested
    // unit-0 sampling texture WITHOUT calling GL; the actual glBindTexture is
    // issued lazily at the next draw (and only if it differs from what is
    // really bound). This is the required entry point for EVERY sampling bind
    // (Texture::Set/UnSet, font atlas pages, particles, debug overlays);
    // texId == 0 is meaningful and, at draw time, still binds 0 -- preserving
    // the incomplete-texture sampling semantics of the old eager path.
    // A draw that passes its own non-zero texture (DrawQuad white-substitute,
    // DrawColorQuad, draw_fullscreen_quad, DrawMesh3D) overwrites the
    // requested value with it, exactly mirroring what the old eager bind left
    // on unit 0. On Wii this stays an immediate shim bind (RendererGX.cpp).
    void BindTexture2D(uint32_t texId);

    // Port specific: immediate glBindTexture(GL_TEXTURE_2D, texId) + shadow
    // sync. Use for texture CREATE/UPLOAD paths (glTexImage2D /
    // glTexSubImage2D need the real binding now); sampling paths use
    // BindTexture2D instead. Keeps m_GLState.boundTex exact so lazy draws
    // never skip a needed re-bind after an upload touched unit 0.
    void BindTextureForUpload(uint32_t texId);

    // Port specific: call right after glDeleteTextures(texId). Mirrors the GL
    // rule that deleting the currently-bound texture rebinds 0, so the shadow
    // (and the lazily-requested sampling texture) never point at a dead --
    // or worse, recycled -- texture name.
    void NotifyTextureDeleted(uint32_t texId);

    // Port specific: shadowed glEnable/glDisable(GL_BLEND) / (GL_CULL_FACE).
    // All per-draw blend/cull toggles go through these so redundant GL calls
    // are elided. On Wii they forward straight to the GX shim.
    void SetBlendEnabled(bool enabled);
    void SetCullFaceEnabled(bool enabled);

    // Port specific: forget everything the GL state shadow believes (program,
    // texture binding, blend/cull, uploaded MVPs, ring attrib config) and
    // re-assert glActiveTexture(GL_TEXTURE0). Called once per frame from
    // DisplayManager::BeginFrame (which also covers the test harness -- every
    // test render loop goes through BeginFrame) and from code that touches GL
    // buffer bindings directly (MeshManager VBO/IBO uploads), so externally-
    // touched GL state cannot desync the shadow. Does NOT clear the requested
    // sampling texture -- the legacy eager binding persisted across frames
    // and the mirror must too.
    // Stage 2: drains the pending 2D batch FIRST (Flush2D), so pending
    // vertices are drawn before the shadow is forgotten. The flush relies on
    // blend/cull/depth/scissor GL state being unchanged since submission --
    // true at both call sites (BeginFrame runs before its state resets;
    // MeshManager churn is buffer-binding only, which Flush2D re-asserts).
    void InvalidateStateCache();

    // Port specific (stage 2): draw the pending 2D batch now -- one ring
    // upload + one glDrawArrays(GL_TRIANGLES) -- and clear it. No-op when
    // nothing is pending (cheap to call defensively). MUST be called before
    // any pixel readback, buffer swap, or GL state change that bypasses the
    // shadowed setters (see the stage-2 RULE in the class comment). On Wii
    // this is a no-op: the GX backend draws immediately.
    void Flush2D();

    // Port specific (stage 2): runtime switch for MVP pre-transform merging
    // (see class comment). ON by default; set false to fall back to
    // same-MVP-only merging, which is pixel-identical to the unbatched
    // stage-1 path. Compile-time default override: FN_2D_PRETRANSFORM_DEFAULT.
    bool m_Merge2DPreTransform;

    // Port specific: no binary counterpart. Wraps glPolygonMode(GL_FRONT_AND_BACK,
    // GL_LINE/GL_FILL) for the F2 wireframe debug toggle. No-op where glPolygonMode
    // is unavailable (GLES / Emscripten) -- same guard the call site used to have.
    void SetWireframe(bool enabled);

    // Path B rendering with QUADCUSTOMVERTEX (stride 0x24).
    // Matches original DrawTriList (0x00240e34) / DrawTriStrip.
    // setBlendFunc: retained for API stability but now INERT -- glBlendFunc is
    // (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) everywhere in the v1.6.1 binary
    // (glBlendFunc @0x0010c088 is xref'd only from init/frame-top; Mesh::
    // DrawTris @0x240c30 toggles the GL_BLEND enable, never the func), so the
    // func is set once and never per draw.
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
    // Port specific: no trailing restore (nothing fixed-function is left to
    // coexist with; MatrixManager no longer issues GL). Leaves its own
    // program / buffer / attrib config in place and flags the ring attrib
    // config dirty so the next 2D draw restores its bindings.
    void DrawMesh3D(GLuint vbo, GLuint ibo, int vertCount, int indexCount, GLenum prim,
                    int stride, int posOff, int uvOff, int colOff, int uvSize, int colSize,
                    GLuint tex, const Matrix44& mvp);

private:
    // Shared GLES2 submit for the 2D paths. Stage 2: does NOT draw -- it
    // repacks `verts` (vertCount * stride bytes; pos 3xfloat @posOff, uv
    // 2xfloat @uvOff, colour 4x normalized ubyte @colOff) into the canonical
    // 24-byte Shaded2DVertex layout, expands GL_TRIANGLE_STRIP to
    // GL_TRIANGLES (winding-preserving), optionally CPU pre-transforms
    // positions by `mvp` (m_Merge2DPreTransform + affine MVP), and appends
    // to the pending CPU batch (m_Batch2D). A submission whose batch key
    // (effective texture; MVP when not pre-transformed) differs from the
    // pending batch -- or that would overflow the ring -- flushes first.
    // The actual upload + glDrawArrays happens in Flush2D().
    // `prim` must be GL_TRIANGLES or GL_TRIANGLE_STRIP (all call sites).
    // tex != 0: resolves to it. tex == 0: resolves to the lazily-requested
    // BindTexture2D texture (Texture::Set / draw_sprite own it -- same
    // contract the eager path had; requested 0 still binds 0 at flush).
    // Blend / cull / scissor / depth state is the CALLER's responsibility,
    // set BEFORE submitting through the flushing setters (SetBlendEnabled /
    // SetCullFaceEnabled / SetClipRect / DisplayManager depth) so a change
    // drains earlier submissions under their own state.
    void DrawShaded2D(const void* verts, int vertCount, int stride,
                      int posOff, int uvOff, int colOff,
                      GLenum prim, GLuint tex, const Matrix44& mvp);

#if defined(FRUIT_PLATFORM_WII)
    // Port specific: GX has no GLSL -- TEV stages (SetupGxVertexAndTev in
    // RendererGX.cpp) replace the Quad2D/Mesh3D shader programs entirely, and
    // GX immediate mode (GX_Begin/End) needs no streaming VBO. The white
    // texel is a self-contained GXTexObj built inline by RendererGX::init()
    // (see m_WhiteTexObj/m_WhiteTexBuf below) rather than riding the
    // gl_funcsWii.cpp shim's texture registry.
    GXTexObj m_WhiteTexObj;  // 1x1 opaque white, built directly (no shim registry slot)
    void* m_WhiteTexBuf;     // GX_GetTexBufferSize(1,1,GX_TF_RGBA8)-sized, memalign(32)'d backing store
    // m_WhiteTex stays a GLuint SENTINEL (not a shim registry id -- the shim
    // registry no longer holds the white texture) so the portable
    // Renderer.cpp::DrawColorQuad can keep passing it into DrawShaded2D
    // unmodified. DrawShaded2D (RendererGX.cpp) recognises this exact value
    // and loads &m_WhiteTexObj directly instead of resolving it through
    // Wii_GetTexObj. Any fixed non-zero value works; kept distinct from real
    // shim texture ids (which start at 1 and are allocated sequentially, see
    // gl_funcsWii.cpp's ShimTexture registry) by using the max GLuint.
    static const GLuint kWhiteTexSentinel = 0xFFFFFFFFu;
    GLuint m_WhiteTex;       // == kWhiteTexSentinel, always (see above)
#else
    ShaderProgram m_Quad2D;  // the Shaders.h Quad2D program
    ShaderProgram m_Mesh3D;  // the Shaders.h Mesh3D program (3D mesh path)
    GLuint m_QuadVBO;        // persistent ring VBO for DrawShaded2D
    GLuint m_WhiteTex;       // 1x1 opaque white — lets DrawColorQuad reuse the textured shader

    // Port specific: GL state shadow + 2D ring bookkeeping (see class
    // comment). All `*Valid` flags are cleared by InvalidateStateCache();
    // `requestedTex` mirrors what the legacy eager scheme would have left
    // bound on unit 0 and survives invalidation.
    struct GLStateShadow {
        GLuint   program;       // current glUseProgram
        bool     programValid;
        uint32_t requestedTex;  // lazily-requested unit-0 sampling texture
        GLuint   boundTex;      // actually bound on unit 0
        bool     boundTexValid;
        bool     blendOn;
        bool     blendValid;
        bool     cullOn;
        bool     cullValid;
        bool     ringReady;     // ring bound as GL_ARRAY_BUFFER + attribs 0/1/2 configured
        float    mvp2D[16];     // last MVP uploaded to the Quad2D program
        bool     mvp2DValid;
        float    mvp3D[16];     // last MVP uploaded to the Mesh3D program
        bool     mvp3DValid;
    };
    GLStateShadow m_GLState;
    GLsizeiptr m_RingCursor;              // next free byte in the ring (24-aligned)

    // Port specific (stage 2): pending 2D batch. m_Batch2D holds packed
    // Shaded2DVertex bytes (always GL_TRIANGLES after strip expansion);
    // empty() == no batch open. Key fields below are valid while non-empty.
    std::vector<unsigned char> m_Batch2D;
    GLuint m_BatchTex;      // effective sampling texture of the open batch
    bool   m_BatchPreXf;    // verts pre-transformed (flush draws u_mvp = identity)
    float  m_BatchMVP[16];  // batch MVP when !m_BatchPreXf
#endif
};

#endif
