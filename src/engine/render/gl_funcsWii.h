#ifndef MORTAR_GL_FUNCS_WII_H
#define MORTAR_GL_FUNCS_WII_H

// Port specific: seam between gl_funcsWii.cpp's GL-on-GX shim state (texture
// registry, buffer-object registry, viewport bookkeeping) and RendererGX.cpp,
// which needs raw access to that state to drive GX directly (native
// proj/modelview load + immediate-mode GX_Begin/End) instead of going through
// the generic glDrawArrays/glDrawElements shim path in gl_funcsWii.cpp.
//
// Only meaningful under FRUIT_PLATFORM_WII; declared here (not gl_compat.h)
// because gl_compat.h is scoped to GL-signature-shaped declarations shared
// conceptually across every backend, while these are Wii-shim-internal
// accessors with no GL equivalent.
#ifdef FRUIT_PLATFORM_WII

#include "render/gl_compat.h"

// Requires <gccore.h> (for the real libogc GXTexObj typedef) to already be
// included by the TU that includes this header -- both gl_funcsWii.cpp and
// RendererGX.cpp include <gccore.h> before this header. Not included here
// directly to keep this a light seam header; GXTexObj is a plain struct
// typedef in libogc (gxstruct.h via <gccore.h>), safe to reference by name
// once that header is visible.

// Returns the shim's GX_TexObj for a GL texture id (as bound via
// glGenTextures/glTexImage2D), or NULL when the id is unused/has no uploaded
// image yet. RendererGX loads this directly via GX_LoadTexObj rather than
// going through glBindTexture + the glDrawArrays shim's own lookup.
GXTexObj* Wii_GetTexObj(unsigned int glTexId);

// Returns the CPU-side bytes the shim retained for a glGenBuffers/
// glBufferData handle (vertex or index buffer), and writes the byte size to
// *outSize. Returns NULL when the id is unused/never uploaded. Backs
// Renderer::DrawMesh3D's vertex/index readback -- the shim keeps buffer
// bytes around specifically so this accessor can hand them back (added this
// pass; previously glBufferData's copy was accessible only to the
// glDrawArrays/glDrawElements shim internals).
const void* Wii_GetBufferData(unsigned int glBufId, unsigned int* outSize);

// Returns the current viewport (x, y, w, h) as tracked by glViewport, into
// vp[0..3]. Thin typed wrapper so RendererGX doesn't need to round-trip
// through glGetIntegerv(GL_VIEWPORT, ...).
void Wii_GetViewport(int vp[4]);

// Returns the GL texture id currently bound on unit 0 (the shim's
// g_BoundTexture, tracked by glBindTexture -- see gl_funcsWii.cpp). Backs
// RendererGX::DrawShaded2D/DrawMesh3D's `tex==0` case, which per Renderer.h's
// contract means "use whatever is already bound on unit 0" (RendererGL.cpp's
// DrawShaded2D skips the glBindTexture call entirely when tex==0, leaving the
// prior GL binding active) rather than "draw untextured".
unsigned int Wii_GetBoundTexture();

// Marks a GL texture id so its NEXT glTexImage2D upload retains the linear
// (untiled) RGBA8 CPU copy alongside the tiled GX buffer. By default the
// shim frees the linear copy right after tiling -- retaining a 2nd full-size
// copy per texture roughly doubles resident texture RAM, which starves
// MEM1 for the last-loaded batch (particle textures) and causes silent
// memalign failures -> untextured opaque-quad fallback. Only glTexSubImage2D
// consumers need the linear copy (currently just the TTF font atlas, see
// FontInterface.cpp's EnsurePageTexture) -- call this before the atlas
// texture's glTexImage2D, not for ordinary asset textures. Does not
// itself allocate; only sets a flag consulted by the next upload.
void Wii_KeepTextureLinear(unsigned int glTexId);

// Port specific: upload ALREADY-TILED GX texel data to a GL texture id,
// skipping the glTexImage2D ExpandToRGBA8+TileRGBA8 path entirely. Backs the
// "GXT1" container (tools/assets/stage-assets.py --wii pre-tiles every
// transcodable Tex1 game texture -- bit-depth-preserving GX format, the MEM1
// win -- plus the WebP-only UI widget art at staging time; encoder:
// tools/lib/gx_encoder.py; reader: TextureFileFormat::ReadGxtx). `gxFmt`
// must be GX_TF_RGB565 (4), GX_TF_RGB5A3 (5) or GX_TF_RGBA8 (6); anything
// else is rejected (texture left untextured). `tiled` must be in that
// format's exact GX hardware tile layout (4x4 tiles; RGBA8 = 64-byte tile,
// AR half then GB half; 16bpp = 32-byte tile of big-endian u16 texels -- see
// gx_encoder.py's module docstring); `tiledSize` should equal
// GX_GetTexBufferSize(w, h, gxFmt, GX_FALSE, 0) -- it is clamped to that
// defensively. Replaces any previous image on the id (frees old tiled/linear
// copies); the resulting texture is CLAMP/CLAMP + LINEAR/LINEAR and keeps no
// linear CPU copy (glTexSubImage2D on it is not supported).
void Wii_UploadTiledGX(unsigned int glTexId, const void* tiled,
                       unsigned int tiledSize, int w, int h,
                       unsigned int gxFmt);

// Port specific: glTexSubImage2D is declared void (shared GL-shape signature
// in gl_compat.h, same as host/web) so it cannot report failure through its
// return value. This accessor exposes the outcome of the MOST RECENT
// glTexSubImage2D call so a caller (FontInterface::BuildPendingTextures) can
// tell a real upload from a silent bail (invalid texture id, null pixels/
// texels, out-of-range sub-rect, wrong format/type) and avoid clearing its
// dirty flag on failure -- see gl_funcsWii.cpp's glTexSubImage2D for every
// bail site this reflects. Bug #47: BuildPendingTextures previously cleared
// m_Dirty unconditionally, so a bailed glyph upload was silently dropped
// forever (never retried) -- the glyph stayed transparent for the game's
// lifetime.
bool Wii_LastTexSubImageOk();

#endif // FRUIT_PLATFORM_WII

#endif // MORTAR_GL_FUNCS_WII_H
