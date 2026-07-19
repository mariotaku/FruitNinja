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
// viewport/scissor Y-flip, compressed textures) carry a
// // TODO(wii Pass 2): marker. (glReadPixels is intentionally absent -- its
// only caller is SDL/GL-only; see the note near glFinish.)
//
// Only compiled when FRUIT_PLATFORM_WII is set (see src/engine/CMakeLists.txt).
#ifdef FRUIT_PLATFORM_WII

#include "render/gl_compat.h"
#include "debug/Logger.h"

#include <gccore.h>
#include <ogc/gu.h>
#include <malloc.h>
#include <cstring>
#include <cstdio>
#include <stdint.h>

#include "render/gl_funcsWii.h"

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
    void*    texels;      // 32-aligned GX tiled data (GX_TF_RGBA8, or GX_TF_IA8
                          // when uploadFormat == GL_LUMINANCE_ALPHA)
    unsigned char* linear; // kept linear copy in uploadFormat's layout (RGBA8 or
                           // LA8), for glTexSubImage2D updates
    // GL format of the last glTexImage2D upload (GL_RGBA or GL_LUMINANCE_ALPHA).
    // glTexSubImage2D keys its linear-blit stride + re-tiler (TileRGBA8 vs
    // TileIA8) off this tag. 0 (memset default) until first upload.
    GLenum   uploadFormat;
    int      width;
    int      height;
    int      minFilter;
    int      magFilter;
    int      wrapS;
    int      wrapT;
    bool     hasImage;
    bool     used;
    // Set via Wii_KeepTextureLinear before the next glTexImage2D upload.
    // Only the TTF font atlas (FontInterface.cpp) issues glTexSubImage2D,
    // which needs `linear` to blit+re-tile from -- every other texture is
    // upload-once, so retaining a 2nd full-size linear copy for it is pure
    // memory waste (roughly doubles resident texture RAM). Defaults false;
    // zeroed by the memset(&t, 0, ...) in glGenTextures/glDeleteTextures.
    bool     keepLinear;
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

// Tiling format tag for TileRegion (below).
enum TileFmt { TILE_FMT_RGBA8, TILE_FMT_IA8 };

// Core GX tiler, shared by TileRGBA8/TileIA8 (full-texture) and
// glTexSubImage2D's partial re-tile. Writes ONLY the 4x4-texel tiles whose
// tile-space coordinates lie in [tileX0,tileX1) x [tileY0,tileY1) -- callers
// pass the full tile grid for a whole-texture retile, or a tile-aligned
// sub-rect to touch just the tiles a glyph update overlaps.
//
// `src` is the FULL linear image (w*h texels, RGBA8 R,G,B,A or LA8 [L][A]
// per fmt) -- always the whole linear buffer, never a cropped one, so
// partial-tile edges read correct neighbor texels straight from it.
// `dst` is the FULL GX-tiled buffer (GX_GetTexBufferSize()-sized); each tile
// is written at its natural offset tileIndex*bytesPerTile where
// tileIndex = (ty/4)*tilesPerRow + (tx/4), tilesPerRow = ceil(w/4) -- i.e.
// the standard GX_TF_RGBA8/IA8 tile raster order, so writing a subset of
// tiles is just a subset of the same address computation the full pass uses.
// bytesPerTile: 64 for RGBA8 (AR half + GB half, 32B each), 32 for IA8.
void TileRegion(const unsigned char* src, void* dst, int w, int h,
                TileFmt fmt, int tileX0, int tileY0, int tileX1, int tileY1) {
    const int tilesPerRow  = (w + 3) / 4;
    const int bytesPerTile = (fmt == TILE_FMT_RGBA8) ? 64 : 32;
    unsigned char* base = (unsigned char*)dst;

    for (int ty = tileY0; ty < tileY1; ty += 4) {
        for (int tx = tileX0; tx < tileX1; tx += 4) {
            int tileIndex = (ty / 4) * tilesPerRow + (tx / 4);
            unsigned char* d = base + (size_t)tileIndex * bytesPerTile;

            if (fmt == TILE_FMT_RGBA8) {
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
            } else {
                // GX_TF_IA8: one BE u16 per texel, memory order [A][I] --
                // alpha byte first, then intensity (mirrors the AR half above).
                for (int y = 0; y < 4; ++y) {
                    for (int x = 0; x < 4; ++x) {
                        int sx = tx + x, sy = ty + y;
                        unsigned char i = 0, a = 0;
                        if (sx < w && sy < h) {
                            const unsigned char* p = src + ((sy * w) + sx) * 2;
                            i = p[0];   // L
                            a = p[1];   // A
                        }
                        *d++ = a;
                        *d++ = i;
                    }
                }
            }
        }
    }
}

// Tile a linear RGBA8 source into GX_TF_RGBA8's 4x4-block, dual-cacheline
// layout (AR block then GB block per 4x4 tile). `src` is width*height*4
// bytes, R,G,B,A order. `dst` must be 32-byte aligned and
// GX_GetTexBufferSize()-sized. Delegates to TileRegion over the full tile grid.
void TileRGBA8(const unsigned char* src, void* dst, int w, int h) {
    int tilesW = (w + 3) & ~3;
    int tilesH = (h + 3) & ~3;
    TileRegion(src, dst, w, h, TILE_FMT_RGBA8, 0, 0, tilesW, tilesH);
}

// Tile a linear LA8 source (2 B/texel, [L][A] byte order -- the TTF font
// atlas's Wii layout, see FontInterface.cpp) into GX_TF_IA8's 4x4-tile layout:
// one BIG-ENDIAN u16 per texel, 16 texels x 2 bytes = 32 bytes per tile.
// GX_TF_IA8 texel memory order is [A][I] (BE u16 = (A << 8) | I), so the
// ALPHA byte is written first, then the INTENSITY byte -- mirrors TileRGBA8's
// AR half-tile writing A before R. Out-of-bounds texels pad to 0x0000
// (transparent black). `dst` must be 32-byte aligned and
// GX_GetTexBufferSize(w, h, GX_TF_IA8, ...)-sized. Delegates to TileRegion
// over the full tile grid.
void TileIA8(const unsigned char* src, void* dst, int w, int h) {
    int tilesW = (w + 3) & ~3;
    int tilesH = (h + 3) & ~3;
    TileRegion(src, dst, w, h, TILE_FMT_IA8, 0, 0, tilesW, tilesH);
}

// Expand an incoming (format,type) source to a temporary linear RGBA8 buffer.
// Only the direct byte formats are supported -- RGBA8 and RGB8. These are the
// only formats the LIVE Wii glTexImage2D callers use: the dynamic TTF font atlas
// (FontInterface), the 1x1 white fallback (RendererGX), and placeholder textures
// (TextureManager). All FILE-backed textures are pre-tiled to GXT1 at staging
// and upload natively via Wii_UploadTiledGX -- they never reach here -- so the
// old packed 16-bit (5551/565) CPU-decode paths are dead and removed: any packed
// or otherwise non-native format now fails loudly (log + NULL) instead of a
// silent CPU conversion. Returns a malloc'd w*h*4 buffer the caller must free.
unsigned char* ExpandToRGBA8(int w, int h, GLenum format, GLenum type,
                             const void* pixels) {
    if (!pixels || w <= 0 || h <= 0) return NULL;
    const int n = w * h;

    if (type == GL_UNSIGNED_BYTE && format == GL_RGBA) {
        unsigned char* out = (unsigned char*)malloc((size_t)n * 4);
        if (out) memcpy(out, pixels, (size_t)n * 4);
        return out;
    }
    if (type == GL_UNSIGNED_BYTE && format == GL_RGB) {
        unsigned char* out = (unsigned char*)malloc((size_t)n * 4);
        if (!out) return NULL;
        const unsigned char* s = (const unsigned char*)pixels;
        for (int i = 0; i < n; ++i) {
            out[i * 4 + 0] = s[i * 3 + 0];
            out[i * 4 + 1] = s[i * 3 + 1];
            out[i * 4 + 2] = s[i * 3 + 2];
            out[i * 4 + 3] = 255;
        }
        return out;
    }

    // Non-native format: on Wii every file texture is GXT1 (native GX), so this
    // should never happen. Fail loudly rather than CPU-convert.
    LOG_ERROR("gl_funcsWii", "glTexImage2D: unsupported non-native format=0x%04x "
              "type=0x%04x -- Wii uploads only RGBA8/RGB8 (assets are GXT1)",
              (unsigned)format, (unsigned)type);
    return NULL;
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
            // The vertex colour is a packed uint32 (r | g<<8 | b<<16 | a<<24),
            // NOT four independent bytes. Reading it as raw bytes reverses the
            // channels on big-endian (particles swizzle, alpha lands in R so
            // semi-transparent quads read alpha=0 and vanish). Reconstruct the
            // native uint32 via memcpy and extract -- correct on both endians.
            uint32_t v;
            memcpy(&v, colBase + (size_t)colStride * idx, 4);
            cr = (u8)(v & 0xFFu);
            cg = (u8)((v >> 8) & 0xFFu);
            cb = (u8)((v >> 16) & 0xFFu);
            ca = (u8)((v >> 24) & 0xFFu);
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

// ===========================================================================
// RendererGX seam accessors (gl_funcsWii.h) -- plain C++ linkage, not part of
// the GL-shaped extern "C" block below. Let RendererGX.cpp reach into this
// shim's texture/buffer/viewport state directly for native GX immediate-mode
// drawing (see RendererGX.cpp's file-top comment for why it bypasses the
// glDrawArrays/glDrawElements shim path).
// ===========================================================================

GXTexObj* Wii_GetTexObj(unsigned int glTexId) {
    if (!glTexId || glTexId >= (unsigned)kMaxTextures) return NULL;
    ShimTexture& t = g_Textures[glTexId];
    if (!t.used || !t.hasImage) return NULL;
    return (GXTexObj*)&t.obj;
}

const void* Wii_GetBufferData(unsigned int glBufId, unsigned int* outSize) {
    if (outSize) *outSize = 0;
    if (!glBufId || glBufId >= (unsigned)kMaxBuffers) return NULL;
    ShimBuffer& b = g_Buffers[glBufId];
    if (!b.used || !b.data) return NULL;
    if (outSize) *outSize = (unsigned)b.size;
    return b.data;
}

void Wii_GetViewport(int vp[4]) {
    vp[0] = g_ShimViewport[0];
    vp[1] = g_ShimViewport[1];
    vp[2] = g_ShimViewport[2];
    vp[3] = g_ShimViewport[3];
}

unsigned int Wii_GetBoundTexture() {
    return g_BoundTexture;
}

void Wii_KeepTextureLinear(unsigned int glTexId) {
    if (!glTexId || glTexId >= (unsigned)kMaxTextures) return;
    g_Textures[glTexId].keepLinear = true;
}

void Wii_UploadTiledGX(unsigned int glTexId, const void* tiled,
                       unsigned int tiledSize, int w, int h,
                       unsigned int gxFmt) {
    // Port specific: direct upload of pre-tiled GX texel data (the "GXT1"
    // container, staged by stage-assets.py --wii for all transcodable Tex1
    // game textures + widget art). Mirrors the tail of glTexImage2D minus
    // ExpandToRGBA8/TileRGBA8 -- the file bytes are already in the target
    // format's GX tile layout, so no runtime decode or tiling.
    if (!glTexId || glTexId >= (unsigned)kMaxTextures) return;
    ShimTexture& t = g_Textures[glTexId];

    if (t.texels) { free(t.texels); t.texels = NULL; }
    if (t.linear) { free(t.linear); t.linear = NULL; }
    t.width    = w;
    t.height   = h;
    t.hasImage = false;
    if (!tiled || w <= 0 || h <= 0) return;
    if (gxFmt != GX_TF_RGB565 && gxFmt != GX_TF_RGB5A3 && gxFmt != GX_TF_RGBA8) {
        static bool warnedFmt = false;
        if (!warnedFmt) {
            warnedFmt = true;
            LOG_WARN("gl_funcsWii", "Wii_UploadTiledGX: unsupported GX format %u for "
                     "tex %u -- texture left untextured (one-shot warning)",
                     gxFmt, glTexId);
        }
        return;
    }

    u32 bufSize = GX_GetTexBufferSize((u16)w, (u16)h, gxFmt, GX_FALSE, 0);
    void* buf = memalign(32, bufSize);
    if (!buf) {
        static bool warnedOOM = false;
        if (!warnedOOM) {
            warnedOOM = true;
            LOG_WARN("gl_funcsWii", "Wii_UploadTiledGX: memalign(bufSize=%u) failed for "
                     "tex %u (%dx%d) -- texture left untextured (one-shot warning)",
                     (unsigned)bufSize, glTexId, w, h);
        }
        return;
    }
    // File length should equal bufSize (encoder emits exactly
    // ceil(w/4)*ceil(h/4) * 64 (RGBA8) or 32 (16bpp) bytes =
    // GX_GetTexBufferSize); clamp defensively.
    memcpy(buf, tiled, tiledSize <= bufSize ? tiledSize : bufSize);
    DCFlushRange(buf, bufSize);
    GX_InvalidateTexAll();
    GX_InitTexObj(&t.obj, buf, (u16)w, (u16)h, (u8)gxFmt,
                  GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&t.obj, GX_LINEAR, GX_LINEAR);
    t.texels     = buf;
    t.hasImage   = true;
    t.keepLinear = false;
    t.linear     = NULL;
}

extern "C" {

// ===========================================================================
// Frame / global state
// ===========================================================================

// glGetString / glGetError are intentionally absent on the GX shim: their only
// callers (GL-renderer logging in mainSDL/mainEmscripten/gl_funcsSDL, GL error
// checks) are SDL/GL-only and never compiled on Wii.

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

// glClearColor is intentionally absent on the GX shim: no Wii code path calls
// it (the EFB clear colour is set once at init via GX_SetCopyClear, mainWii.cpp).

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
            // GX scissor is always active (no test-enable toggle), so a prior
            // glScissor rect would leak into every subsequent draw. Restore the
            // full viewport rect here so glDisable(GL_SCISSOR_TEST) matches GL
            // semantics -- without this the AboutScreen credits' clip rect
            // stayed active and clipped the whole rest of the frame.
            GX_SetScissor((u32)g_ShimViewport[0], (u32)g_ShimViewport[1],
                          (u32)g_ShimViewport[2], (u32)g_ShimViewport[3]);
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
    // GL scissor origin is bottom-left; GX_SetScissor is top-left. Flip Y
    // using the viewport height (g_ShimViewport[3]): the rect's GX top edge =
    // vpH - (glScissor y + h). Without this, worldspace-clipped content
    // (e.g. AboutScreen credits via BakedStringBox's glScissor) is clipped in
    // the wrong vertical band.
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    int flippedY = g_ShimViewport[3] - y - h;
    if (flippedY < 0) flippedY = 0;
    GX_SetScissor((u32)x, (u32)flippedY, (u32)w, (u32)h);
}

void glPixelStorei(GLenum /*pname*/, GLint /*param*/) {
    // Row alignment is irrelevant to the GX tiled upload path. No-op.
}

// glReadPixels is intentionally NOT implemented on Wii: its only caller is
// GameSDL.cpp's do_screenshot_if_requested (F12 GL readback + SDL_SaveBMP),
// which is SDL/GL-only and never compiled on the Wii target (GameWii.cpp is
// used instead, see CMakeLists.txt). No Wii code path reads back the EFB.

// glFinish / glFlush / glPolygonMode are intentionally absent on the GX shim:
// none are called by Wii-compiled code (glFinish/glFlush have no callers at all;
// glPolygonMode's wireframe toggle is RendererGL/GameSDL-only).

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
            if (g_Textures[id].linear) free(g_Textures[id].linear);
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

    if (format == GL_LUMINANCE_ALPHA && type == GL_UNSIGNED_BYTE) {
        // LA8 path -- the TTF glyph atlas (FontInterface.cpp's Wii layout,
        // 2 B/texel [L][A]). Already native-sized for GX_TF_IA8, so no
        // ExpandToRGBA8 (that helper stays RGBA/RGB-only): copy the linear
        // bytes as-is and tile with TileIA8.
        if (t.texels) { free(t.texels); t.texels = NULL; }
        if (t.linear) { free(t.linear); t.linear = NULL; }
        t.width  = width;
        t.height = height;
        t.hasImage = false;
        t.uploadFormat = GL_LUMINANCE_ALPHA;
        if (!pixels || width <= 0 || height <= 0) return;

        unsigned char* la8 = (unsigned char*)malloc((size_t)width * height * 2);
        if (!la8) return;
        memcpy(la8, pixels, (size_t)width * height * 2);

        u32 bufSize = GX_GetTexBufferSize(width, height, GX_TF_IA8, GX_FALSE, 0);
        void* tiled = memalign(32, bufSize);
        if (tiled) {
            TileIA8(la8, tiled, width, height);
            DCFlushRange(tiled, bufSize);
            GX_InvalidateTexAll();
            GX_InitTexObj(&t.obj, tiled, (u16)width, (u16)height,
                          GX_TF_IA8, GxWrap(t.wrapS), GxWrap(t.wrapT), GX_FALSE);
            GX_InitTexObjFilterMode(&t.obj, GxFilter(t.minFilter), GxFilter(t.magFilter));
            t.texels   = tiled;
            t.hasImage = true;
        } else {
            LOG_WARN("gl_funcsWii", "glTexImage2D: memalign(bufSize=%u) failed for "
                     "IA8 tex %u (%dx%d) -- texture left untextured",
                     (unsigned)bufSize, g_BoundTexture, (int)width, (int)height);
        }
        if (t.keepLinear) {
            // TTF atlas: glTexSubImage2D blits+re-tiles from this copy.
            t.linear = la8;
        } else {
            free(la8);
        }
        return;
    }

    unsigned char* rgba = ExpandToRGBA8(width, height, format, type, pixels);
    // pixels may legitimately be NULL for a placeholder alloc; expand yields
    // NULL then and we leave the texture without an image this pass.
    if (t.texels) { free(t.texels); t.texels = NULL; }
    if (t.linear) { free(t.linear); t.linear = NULL; }
    t.width  = width;
    t.height = height;
    t.hasImage = false;
    t.uploadFormat = GL_RGBA;

    if (rgba) {
        u32 bufSize = GX_GetTexBufferSize(width, height, GX_TF_RGBA8, GX_FALSE, 0);
        void* tiled = memalign(32, bufSize);
        if (tiled) {
            TileRGBA8(rgba, tiled, width, height);
            DCFlushRange(tiled, bufSize);
            // A freed heap address can be reused by a new texture; TMEM may
            // still hold the old occupant's texels. glTexSubImage2D already
            // invalidates after its re-tile (below) -- mirror that here.
            GX_InvalidateTexAll();
            GX_InitTexObj(&t.obj, tiled, (u16)width, (u16)height,
                          GX_TF_RGBA8, GxWrap(t.wrapS), GxWrap(t.wrapT), GX_FALSE);
            GX_InitTexObjFilterMode(&t.obj, GxFilter(t.minFilter), GxFilter(t.magFilter));
            t.texels   = tiled;
            t.hasImage = true;
        } else {
            static bool warnedOOM = false;
            if (!warnedOOM) {
                warnedOOM = true;
                LOG_WARN("gl_funcsWii", "glTexImage2D: memalign(bufSize=%u) failed for "
                         "tex %u (%dx%d) -- texture left untextured (one-shot warning)",
                         (unsigned)bufSize, g_BoundTexture, (int)width, (int)height);
            }
        }
        if (t.keepLinear) {
            // TTF atlas: glTexSubImage2D blits+re-tiles from this copy.
            t.linear = rgba;
        } else {
            // No sub-image consumer for this texture -- don't retain a 2nd
            // full-size linear copy on top of the tiled GX buffer.
            free(rgba);
        }
    }
}

void glTexSubImage2D(GLenum /*target*/, GLint /*level*/, GLint xoff,
                     GLint yoff, GLsizei w, GLsizei h,
                     GLenum format, GLenum type, const void* pixels) {
    // Update a sub-rect of the bound texture (the dynamic TTF glyph atlas,
    // FontInterface.cpp). Blit the new pixels into the kept linear copy (so
    // partial-tile edges still read correct neighbor texels), then re-tile
    // ONLY the 4x4 GX tiles the sub-rect overlaps -- for the 512x512 atlas a
    // single small glyph would otherwise re-swizzle the entire page.
    if (!g_BoundTexture || g_BoundTexture >= (unsigned)kMaxTextures) return;
    ShimTexture& t = g_Textures[g_BoundTexture];
    if (!t.linear || !t.texels || !pixels || w <= 0 || h <= 0) return;
    if (xoff < 0 || yoff < 0 || xoff + w > t.width || yoff + h > t.height) return;

    // Tile-aligned bounds of the touched region: round the sub-rect out to
    // the enclosing 4x4 tile grid, clamped to the texture extents.
    int tileX0 = xoff & ~3;
    int tileY0 = yoff & ~3;
    int tileX1 = (xoff + w + 3) & ~3;
    int tileY1 = (yoff + h + 3) & ~3;
    if (tileX1 > t.width)  tileX1 = (t.width  + 3) & ~3;
    if (tileY1 > t.height) tileY1 = (t.height + 3) & ~3;

    if (t.uploadFormat == GL_LUMINANCE_ALPHA) {
        // LA8 atlas: incoming sub-rect is already 2 B/texel [L][A] (same
        // layout as t.linear) -- blit rows at the 2-byte stride, re-tile only
        // the touched tiles with TileRegion(TILE_FMT_IA8).
        if (format != GL_LUMINANCE_ALPHA || type != GL_UNSIGNED_BYTE) return;
        const unsigned char* src8 = (const unsigned char*)pixels;
        for (int row = 0; row < h; ++row) {
            unsigned char* dst = t.linear + (((size_t)(yoff + row) * t.width + xoff) * 2);
            memcpy(dst, src8 + (size_t)row * w * 2, (size_t)w * 2);
        }
        TileRegion(t.linear, t.texels, t.width, t.height, TILE_FMT_IA8,
                  tileX0, tileY0, tileX1, tileY1);
        // Flushing the whole buffer is cheap next to a full re-tile; keeps
        // the DCFlushRange call simple vs. computing the touched byte span.
        u32 bufSize = GX_GetTexBufferSize(t.width, t.height, GX_TF_IA8, GX_FALSE, 0);
        DCFlushRange(t.texels, bufSize);
        GX_InvalidateTexAll();
        return;
    }

    unsigned char* sub = ExpandToRGBA8(w, h, format, type, pixels);
    if (!sub) return;
    for (int row = 0; row < h; ++row) {
        unsigned char* dst = t.linear + (((size_t)(yoff + row) * t.width + xoff) * 4);
        const unsigned char* src = sub + ((size_t)row * w * 4);
        memcpy(dst, src, (size_t)w * 4);
    }
    free(sub);

    TileRegion(t.linear, t.texels, t.width, t.height, TILE_FMT_RGBA8,
              tileX0, tileY0, tileX1, tileY1);
    u32 bufSize = GX_GetTexBufferSize(t.width, t.height, GX_TF_RGBA8, GX_FALSE, 0);
    DCFlushRange(t.texels, bufSize);
    GX_InvalidateTexAll();
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
