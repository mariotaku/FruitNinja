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

#endif // FRUIT_PLATFORM_WII

#endif // MORTAR_GL_FUNCS_WII_H
