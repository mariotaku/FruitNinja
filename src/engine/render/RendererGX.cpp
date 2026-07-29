// Port specific: Wii-native GX implementation of the Renderer backend.
//
// Companion to RendererGL.cpp (the GL/GLES2 backend) -- same class
// (`Renderer`), same public method signatures (Renderer.h). Unlike the GL
// backend, RendererGX makes ZERO gl* calls itself: it draws pure native GX
// (GX_Begin/End immediate mode, its own private m_WhiteTexObj GXTexObj) and
// only reaches into gl_funcsWii.cpp's shim state via plain-C++ `Wii_*` seam
// accessors for resources that TU still owns (game texture registry, mesh
// VBO/IBO bytes, viewport). GX has no binary counterpart (the Bada original
// is fixed-function OpenGL ES 1.x), so every method here is a from-scratch
// Wii port decision -- no binary fidelity applies, all of it is
// `// Port specific:`.
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
//      or GX_ORTHOGRAPHIC, correct [0,1] clip-z per libogc's guFrustum/
//      guOrtho convention) via libogc's guFrustum / guOrtho, with the
//      frustum parameters recovered algebraically from the GL-convention
//      projection matrix's coefficients. GX then does the perspective divide
//      in hardware, exactly like it would for any native GX title -- no
//      CPU-side divide, no lost precision, no warped meshes.
//
// guFrustum/guPerspective fully cover every perspective projection this port
// issues (GL perspective's near/far are already positive eye-space
// distances, guPerspective's expected convention). guOrtho does NOT cover
// this port's ortho, though: SetupGameOrtho's game-space box uses
// _Matrix44::OrthoW's convention (linear map of the RAW SIGNED world-z
// range [near=2000 .. far=-6000] to clip [0,1]), not guOrtho's assumed
// "n/f are positive eye-space distances in front of a camera looking down
// -Z" convention -- handing OrthoW's signed far=-6000 to guOrtho inverts its
// internal 1/(f-n) sign and clips out most of the scene (see LoadGxOrtho).
// LoadGxOrtho therefore uses guOrtho only for the convention-agnostic X/Y
// (l/r/t/b) terms and overwrites the z row directly from the GL matrix's own
// m[10]/m[14] -- an exact coefficient copy, not a re-derivation through
// guOrtho's incompatible near/far parameterization.
//
// CONFIRMED (on-device [GXMTX]/[GXDIAG] logs): the z-row copy alone left the
// 2D UI rendering entirely black. GX's valid clip-z (NDC z) range is [-1,0],
// but OrthoW's z-row maps world-z into clip [0,1] (matching what RendererGL
// feeds the GLES2 shader) -- e.g. a UI vertex at z=0 -> eye-z=-5600 ->
// clip_z=0.95, outside GX's range, so every 2D draw was clipped. Fix:
// LoadGxOrtho's out[2][3] additionally subtracts w (=1) to shift [0,1] into
// GX's [-1,0]; see the comment at that assignment.
//
// Texture upload/state (ShimTexture registry, GX_TexObj creation, TileRGBA8)
// stays entirely in gl_funcsWii.cpp -- RendererGX only BINDS an
// already-uploaded GX_TexObj via the new Wii_GetTexObj accessor. Likewise
// vertex/index buffer bytes are read back out of the shim's buffer registry
// via Wii_GetBufferData (extended in this pass to retain buffer bytes after
// upload -- see gl_funcsWii.cpp/.h changes noted at the top of that file).
//
// Only compiled when FRUIT_PLATFORM_WII is set -- src/engine/CMakeLists.txt
// adds this TU (and excludes RendererGL.cpp) for the FRUIT_PLATFORM_WII
// target, so this is the active Wii Renderer backend.
#ifdef FRUIT_PLATFORM_WII

#include "render/Renderer.h"
#include "render/RendererInternal.h"
#include "render/MatrixManager.h"
#include "debug/Logger.h"

#include <gccore.h>
#include <ogc/gu.h>
#include <malloc.h>
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

// Recover an orthographic box (l,r,b,t) from a GL column-major ortho
// projection and load it into `out` via guOrtho, then OVERWRITE guOrtho's
// z row directly from the GL matrix's own z coefficients. GL ortho
// coefficients (matches _Matrix44::OrthoW above, same convention
// MatrixManager::SetupOrtho uses):
//   m[0]=2/(r-l)  m[5]=2/(t-b)  m[10]=1/(f-n) [note: OrthoW's sign, see below]
//   m[12]=-(r+l)/(r-l)  m[13]=-(t+b)/(t-b)  m[14]=n/(n-f)
// _Matrix44::OrthoW (the builder MatrixManager::SetupOrtho/SetupPerspective's
// sibling actually uses in this port) writes m[10]=1/(far-near) and
// m[14]=near/(near-far) -- NOT the canonical GL -2/(f-n) form.
//
// Why the z row can't go through guOrtho's (n,f) parameters at all: guOrtho
// (per libogc's gu.h doc comment) requires "n and f... both given as
// positive distances" under the assumption pre-transformed points have
// negative eye-space z (camera looks down -Z, near/far both in front of the
// eye). This port's actual game ortho -- SetupGameOrtho's
// SetupOrtho(160,-160,-240,240, near=2000, far=-6000) -- is NOT that
// convention: OrthoW is a generic linear box-map [near..far] -> clip [0..1]
// over the RAW SIGNED world-z value, with near=+2000 and far=-6000 (far is
// numerically LESS than near, and negative). Recovering near_/far_ from
// m[10]/m[14] faithfully reproduces those exact signed values (2000, -6000)
// -- but then handing far_=-6000 to guOrtho as if it were a positive eye
// distance flips the sign of guOrtho's internal 1/(f-n) term, producing a
// z row that clips out most of the game's z range (e.g. the z=-5599
// background quad) instead of mapping it into GX's [0,1] clip volume.
// Fix: keep guOrtho only for the well-behaved, convention-agnostic l/r/t/b
// (X/Y) terms, then set mt[2][2]/mt[2][3] directly from the same m[10]/m[14]
// this port's OrthoW already uses -- a direct coefficient copy (with the GL
// column-major -> GX row-major transpose), not a re-derivation through
// guOrtho's incompatible parameterization. This exactly reproduces OrthoW's
// clip-z mapping (world z=near -> clip 0, z=far -> clip 1) for GX.
//
// CONFIRMED via on-device [GXMTX]/[GXDIAG] logs: modelview[2d] row2 =
// (0,0,1,-5600) (a UI vertex at z=0 -> eye-z=-5600) and ortho.proj row2 was
// (0,0,-0.000125,0.25) before this fix, giving
// clip_z = -0.000125*(-5600) + 0.25 = 0.95 -- OUTSIDE GX's valid clip-z range
// (see z-translation comment below) -- every 2D draw was being clipped,
// rendering the whole UI black.
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

    // Dummy n/f: only X/Y terms of guOrtho's output are used below; the z
    // row is overwritten immediately after with the exact OrthoW mapping.
    guOrtho(out, top, bottom, left, right, 1.0f, 2.0f);

    // Direct copy of OrthoW's z-row coefficients (GL m[10]/m[14], column-major)
    // into GX's row-major mt[2][2]/mt[2][3] -- reproduces
    // clip_z = m[10]*world_z + m[14] exactly, matching what RendererGL feeds
    // the GLES2 shader's u_mvp for the same draw.
    //
    // GX clip-z is [-1,0] but OrthoW maps to [0,1]; subtract w (=1, from proj
    // row3) to shift [0,1] -> [-1,0]. (2D UI rendered black without this;
    // on-device [GXMTX] confirmed clip_z=0.95 was being clipped.)
    out[2][2] = proj.m[10];
    out[2][3] = proj.m[14] - 1.0f;
}

// Loads MatrixManager's current projection + modelview (view*world) into GX's
// two independent matrix slots, replacing the combined-MVP-as-posmtx approach
// the gl_funcsWii.cpp shim uses. Must be called before GX_Begin for every
// DrawShaded2D / DrawMesh3D.
void LoadGxMatricesFromMatrixManager() {
    MatrixManager& mm = MatrixManager::GetInstance();
    const Matrix44& view = mm.GetViewStack().m_Current;
    const Matrix44& world = mm.GetWorldStack().m_Current;
    const Matrix44& proj = mm.GetProjectionStack().m_Current;
    Matrix44 modelview = view * world;

    Mtx mv;
    Gl44ToGxPosMtx(modelview, mv);
    GX_LoadPosMtxImm(mv, GX_PNMTX0);
    GX_SetCurrentMtx(GX_PNMTX0);

    Mtx44 gxProj;
    bool isPersp = IsPerspective(proj);
    if (isPersp) {
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
//
// `colorIsNativePacked` distinguishes the TWO different colour sources the
// two callers pass, which need OPPOSITE extraction on a big-endian host:
//
//  - DrawShaded2D (colorIsNativePacked=true): `Shaded2DVertex::color` is a
//    `uint32_t` assigned at runtime from `Colour::PlatformColour()` (see
//    RendererInternal.h) -- i.e. `*(uint32_t*)ptr = a<<24|b<<16|g<<8|r`, a
//    plain C integer store using the HOST's native endianness. Reading it
//    back must undo that with the same host-native load: memcpy the 4 bytes
//    into a uint32_t (reproduces the exact bit pattern, any host) then
//    extract channels via shifts (`v & 0xFF` == r, mathematically, regardless
//    of host byte order). This round-trips correctly on any host because
//    both the write and the read use native integer semantics.
//
//  - DrawMesh3D (colorIsNativePacked=false): mesh vertex colour is 4 raw
//    RGBA8888 bytes read directly off disk (PSP vertex-decl colorFmt==3, see
//    MeshManager.cpp's stream parse -- "the 4 bytes ... are read by
//    glColorPointer as 4-byte RGBA vertex colour"). Byte ARRAYS on disk have
//    a fixed a fixed r,g,b,a address order regardless of host endianness (no
//    scalar to byteswap -- MeshManager's FN_BIG_ENDIAN swaps only apply to
//    the multi-byte scalar header fields like vertDecl/vertCount, never to
//    this byte stream). Positional read (c[0]=r, c[1]=g, ...) is exact here.
//
// BUG (previously): both call sites shared one positional-only extraction.
// That is correct for the mesh path but WRONG for the 2D path on the Wii's
// big-endian PowerPC -- Shaded2DVertex::color's native uint32 store puts `a`
// (not `r`) at the lowest address on a BE host, so positional c[0] silently
// read alpha as red etc, showing up on-device as cyan/magenta/red-swapped UI
// labels. (An earlier version of this function instead shared the OTHER
// extraction -- memcpy+shift -- for both callers; that is correct for 2D but
// wrong for the mesh's on-disk RGBA bytes, which are not a native-endian
// integer at all. Neither single shared extraction is right for both
// sources; they read from genuinely different kinds of storage.)
void EmitInterleavedVertex(const unsigned char* base, int stride,
                           int posOff, bool haveUV, int uvOff,
                           bool haveColor, int colOff,
                           bool colorIsNativePacked) {
    const float* p = (const float*)(base + posOff);
    GX_Position3f32(p[0], p[1], p[2]);

    if (haveColor) {
        if (colorIsNativePacked) {
            uint32_t v;
            memcpy(&v, base + colOff, 4);
            uint8_t r = (uint8_t)(v & 0xFF);
            uint8_t g = (uint8_t)((v >> 8) & 0xFF);
            uint8_t b = (uint8_t)((v >> 16) & 0xFF);
            uint8_t a = (uint8_t)((v >> 24) & 0xFF);
            GX_Color4u8(r, g, b, a);
        } else {
            const unsigned char* c = base + colOff;
            GX_Color4u8(c[0], c[1], c[2], c[3]);
        }
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
// non-GL bookkeeping RendererGL::init() does (instance pointer, the 1x1
// white texture used by DrawColorQuad/untextured meshes). No shader programs
// to compile (GX has no GLSL -- TEV stages replace it, set up per draw in
// SetupGxVertexAndTev) and no streaming VBO (GX draws immediate-mode via
// GX_Begin/End straight off the CPU vertex bytes -- see DrawShaded2D).
//
// The white texel is a RendererGX-private GXTexObj, built directly rather
// than riding the gl_funcsWii.cpp shim's texture registry -- an all-opaque-
// white RGBA8 tile is just every byte 0xFF, so no tiling swizzle is needed
// (TileRGBA8 and a memset(0xFF) produce byte-identical output for this one
// case). DCFlushRange pushes the CPU-written bytes out of dcache so GX's
// texture sampler (which DMA-reads main RAM into TMEM) sees them;
// GX_InvalidateTexAll drops any stale TMEM cache lines from a previous
// texture at this address.
bool Renderer::init() {
    s_instance = this;

    u32 whiteBufSize = GX_GetTexBufferSize(1, 1, GX_TF_RGBA8, GX_FALSE, 0);
    m_WhiteTexBuf = memalign(32, whiteBufSize);
    if (!m_WhiteTexBuf) {
        LOG_ERROR("RENDERER/init", "GX: memalign(%u) failed for white texel", (unsigned)whiteBufSize);
        return false;
    }
    memset(m_WhiteTexBuf, 0xFF, whiteBufSize);
    DCFlushRange(m_WhiteTexBuf, whiteBufSize);
    GX_InvalidateTexAll();
    GX_InitTexObj(&m_WhiteTexObj, m_WhiteTexBuf, 1, 1, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&m_WhiteTexObj, GX_NEAR, GX_NEAR);
    return true;
}

// Port specific: GX FIFO/video teardown is owned by mainWii.cpp; this only
// releases the resource init() allocated (mirrors RendererGL::shutdown).
void Renderer::shutdown() {
    if (m_WhiteTexBuf) {
        free(m_WhiteTexBuf);
        m_WhiteTexBuf = nullptr;
    }
    // Port specific: no binary counterpart. See RendererGL.cpp::shutdown()'s
    // matching comment -- s_instance must be cleared here so the atexit
    // singleton teardown (PowerUpManager/TextureManager/PSPParticleManager)
    // sees a dead GetInstance() rather than dereferencing a dangling this.
    if (s_instance == this) s_instance = nullptr;
}

// Port specific: GX equivalents of RendererGL::InitGL's one-time GL state
// (viewport, blend mode, depth). Calls GX_SetViewport/GX_SetScissor directly
// (not through the glViewport shim, which now also drives real per-frame GX
// state -- see gl_funcsWii.cpp) since this is boot-time setup that predates
// the shim's g_ShimViewport bookkeeping being meaningful; both this call and
// mainWii's own boot-time GX_SetViewport/GX_SetScissor use the full-EFB rect,
// so the shim's first per-frame glViewport call (GameWii.cpp's renderFrame)
// simply re-asserts (or narrows, if letterboxed) the same state. This call
// additionally seeds the default Z-mode. Depth-test default matches the GL
// path (enabled, LEQUAL -- see DrawMesh3D's per-draw toggle note below for
// why 2D vs 3D differ).
void Renderer::InitGL(int width, int height) {
    GX_SetViewport(0.0f, 0.0f, (f32)width, (f32)height, 0.0f, 1.0f);
    GX_SetScissor(0, 0, (u32)width, (u32)height);
    // Port specific: initial cull state; DrawShaded2D/DrawMesh3D each set
    // their own cull mode explicitly per draw (see those functions), so this
    // is just the boot-time default and never leaks into either draw path.
    GX_SetCullMode(GX_CULL_NONE);
    SetStandardAlphaBlend();
    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
}

// Port specific: 2D quad/UI draw path (DrawQuad / DrawColorQuad /
// draw_fullscreen_quad / DrawTriList / DrawTriStrip all funnel here via
// Renderer.cpp's shared code). Ignores the passed `mvp` -- see the file-top
// comment for why -- and instead loads MatrixManager's current
// projection/modelview split into GX directly. RendererGL doesn't touch
// depth state inside DrawShaded2D either (caller's responsibility per
// Renderer.h) -- this function matches that contract: it neither reads nor
// sets GX_SetZMode, so whatever GameDraw's real DisplayManager::
// SetDepthBuffer/SetDepthBufferWrite calls last configured (mirrored into GX
// by the gl_funcsWii.cpp shadow ZMode, see g_ZMode/ApplyZMode) stays live
// for this draw. That is what makes 2D HUD correctly overlay 3D fruit
// (test-on/write-off during the HUD/splat pass -- see GameInit.cpp's
// GameDraw) instead of the GX layer racing 2D and 3D against a hardcoded,
// caller-blind ZMode.
//
// The depth buffer is not 2D's job to seed: DisplayManagerWii::SwapBuffers
// forces GX_SetZMode write-enable ON immediately before GX_CopyDisp every
// frame (see that file, independent of the shadow state above), so the EFB
// Z-clear that GX_CopyDisp performs is never gated off by whatever ZMode the
// last draw of the frame left behind. That's the root fix for the earlier
// black-center tumbling-fruit bug -- 3D fruit never depended on 2D quads
// writing a depth floor, so 2D writing off (per the caller's real intent)
// does not reintroduce it.
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
    //
    // tex==kWhiteTexSentinel: Renderer.cpp::DrawColorQuad (portable, shared
    // with the GL backend) passes m_WhiteTex explicitly to force "sample
    // opaque white regardless of what's bound". On Wii m_WhiteTex is a fixed
    // sentinel (not a shim registry id -- the white texel lives in
    // RendererGX's own m_WhiteTexObj, see init()), so it's resolved here
    // directly instead of through Wii_GetTexObj.
    GLuint boundTex = tex ? tex : Wii_GetBoundTexture();
    bool textured = false;
    GXTexObj* texObj = (tex == kWhiteTexSentinel) ? &m_WhiteTexObj
                      : boundTex ? Wii_GetTexObj(boundTex) : nullptr;
    if (texObj) {
        GX_LoadTexObj(texObj, GX_TEXMAP0);
        textured = true;
    } else if (boundTex) {
        static bool warnedNoTexObj = false;
        if (!warnedNoTexObj) {
            warnedNoTexObj = true;
            LOG_WARN("RendererGX", "DrawShaded2D: tex %u bound but has no GX_TexObj "
                     "-- drawing untextured (raw vertex colour, one-shot warning)",
                     (unsigned)boundTex);
        }
    }

    // Port specific: 2D UI quads are unculled in the binary's fixed-function
    // equivalent (single-sided billboards, winding not guaranteed). Set
    // explicitly so this draw never inherits DrawMesh3D's back-face cull
    // state from a preceding 3D draw this frame.
    GX_SetCullMode(GX_CULL_NONE);
    SetStandardAlphaBlend();
    // Depth state (test/func/write) is NOT set here -- it is driven by the
    // caller's real GL depth calls (glEnable/glDisable(GL_DEPTH_TEST),
    // glDepthFunc, glDepthMask), which the gl_funcsWii.cpp shim mirrors into
    // GX via a shadow GX_SetZMode (see g_ZMode/ApplyZMode there). GameDraw's
    // DisplayManager::SetDepthBuffer/SetDepthBufferWrite calls already
    // express the binary-faithful per-phase intent (test-on/write-off for
    // the HUD/splat/2D pass) -- matches RendererGL::DrawShaded2D, which
    // likewise leaves depth state to the caller per Renderer.h's contract.
    SetupGxVertexAndTev(/*haveUV=*/true, textured);

    const unsigned char* base = (const unsigned char*)verts;
    GX_Begin(GxPrimFromGL(prim), GX_VTXFMT0, (u16)vertCount);
    for (int i = 0; i < vertCount; ++i) {
        EmitInterleavedVertex(base + (size_t)stride * i, stride,
                              posOff, /*haveUV=*/true, uvOff,
                              /*haveColor=*/true, colOff,
                              /*colorIsNativePacked=*/true);
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

    bool haveUV = uvSize > 0;
    bool haveColor = colSize > 0;
    bool textured = false;
    if (haveUV) {
        // tex==0 -> RendererGX-private white GXTexObj (untextured meshes
        // still sample a texture so the shared GX_MODULATE TEV stage in
        // SetupGxVertexAndTev works unmodified); tex!=0 -> shim registry.
        GXTexObj* texObj = tex ? Wii_GetTexObj(tex) : &m_WhiteTexObj;
        if (texObj) {
            GX_LoadTexObj(texObj, GX_TEXMAP0);
            textured = true;
        }
    }
    if (!textured) haveUV = false;

    // Port specific: fruit meshes are authored for GL (CCW = front-facing);
    // host Geometry::Render culls GL_BACK. GX's screen-space winding is the
    // OPPOSITE parity, so the GL "back" faces are GX "front" faces -- cull
    // GX_CULL_FRONT to match GL_CULL_BACK. This keeps the outward (textured)
    // shell and discards the inward one. It is most visible on dual-shell
    // meshes: the bomb (outward dark body + inward red interior) rendered as
    // a SOLID RED ball under GX_CULL_BACK because the dark body was culled and
    // only the red inner shell survived; GX_CULL_FRONT shows the dark body +
    // red X correctly. Single-shell fruit (orange, watermelon) looked OK under
    // GX_CULL_BACK only because they're near-symmetric so the mirrored inner
    // face reads similar -- it was still wrong. (The earlier GX_CULL_BACK pick
    // was made against a stale .dol and was incorrect.)
    GX_SetCullMode(GX_CULL_FRONT);
    // Depth state is NOT set here -- driven by the caller's GL depth calls
    // via the gl_funcsWii.cpp shadow ZMode (see DrawShaded2D's matching
    // note). GameDraw's BeginFrame sets glDepthFunc(GL_LESS) once and the 3D
    // pass (SetDepthBuffer(1)+SetDepthBufferWrite(1) just before
    // ActorManager::Draw) never changes the func, so GX_LESS is already the
    // live compare by the time this draw runs -- required for correctness,
    // not just fidelity: bomb (and other dual-shell fruit) meshes carry
    // co-incident triangles (outward dark-body shell + inward interior/red
    // shell at the same positions). Under LESS the FIRST-submitted (dark
    // body) triangle wins the equal-depth pixel -- correct. Under LEQUAL the
    // later (red inner) shell wins every pixel -> solid red bomb (the
    // GL_LEQUAL "white-bomb" experiment in commit 99b9bafb was REVERTED in
    // b7bbff1b). If a future caller-side change ever leaves GL_LEQUAL active
    // going into the 3D pass, this invariant breaks -- see BeginFrame.
    // Port specific: match host Geometry::Render, which does glDisable(GL_BLEND)
    // for every 3D mesh (opaque). Alpha blend on the mesh let self-overlapping
    // fragments accumulate/blend against the framebuffer with depth-write on ->
    // black-center on tumbling fruit. Opaque src-replace mirrors the GL path.
    GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);

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
                                  posOff, haveUV, uvOff, haveColor, colOff,
                                  /*colorIsNativePacked=*/false);
        }
        GX_End();
    } else {
        GX_Begin(GxPrimFromGL(prim), GX_VTXFMT0, (u16)vertCount);
        for (int i = 0; i < vertCount; ++i) {
            EmitInterleavedVertex(vboData + (size_t)stride * i, stride,
                                  posOff, haveUV, uvOff, haveColor, colOff,
                                  /*colorIsNativePacked=*/false);
        }
        GX_End();
    }
}

// Port specific: records the bound texture id; GX_LoadTexObj happens at draw
// time (DrawShaded2D/DrawMesh3D resolve `tex` -> GX_TexObj themselves), so
// this only needs to route through the shim's bound-texture bookkeeping —
// matches RendererGL::BindTexture2D's raw-GLuint debug-overlay use case
// (Renderer.h: "no binary counterpart... raw GLuint textures, not
// Mortar::Texture"). Uses the Wii_SetBoundTexture seam (plain C++, not gl*)
// instead of glActiveTexture+glBindTexture so RendererGX.cpp stays gl-free;
// the shim's g_BoundTexture stays the single source of truth for both this
// debug path and the game's Texture::Set/glBindTexture path (both feed
// DrawShaded2D's tex==0 "use currently-bound texture" contract).
void Renderer::BindTexture2D(uint32_t texId) {
    Wii_SetBoundTexture(texId);
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
    // Y-FLIP: glScissor is bottom-left origin, GX_SetScissor is top-left. GL
    // uses (bottom + H/2)/H * vpH (distance from viewport bottom to the rect's
    // bottom edge); GX needs the distance from the viewport TOP to the rect's
    // TOP edge = (H/2 - top)/H * vpH. (Was copied verbatim from RendererGL
    // without the flip -> clipped the wrong vertical band, e.g. About screen.)
    int sy = (int)((orthoH * 0.5f - top) / orthoH * (float)vpH) + vpY;
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
