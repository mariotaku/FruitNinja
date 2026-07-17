// Port specific: GL-on-GX translation shim for the Wii backend.
//
// Implements every gl* entry point the core render / asset / particle TUs
// call (see render/gl_compat.h's FRUIT_PLATFORM_WII branch), backed by
// libogc's GX. This lets Renderer.cpp / DisplayManager.cpp / Texture.cpp /
// ShaderProgram.cpp / Shaders.cpp / Geometry.cpp / MeshManager.cpp compile
// and link UNCHANGED against the Wii target.
//
// PASS 1 SCOPE (this file): LINK + BOOT to a cleared screen. Textures and
// buffers are really uploaded/converted so Pass 2's GX draw has the data it
// needs, but the actual draw calls (glDrawArrays / glDrawElements) are
// no-ops -- nothing is rasterized yet. Shader objects are no-op stubs (GX
// uses fixed TEV stages, not GLSL). Every deferred piece carries a
// // TODO(wii Pass 2): marker.
//
// Only compiled when FRUIT_PLATFORM_WII is set (see src/engine/CMakeLists.txt).
#ifdef FRUIT_PLATFORM_WII

#include "render/gl_compat.h"

#include <gccore.h>
#include <malloc.h>
#include <cstring>
#include <cstdio>

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

void glVertexAttrib4f(GLuint /*index*/, GLfloat /*x*/, GLfloat /*y*/,
                      GLfloat /*z*/, GLfloat /*w*/) {
    // TODO(wii Pass 2): constant vertex attribute (used when an attrib array
    // is disabled). No-op boot.
}

void glEnableVertexAttribArray(GLuint index) {
    if (index < 8) g_ShimAttrib[index].enabled = true;
}

void glDisableVertexAttribArray(GLuint index) {
    if (index < 8) g_ShimAttrib[index].enabled = false;
}

// ===========================================================================
// Draw calls -- STUBBED for this pass (boot to a cleared screen).
// ===========================================================================

void glDrawArrays(GLenum /*mode*/, GLint /*first*/, GLsizei /*count*/) {
    // TODO(wii Pass 2): GX immediate draw from g_ShimAttrib + g_ShimMVP +
    // bound tex (g_BoundTexture). Emit a GX_Begin/GX_End batch reading the
    // attribute table's CPU pointers (buffer id 0 => client array, else the
    // g_Buffers[] copy). No-op for now.
}

void glDrawElements(GLenum /*mode*/, GLsizei /*count*/, GLenum /*type*/,
                    const void* /*indices*/) {
    // TODO(wii Pass 2): GX indexed draw. `indices` is a byte offset into the
    // bound GL_ELEMENT_ARRAY_BUFFER (g_Buffers[g_ElementBufferBinding]) when
    // an index buffer is bound. No-op for now.
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
