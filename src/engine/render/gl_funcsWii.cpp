// Port specific: GL-on-GX translation shim for the Wii backend.
//
// Implements every gl* entry point the core render / asset / particle TUs
// call (see render/gl_compat.h's FRUIT_PLATFORM_WII branch), backed by
// libogc's GX. This lets Renderer.cpp / DisplayManager.cpp / Texture.cpp /
// ShaderProgram.cpp / Shaders.cpp / Geometry.cpp / MeshManager.cpp compile
// and link UNCHANGED against the Wii target.
//
// PASS 2 SCOPE (this file): real GX immediate-mode rendering. glDrawArrays /
// glDrawElements build a GX_Begin/GX_End batch from the recorded shim state
// (g_ShimAttrib pointers, g_ShimMVP, bound texture): the full 4x4 MVP is
// loaded as the GX projection with an identity pos modelview (clip = MVP*pos
// for both 2D-ortho and 3D-perspective), and a single GX_MODULATE / GX_PASSCLR
// TEV stage reproduces GL_MODULATE. Textures are uploaded/converted to
// GX_TF_RGBA8 tiled. Shader objects remain no-op stubs (GX uses fixed TEV
// stages, not GLSL). Remaining deferred pieces (glTexSubImage2D, per-frame
// viewport/scissor Y-flip, compressed textures, glReadPixels) carry a
// // TODO(wii Pass 2): marker.
//
// Only compiled when FRUIT_PLATFORM_WII is set (see src/engine/CMakeLists.txt).
#ifdef FRUIT_PLATFORM_WII

#include "render/gl_compat.h"

#include <gccore.h>
#include <ogc/gu.h>
#include <malloc.h>
#include <cstring>
#include <cstdio>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Shared shim state (read by DisplayManagerWii.cpp + Pass 2's GX draw).
// ---------------------------------------------------------------------------

// MVP uploaded by glUniformMatrix4fv (column-major 4x4, GL layout). Pass 2's
// GX draw converts this to a GX Mtx44 for GX_LoadProjectionMtx / posmtx.
float g_ShimMVP[16] = {
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1
};

// Clear colour stored by glClearColor; applied at swap via GX_SetCopyClear
// (GX has no glClear -- the clear happens during GX_CopyDisp in the swap).
GXColor g_ShimClearColor = { 0, 0, 0, 255 };

// Stored viewport (glViewport) -- reported back by glGetIntegerv(GL_VIEWPORT).
int g_ShimViewport[4] = { 0, 0, 640, 480 };

namespace {

// ---- Vertex attribute table (glVertexAttribPointer state) ---------------
struct ShimAttrib {
    const void* ptr;
    int         size;
    unsigned    type;
    int         stride;
    unsigned    boundBuffer;   // GL_ARRAY_BUFFER binding at pointer-set time
    bool        enabled;
};
ShimAttrib g_ShimAttrib[8];

// Constant vertex-attribute values (glVertexAttrib4f) -- used for an attrib
// index when its array is DISABLED. DrawMesh3D sets attrib 2 (colour) to
// white this way for uncoloured meshes, and attrib 1 (uv) to zero.
float g_ShimConstAttrib[8][4] = {
    {0,0,0,1},{0,0,0,1},{1,1,1,1},{0,0,0,1},
    {0,0,0,1},{0,0,0,1},{0,0,0,1},{0,0,0,1}
};

// ---- Buffer objects (glGenBuffers / glBufferData) -----------------------
struct ShimBuffer {
    unsigned char* data;
    long           size;
    bool           used;
};
const int kMaxBuffers = 512;
ShimBuffer g_Buffers[kMaxBuffers];
unsigned   g_ArrayBufferBinding   = 0;
unsigned   g_ElementBufferBinding = 0;

// ---- Texture objects (glGenTextures / glTexImage2D) ---------------------
struct ShimTexture {
    GXTexObj obj;
    void*    texels;      // 32-aligned GX_TF_RGBA8 tiled data
    int      width;
    int      height;
    int      minFilter;
    int      magFilter;
    int      wrapS;
    int      wrapT;
    bool     hasImage;
    bool     used;
};
const int kMaxTextures = 1024;
ShimTexture g_Textures[kMaxTextures];
unsigned    g_ActiveTexUnit    = 0;
unsigned    g_BoundTexture     = 0;   // texture bound on the active unit

// ---- Shader / program objects (no-op stubs) -----------------------------
unsigned g_NextShaderId  = 1;
unsigned g_NextProgramId = 1;
int      g_NextUniformId = 1;

unsigned NextFreeBuffer() {
    for (int i = 1; i < kMaxBuffers; ++i) {
        if (!g_Buffers[i].used) return (unsigned)i;
    }
    return 0;
}

unsigned NextFreeTexture() {
    for (int i = 1; i < kMaxTextures; ++i) {
        if (!g_Textures[i].used) return (unsigned)i;
    }
    return 0;
}

// GX texture-address wrap from GL enum.
u8 GxWrap(int glWrap) {
    switch (glWrap) {
        case GL_REPEAT:        return GX_REPEAT;
        case GL_CLAMP_TO_EDGE: return GX_CLAMP;
        default:               return GX_CLAMP;
    }
}

// GX texture filter from GL enum (mip filters collapse to the base filter --
// no mipmaps generated in this pass).
u8 GxFilter(int glFilter) {
    return (glFilter == GL_NEAREST) ? GX_NEAR : GX_LINEAR;
}

// Tile a linear RGBA8 source into GX_TF_RGBA8's 4x4-block, dual-cacheline
// layout (AR block then GB block per 4x4 tile). `src` is width*height*4
// bytes, R,G,B,A order. `dst` must be 32-byte aligned and
// GX_GetTexBufferSize()-sized. Returns nothing; caller flushes the cache.
void TileRGBA8(const unsigned char* src, void* dst, int w, int h) {
    unsigned char* d = (unsigned char*)dst;
    for (int ty = 0; ty < h; ty += 4) {
        for (int tx = 0; tx < w; tx += 4) {
            // AR half-tile (16 texels x 2 bytes = 32 bytes).
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    int sx = tx + x, sy = ty + y;
                    unsigned char a = 255, r = 0;
                    if (sx < w && sy < h) {
                        const unsigned char* p = src + ((sy * w) + sx) * 4;
                        r = p[0];
                        a = p[3];
                    }
                    *d++ = a;
                    *d++ = r;
                }
            }
            // GB half-tile.
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    int sx = tx + x, sy = ty + y;
                    unsigned char g = 0, b = 0;
                    if (sx < w && sy < h) {
                        const unsigned char* p = src + ((sy * w) + sx) * 4;
                        g = p[1];
                        b = p[2];
                    }
                    *d++ = g;
                    *d++ = b;
                }
            }
        }
    }
}

// Expand an incoming (format,type) source to a temporary linear RGBA8 buffer.
// Supports the formats Texture.cpp actually uploads: RGBA8 (0x01), RGB8
// (0x00), plus the packed 5551 / 565 shorts for completeness. Returns a
// malloc'd w*h*4 buffer the caller must free, or NULL on unsupported input.
unsigned char* ExpandToRGBA8(int w, int h, GLenum format, GLenum type,
                             const void* pixels) {
    if (!pixels || w <= 0 || h <= 0) return NULL;
    unsigned char* out = (unsigned char*)malloc((size_t)w * h * 4);
    if (!out) return NULL;
    const int n = w * h;

    if (type == GL_UNSIGNED_BYTE && format == GL_RGBA) {
        memcpy(out, pixels, (size_t)n * 4);
    } else if (type == GL_UNSIGNED_BYTE && format == GL_RGB) {
        const unsigned char* s = (const unsigned char*)pixels;
        for (int i = 0; i < n; ++i) {
            out[i * 4 + 0] = s[i * 3 + 0];
            out[i * 4 + 1] = s[i * 3 + 1];
            out[i * 4 + 2] = s[i * 3 + 2];
            out[i * 4 + 3] = 255;
        }
    } else if (type == GL_UNSIGNED_SHORT_5_5_5_1 && format == GL_RGBA) {
        const unsigned short* s = (const unsigned short*)pixels;
        for (int i = 0; i < n; ++i) {
            unsigned short v = s[i];
            out[i * 4 + 0] = (unsigned char)(((v >> 11) & 0x1F) * 255 / 31);
            out[i * 4 + 1] = (unsigned char)(((v >> 6) & 0x1F) * 255 / 31);
            out[i * 4 + 2] = (unsigned char)(((v >> 1) & 0x1F) * 255 / 31);
            out[i * 4 + 3] = (unsigned char)((v & 0x1) ? 255 : 0);
        }
    } else if (type == GL_UNSIGNED_SHORT_5_6_5 && format == GL_RGB) {
        const unsigned short* s = (const unsigned short*)pixels;
        for (int i = 0; i < n; ++i) {
            unsigned short v = s[i];
            out[i * 4 + 0] = (unsigned char)(((v >> 11) & 0x1F) * 255 / 31);
            out[i * 4 + 1] = (unsigned char)(((v >> 5) & 0x3F) * 255 / 63);
            out[i * 4 + 2] = (unsigned char)((v & 0x1F) * 255 / 31);
            out[i * 4 + 3] = 255;
        }
    } else {
        // Unsupported combination -- leave opaque magenta so it's visible if
        // a Pass 2 draw ever samples it, rather than crashing.
        for (int i = 0; i < n; ++i) {
            out[i * 4 + 0] = 255;
            out[i * 4 + 1] = 0;
            out[i * 4 + 2] = 255;
            out[i * 4 + 3] = 255;
        }
    }
    return out;
}

// GL primitive-mode -> GX primitive-type. Best-effort for the modes this
// port actually issues (TRIANGLES / TRIANGLE_STRIP / TRIANGLE_FAN /
// LINE_STRIP / LINES).
u8 GxPrim(GLenum mode) {
    switch (mode) {
        case GL_TRIANGLES:      return GX_TRIANGLES;
        case GL_TRIANGLE_STRIP: return GX_TRIANGLESTRIP;
        case GL_TRIANGLE_FAN:   return GX_TRIANGLEFAN;
        case GL_LINE_STRIP:     return GX_LINESTRIP;
        case GL_LINES:          return GX_LINES;
        default:                return GX_TRIANGLES;
    }
}

// Resolve an attribute's byte base pointer: when a GL_ARRAY_BUFFER was bound
// at glVertexAttribPointer time, `ptr` is a byte OFFSET into that buffer's
// CPU copy; otherwise `ptr` is a raw client pointer. Returns NULL when the
// buffer id is stale / empty.
const unsigned char* AttribBase(const ShimAttrib& a) {
    if (a.boundBuffer) {
        if (a.boundBuffer >= (unsigned)kMaxBuffers) return NULL;
        const ShimBuffer& b = g_Buffers[a.boundBuffer];
        if (!b.used || !b.data) return NULL;
        return b.data + (size_t)(uintptr_t)a.ptr;
    }
    return (const unsigned char*)a.ptr;
}

// Effective stride for an attribute: GL treats stride==0 as "tightly packed",
// i.e. the size of one element.
int AttribStride(const ShimAttrib& a) {
    if (a.stride != 0) return a.stride;
    int comp = (a.type == GL_UNSIGNED_BYTE) ? 1 : 4;   // bytes per component
    return a.size * comp;
}

// True when the current draw's MVP is a perspective (non-affine) transform:
// EmitVertex then does the full MVP*pos + perspective divide on the CPU and
// emits NDC, since GX's 3x4 modelview can't carry the perspective w.
bool g_SoftwareXform = false;

// Set up GX vertex descriptor + attribute formats + TEV/channel state for the
// current draw, based on which shim attribs are enabled and whether a texture
// is bound. Must be called BEFORE GX_Begin.
void SetupGxDrawState(bool haveUV, bool haveColor, bool textured) {
    // --- Matrix. GX_LoadProjectionMtx does NOT do a general 4x4 multiply -- for
    // GX_PERSPECTIVE/ORTHOGRAPHIC it extracts only the ~6 frustum elements and
    // assumes a standard projection shape. The ES2 shader hands us a COMBINED
    // MVP (proj*view*world) that already maps vertex -> clip/NDC, so cramming it
    // into the projection slot mangles the transform. Instead load the MVP as
    // the POSITION modelview (3x4 affine; exact for the 2D-ortho UI where the
    // MVP's 4th row is [0,0,0,1]) and use an identity passthrough projection so
    // clip = I * (MVP * pos) = MVP*pos.
    // TODO(wii): 3D perspective meshes (DrawMesh3D) have a non-affine MVP (4th
    // row != [0,0,0,1]); the perspective w is lost in the 3x4 here -- needs a
    // separate proj/modelview split for those draws.
    // Detect a non-affine (perspective) MVP. In GL column-major layout the 4th
    // row is at indices 3,7,11,15; an affine 2D-ortho MVP has [0,0,0,1] there.
    // 3D meshes (DrawMesh3D) carry a perspective row -> the 3x4 modelview below
    // can't hold the perspective w, so those are transformed on the CPU in
    // EmitVertex (full MVP * pos + perspective divide -> NDC) with an identity
    // modelview here. Affine 2D draws keep the fast GX-hardware transform.
    {
        float r3a = g_ShimMVP[3],  r3b = g_ShimMVP[7];
        float r3c = g_ShimMVP[11], r3d = g_ShimMVP[15];
        if (r3a < 0) r3a = -r3a; if (r3b < 0) r3b = -r3b; if (r3c < 0) r3c = -r3c;
        float d = r3d - 1.0f; if (d < 0) d = -d;
        g_SoftwareXform = (r3a > 1e-5f || r3b > 1e-5f || r3c > 1e-5f || d > 1e-5f);
    }

    Mtx mv;
    if (g_SoftwareXform) {
        guMtxIdentity(mv);                        // EmitVertex emits NDC directly
    } else {
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c)
                mv[r][c] = g_ShimMVP[c * 4 + r];  // col-major GL -> row-major GX, rows 0-2
    }
    GX_LoadPosMtxImm(mv, GX_PNMTX0);

    // Identity x/y passthrough, but remap z: NDC z is GL [-1,1] while GX clips
    // at [-1,0]. z' = 0.5*z - 0.5*w maps [-1,1] -> [-1,0] so geometry at z=0
    // (-> -0.5) isn't clipped away.
    Mtx44 proj;
    memset(proj, 0, sizeof(proj));
    proj[0][0] = 1.0f;
    proj[1][1] = 1.0f;
    proj[2][2] = 0.5f; proj[2][3] = -0.5f;
    proj[3][3] = 1.0f;
    GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);

    // --- Vertex descriptor.
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    if (haveUV) {
        GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
        GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    }
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    (void)haveColor;   // CLR0 always emitted (const white when array off)

    // --- Colour channel: pass the vertex colour straight through, no lighting.
    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX,
                   0, GX_DF_NONE, GX_AF_NONE);

    // --- TEV: GL_MODULATE = texel * vertex colour when textured; PASSCLR
    // (vertex colour only) when untextured.
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

// Emit one vertex worth of GX immediate-mode data for logical vertex `idx`.
// posBase/uvBase/colBase already resolved to CPU byte pointers (or NULL). The
// enabled flags mirror g_ShimAttrib[]; when an array is off the tracked
// glVertexAttrib4f constant is used instead.
void EmitVertex(int idx,
                const unsigned char* posBase, int posStride, int posSize,
                bool haveUV, const unsigned char* uvBase, int uvStride,
                bool haveColor, const unsigned char* colBase, int colStride,
                unsigned colType) {
    // Position.
    const float* p = (const float*)(posBase + (size_t)posStride * idx);
    float x = p[0];
    float y = p[1];
    float z = (posSize >= 3) ? p[2] : 0.0f;
    if (g_SoftwareXform) {
        // Perspective MVP: transform + divide on the CPU, emit NDC (GX modelview
        // is identity for this draw). g_ShimMVP is GL column-major.
        const float* m = g_ShimMVP;
        float cx = m[0]*x + m[4]*y + m[8]*z  + m[12];
        float cy = m[1]*x + m[5]*y + m[9]*z  + m[13];
        float cz = m[2]*x + m[6]*y + m[10]*z + m[14];
        float cw = m[3]*x + m[7]*y + m[11]*z + m[15];
        if (cw != 0.0f) { float inv = 1.0f / cw; cx *= inv; cy *= inv; cz *= inv; }
        GX_Position3f32(cx, cy, cz);
    } else {
        GX_Position3f32(x, y, z);
    }

    // Colour (always emitted -- CLR0 is GX_DIRECT).
    u8 cr = 255, cg = 255, cb = 255, ca = 255;
    if (haveColor && colBase) {
        if (colType == GL_UNSIGNED_BYTE) {
            const unsigned char* c = colBase + (size_t)colStride * idx;
            cr = c[0]; cg = c[1]; cb = c[2]; ca = c[3];
        } else {
            const float* c = (const float*)(colBase + (size_t)colStride * idx);
            cr = (u8)(c[0] * 255.0f);
            cg = (u8)(c[1] * 255.0f);
            cb = (u8)(c[2] * 255.0f);
            ca = (u8)(c[3] * 255.0f);
        }
    } else {
        const float* c = g_ShimConstAttrib[2];
        cr = (u8)(c[0] * 255.0f);
        cg = (u8)(c[1] * 255.0f);
        cb = (u8)(c[2] * 255.0f);
        ca = (u8)(c[3] * 255.0f);
    }
    GX_Color4u8(cr, cg, cb, ca);

    // Texcoord (only when TEX0 is in the descriptor).
    if (haveUV) {
        if (uvBase) {
            const float* uv = (const float*)(uvBase + (size_t)uvStride * idx);
            GX_TexCoord2f32(uv[0], uv[1]);
        } else {
            const float* uv = g_ShimConstAttrib[1];
            GX_TexCoord2f32(uv[0], uv[1]);
        }
    }
}

} // namespace

extern "C" {

// ===========================================================================
// Frame / global state
// ===========================================================================

const GLubyte* glGetString(GLenum name) {
    switch (name) {
        case GL_VENDOR:   return (const GLubyte*)"Nintendo";
        case GL_RENDERER: return (const GLubyte*)"GX (libogc)";
        case GL_VERSION:  return (const GLubyte*)"GL-on-GX shim 1.0";
        default:          return (const GLubyte*)"";
    }
}

GLenum glGetError(void) { return GL_NO_ERROR; }

void glViewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    g_ShimViewport[0] = x;
    g_ShimViewport[1] = y;
    g_ShimViewport[2] = w;
    g_ShimViewport[3] = h;
    // GX viewport origin is top-left; GL's is bottom-left. The stored xfb
    // height is needed to flip, which DisplayManagerWii owns -- for the boot
    // pass we set the GX viewport in mainWii's one-time setup and leave the
    // per-frame GL viewport as bookkeeping only.
    // TODO(wii Pass 2): GX_SetViewport(x, xfbHeight-y-h, w, h, 0, 1) with the
    // GL->GX Y flip once per-frame viewport changes need to take effect.
}

void glGetIntegerv(GLenum pname, GLint* params) {
    if (!params) return;
    if (pname == GL_VIEWPORT) {
        params[0] = g_ShimViewport[0];
        params[1] = g_ShimViewport[1];
        params[2] = g_ShimViewport[2];
        params[3] = g_ShimViewport[3];
    } else {
        params[0] = 0;
    }
}

void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    g_ShimClearColor.r = (u8)(r * 255.0f);
    g_ShimClearColor.g = (u8)(g * 255.0f);
    g_ShimClearColor.b = (u8)(b * 255.0f);
    g_ShimClearColor.a = (u8)(a * 255.0f);
    GX_SetCopyClear(g_ShimClearColor, GX_MAX_Z24);
}

void glClearDepthf(GLclampf /*d*/) {
    // GX depth clear is folded into GX_SetCopyClear's Z arg (always max Z);
    // nothing per-value to do here.
}

void glClear(GLbitfield /*mask*/) {
    // GX has no glClear -- the framebuffer clear happens via GX_SetCopyClear
    // + GX_CopyDisp during the swap (DisplayManagerWii::SwapBuffers). No-op.
}

void glEnable(GLenum cap) {
    switch (cap) {
        case GL_BLEND:
            GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
            break;
        case GL_CULL_FACE:
            GX_SetCullMode(GX_CULL_BACK);
            break;
        case GL_DEPTH_TEST:
            GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
            break;
        case GL_SCISSOR_TEST:
            // Scissor rect is applied in glScissor; enabling is implicit.
            break;
        default:
            break;
    }
}

void glDisable(GLenum cap) {
    switch (cap) {
        case GL_BLEND:
            GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
            break;
        case GL_CULL_FACE:
            GX_SetCullMode(GX_CULL_NONE);
            break;
        case GL_DEPTH_TEST:
            GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
            break;
        case GL_SCISSOR_TEST:
            // TODO(wii Pass 2): restore full-EFB scissor when disabled.
            break;
        default:
            break;
    }
}

void glCullFace(GLenum face) {
    GX_SetCullMode(face == GL_FRONT ? GX_CULL_FRONT : GX_CULL_BACK);
}

void glBlendFunc(GLenum /*src*/, GLenum /*dst*/) {
    // Only ever called with (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) in this
    // port; glEnable(GL_BLEND) already sets that mode. Full src/dst mapping
    // deferred.
    // TODO(wii Pass 2): map arbitrary src/dst factors to GX_SetBlendMode.
}

void glDepthFunc(GLenum func) {
    u8 gxFunc = (func == GL_LESS) ? GX_LESS
              : (func == GL_LEQUAL) ? GX_LEQUAL
              : GX_ALWAYS;
    GX_SetZMode(GX_TRUE, gxFunc, GX_TRUE);
}

void glDepthMask(GLboolean flag) {
    // Re-issue Z mode with the requested write-enable; keep LEQUAL as the
    // compare (the only compare this port uses alongside depth writes).
    GX_SetZMode(GX_TRUE, GX_LEQUAL, flag ? GX_TRUE : GX_FALSE);
}

void glScissor(GLint x, GLint y, GLsizei w, GLsizei h) {
    // GL scissor origin is bottom-left; GX is top-left. Flip needs xfb
    // height (DisplayManagerWii owns it). For the boot pass, set the scissor
    // in GL coords directly -- nothing draws yet so the exact rect is moot.
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    // TODO(wii Pass 2): GX_SetScissor(x, xfbHeight-y-h, w, h) with the Y flip.
    GX_SetScissor((u32)x, (u32)y, (u32)w, (u32)h);
}

void glPixelStorei(GLenum /*pname*/, GLint /*param*/) {
    // Row alignment is irrelevant to the GX tiled upload path. No-op.
}

void glReadPixels(GLint /*x*/, GLint /*y*/, GLsizei w, GLsizei h,
                  GLenum /*format*/, GLenum /*type*/, void* data) {
    // TODO(wii Pass 2): EFB->texture copy + detile for real screenshots.
    if (data && w > 0 && h > 0) {
        memset(data, 0, (size_t)w * h * 4);
    }
}

void glFinish(void) { GX_Flush(); }
void glFlush(void)  { GX_Flush(); }

void glPolygonMode(GLenum /*face*/, GLenum /*mode*/) {
    // Wireframe debug toggle has no GX equivalent. No-op.
}

// ===========================================================================
// Textures
// ===========================================================================

void glGenTextures(GLsizei n, GLuint* ids) {
    for (GLsizei i = 0; i < n; ++i) {
        unsigned id = NextFreeTexture();
        if (id) {
            ShimTexture& t = g_Textures[id];
            memset(&t, 0, sizeof(t));
            t.used      = true;
            t.minFilter = GL_LINEAR;
            t.magFilter = GL_LINEAR;
            t.wrapS     = GL_REPEAT;
            t.wrapT     = GL_REPEAT;
        }
        ids[i] = id;
    }
}

void glDeleteTextures(GLsizei n, const GLuint* ids) {
    for (GLsizei i = 0; i < n; ++i) {
        unsigned id = ids[i];
        if (id && id < (unsigned)kMaxTextures && g_Textures[id].used) {
            if (g_Textures[id].texels) free(g_Textures[id].texels);
            memset(&g_Textures[id], 0, sizeof(ShimTexture));
        }
    }
}

void glBindTexture(GLenum /*target*/, GLuint texture) {
    g_BoundTexture = texture;
}

void glTexParameteri(GLenum /*target*/, GLenum pname, GLint param) {
    if (!g_BoundTexture || g_BoundTexture >= (unsigned)kMaxTextures) return;
    ShimTexture& t = g_Textures[g_BoundTexture];
    switch (pname) {
        case GL_TEXTURE_MIN_FILTER: t.minFilter = param; break;
        case GL_TEXTURE_MAG_FILTER: t.magFilter = param; break;
        case GL_TEXTURE_WRAP_S:     t.wrapS     = param; break;
        case GL_TEXTURE_WRAP_T:     t.wrapT     = param; break;
        default: break;
    }
}

void glTexImage2D(GLenum /*target*/, GLint /*level*/, GLint /*internalFormat*/,
                  GLsizei width, GLsizei height, GLint /*border*/,
                  GLenum format, GLenum type, const void* pixels) {
    if (!g_BoundTexture || g_BoundTexture >= (unsigned)kMaxTextures) return;
    ShimTexture& t = g_Textures[g_BoundTexture];

    unsigned char* rgba = ExpandToRGBA8(width, height, format, type, pixels);
    // pixels may legitimately be NULL for a placeholder alloc; expand yields
    // NULL then and we leave the texture without an image this pass.
    if (t.texels) { free(t.texels); t.texels = NULL; }
    t.width  = width;
    t.height = height;
    t.hasImage = false;

    if (rgba) {
        u32 bufSize = GX_GetTexBufferSize(width, height, GX_TF_RGBA8, GX_FALSE, 0);
        void* tiled = memalign(32, bufSize);
        if (tiled) {
            TileRGBA8(rgba, tiled, width, height);
            DCFlushRange(tiled, bufSize);
            GX_InitTexObj(&t.obj, tiled, (u16)width, (u16)height,
                          GX_TF_RGBA8, GxWrap(t.wrapS), GxWrap(t.wrapT), GX_FALSE);
            GX_InitTexObjFilterMode(&t.obj, GxFilter(t.minFilter), GxFilter(t.magFilter));
            t.texels   = tiled;
            t.hasImage = true;
        }
        free(rgba);
    }
}

void glTexSubImage2D(GLenum /*target*/, GLint /*level*/, GLint /*xoff*/,
                     GLint /*yoff*/, GLsizei /*w*/, GLsizei /*h*/,
                     GLenum /*format*/, GLenum /*type*/, const void* /*pixels*/) {
    // TODO(wii Pass 2): partial retile into the bound texture's GX buffer
    // (used by the dynamic TTF glyph cache, FontInterface.cpp). No-op boot.
}

void glCompressedTexImage2D(GLenum /*target*/, GLint /*level*/, GLenum /*fmt*/,
                            GLsizei /*w*/, GLsizei /*h*/, GLint /*border*/,
                            GLsizei /*imgSize*/, const void* /*data*/) {
    // TODO(wii Pass 2): map compressed formats to GX_TF_CMPR where possible.
    // Not exercised by the Bada asset set. No-op boot.
}

void glActiveTexture(GLenum tex) {
    g_ActiveTexUnit = tex - GL_TEXTURE0;
}

// ===========================================================================
// Buffers
// ===========================================================================

void glGenBuffers(GLsizei n, GLuint* ids) {
    for (GLsizei i = 0; i < n; ++i) {
        unsigned id = NextFreeBuffer();
        if (id) {
            g_Buffers[id].used = true;
            g_Buffers[id].data = NULL;
            g_Buffers[id].size = 0;
        }
        ids[i] = id;
    }
}

void glDeleteBuffers(GLsizei n, const GLuint* ids) {
    for (GLsizei i = 0; i < n; ++i) {
        unsigned id = ids[i];
        if (id && id < (unsigned)kMaxBuffers && g_Buffers[id].used) {
            if (g_Buffers[id].data) free(g_Buffers[id].data);
            g_Buffers[id].data = NULL;
            g_Buffers[id].size = 0;
            g_Buffers[id].used = false;
        }
    }
}

void glBindBuffer(GLenum target, GLuint buffer) {
    if (target == GL_ELEMENT_ARRAY_BUFFER) g_ElementBufferBinding = buffer;
    else                                   g_ArrayBufferBinding   = buffer;
}

void glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum /*usage*/) {
    unsigned id = (target == GL_ELEMENT_ARRAY_BUFFER)
                      ? g_ElementBufferBinding : g_ArrayBufferBinding;
    if (!id || id >= (unsigned)kMaxBuffers || !g_Buffers[id].used) return;
    ShimBuffer& b = g_Buffers[id];
    if (b.data) { free(b.data); b.data = NULL; }
    b.size = (long)size;
    if (size > 0) {
        b.data = (unsigned char*)malloc((size_t)size);
        if (b.data && data) memcpy(b.data, data, (size_t)size);
    }
}

// ===========================================================================
// Vertex attributes
// ===========================================================================

void glVertexAttribPointer(GLuint index, GLint size, GLenum type,
                           GLboolean /*normalized*/, GLsizei stride,
                           const void* pointer) {
    if (index >= 8) return;
    ShimAttrib& a = g_ShimAttrib[index];
    a.ptr         = pointer;
    a.size        = size;
    a.type        = type;
    a.stride      = stride;
    a.boundBuffer = g_ArrayBufferBinding;
}

void glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y,
                      GLfloat z, GLfloat w) {
    if (index >= 8) return;
    g_ShimConstAttrib[index][0] = x;
    g_ShimConstAttrib[index][1] = y;
    g_ShimConstAttrib[index][2] = z;
    g_ShimConstAttrib[index][3] = w;
}

void glEnableVertexAttribArray(GLuint index) {
    if (index < 8) g_ShimAttrib[index].enabled = true;
}

void glDisableVertexAttribArray(GLuint index) {
    if (index < 8) g_ShimAttrib[index].enabled = false;
}

// ===========================================================================
// Draw calls -- real GX immediate-mode rendering.
//
// Both paths share the same state setup (SetupGxDrawState) and per-vertex
// emit (EmitVertex): full 4x4 MVP loaded as the GX projection (identity pos
// modelview), a single GX_MODULATE / GX_PASSCLR TEV stage, and vertex attrs
// read directly out of the shim's client/bound-buffer CPU copies.
// ===========================================================================
namespace {

// Common preamble for both draw entry points. Resolves attribute bases and
// texture binding, sets GX state. Fills the out-params the emit loop needs.
// Returns false when position data is missing (nothing to draw).
bool BeginGxDraw(const unsigned char** posBase, int* posStride, int* posSize,
                 bool* haveUV, const unsigned char** uvBase, int* uvStride,
                 bool* haveColor, const unsigned char** colBase, int* colStride,
                 unsigned* colType) {
    const ShimAttrib& pos = g_ShimAttrib[0];
    const ShimAttrib& uv  = g_ShimAttrib[1];
    const ShimAttrib& col = g_ShimAttrib[2];

    if (!pos.enabled) return false;
    *posBase = AttribBase(pos);
    if (!*posBase) return false;
    *posStride = AttribStride(pos);
    *posSize   = pos.size;

    *haveUV = uv.enabled;
    *uvBase = *haveUV ? AttribBase(uv) : NULL;
    *uvStride = *haveUV ? AttribStride(uv) : 0;

    *haveColor = col.enabled;
    *colBase = *haveColor ? AttribBase(col) : NULL;
    *colStride = *haveColor ? AttribStride(col) : 0;
    *colType = col.type;

    // Textured when a real texture with an uploaded image is bound AND the UV
    // stream is present (either via the enabled array or a constant is moot
    // without texcoords -- GX needs a texgen, which needs UVs).
    bool textured = false;
    if (g_BoundTexture && g_BoundTexture < (unsigned)kMaxTextures) {
        const ShimTexture& t = g_Textures[g_BoundTexture];
        if (t.hasImage && *haveUV) {
            GX_LoadTexObj((GXTexObj*)&t.obj, GX_TEXMAP0);
            textured = true;
        }
    }
    // If we won't texture, drop the UV stream from the descriptor so GX
    // doesn't expect texcoords it has no texgen for.
    if (!textured) *haveUV = false;

    SetupGxDrawState(*haveUV, *haveColor, textured);
    return true;
}

} // namespace

void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    if (count <= 0) return;

    const unsigned char *posBase, *uvBase, *colBase;
    int posStride, posSize, uvStride, colStride;
    bool haveUV, haveColor;
    unsigned colType;
    if (!BeginGxDraw(&posBase, &posStride, &posSize, &haveUV, &uvBase, &uvStride,
                     &haveColor, &colBase, &colStride, &colType)) {
        return;
    }

    GX_Begin(GxPrim(mode), GX_VTXFMT0, (u16)count);
    for (int i = 0; i < count; ++i) {
        EmitVertex(first + i, posBase, posStride, posSize,
                   haveUV, uvBase, uvStride,
                   haveColor, colBase, colStride, colType);
    }
    GX_End();
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type,
                    const void* indices) {
    if (count <= 0) return;

    const unsigned char *posBase, *uvBase, *colBase;
    int posStride, posSize, uvStride, colStride;
    bool haveUV, haveColor;
    unsigned colType;
    if (!BeginGxDraw(&posBase, &posStride, &posSize, &haveUV, &uvBase, &uvStride,
                     &haveColor, &colBase, &colStride, &colType)) {
        return;
    }

    // Index source: byte offset into the bound GL_ELEMENT_ARRAY_BUFFER's CPU
    // copy, or a raw client pointer when no index buffer is bound.
    const unsigned char* idxBase;
    if (g_ElementBufferBinding && g_ElementBufferBinding < (unsigned)kMaxBuffers &&
        g_Buffers[g_ElementBufferBinding].used && g_Buffers[g_ElementBufferBinding].data) {
        idxBase = g_Buffers[g_ElementBufferBinding].data + (size_t)(uintptr_t)indices;
    } else {
        idxBase = (const unsigned char*)indices;
    }
    if (!idxBase) return;

    const unsigned short* idx16 = (const unsigned short*)idxBase;
    const unsigned char*   idx8 = idxBase;

    GX_Begin(GxPrim(mode), GX_VTXFMT0, (u16)count);
    for (int i = 0; i < count; ++i) {
        int vi = (type == GL_UNSIGNED_BYTE) ? (int)idx8[i] : (int)idx16[i];
        EmitVertex(vi, posBase, posStride, posSize,
                   haveUV, uvBase, uvStride,
                   haveColor, colBase, colStride, colType);
    }
    GX_End();
}

// ===========================================================================
// Shader objects -- NO-OP stubs (GX uses fixed TEV stages, not GLSL).
// ===========================================================================

GLuint glCreateShader(GLenum /*type*/) { return g_NextShaderId++; }
void glShaderSource(GLuint, GLsizei, const GLchar* const*, const GLint*) {}
void glCompileShader(GLuint) {}

void glGetShaderiv(GLuint, GLenum pname, GLint* params) {
    if (!params) return;
    if (pname == GL_COMPILE_STATUS)      *params = GL_TRUE;
    else if (pname == GL_INFO_LOG_LENGTH) *params = 0;
    else                                  *params = 0;
}

void glGetShaderInfoLog(GLuint, GLsizei /*bufSize*/, GLsizei* length, GLchar* infoLog) {
    if (length) *length = 0;
    if (infoLog) infoLog[0] = '\0';
}

GLuint glCreateProgram(void) { return g_NextProgramId++; }
void glAttachShader(GLuint, GLuint) {}
void glBindAttribLocation(GLuint, GLuint, const GLchar*) {}
void glLinkProgram(GLuint) {}

void glGetProgramiv(GLuint, GLenum pname, GLint* params) {
    if (!params) return;
    if (pname == GL_LINK_STATUS)          *params = GL_TRUE;
    else if (pname == GL_INFO_LOG_LENGTH)  *params = 0;
    else                                   *params = 0;
}

void glGetProgramInfoLog(GLuint, GLsizei /*bufSize*/, GLsizei* length, GLchar* infoLog) {
    if (length) *length = 0;
    if (infoLog) infoLog[0] = '\0';
}

void glUseProgram(GLuint) {}

GLint glGetUniformLocation(GLuint, const GLchar*) {
    // Hand out distinct non-negative fake ids so callers treat them as valid.
    return g_NextUniformId++;
}

void glUniformMatrix4fv(GLint /*location*/, GLsizei /*count*/,
                        GLboolean /*transpose*/, const GLfloat* value) {
    if (value) memcpy(g_ShimMVP, value, sizeof(float) * 16);
}

void glUniform1i(GLint /*location*/, GLint /*v0*/) {
    // Sampler unit assignment -- GX binds textures per draw; ignore.
}

void glDeleteShader(GLuint) {}
void glDeleteProgram(GLuint) {}

} // extern "C"

#endif // FRUIT_PLATFORM_WII
