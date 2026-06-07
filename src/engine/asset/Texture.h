#ifndef MORTAR_TEXTURE_H
#define MORTAR_TEXTURE_H

#include "util/ReferenceCounter.h"
#include "util/SmartPtr.h"
#include "render/gl_funcs.h"
#include <string>

namespace Mortar {

// Mortar::Texture — abstract base class (binary size = 12 bytes, zero own members).
// Binary ctor @ 0x0018a1c4: only calls ReferenceCounter ctor then sets vptr.
// All GL handle / width / height / format state lives in concrete subclasses
// (e.g. Texture2D, Texture2DFromFile_Bada) — NOT in this base.
// Vtable @ 0x001eadd8 (slots from +0x08): [0]=0x189f4c [1]=0x18a10c [2]=0x12e564 ...
//
// Binary layout:
//   +0x00  ReferenceCounter (vptr @ +0x00, refcount @ +0x04, weak @ +0x08) — 12 bytes
//   +0x0C  end (sizeof = 12)
class Texture : public Mortar::ReferenceCounter {
public:
    Texture();
    virtual ~Texture();

    // Bind this texture to GL_TEXTURE_2D on unit GL_TEXTURE0
    void Set();

    // Unbind (bind texture 0)
    void UnSet();

    // Load a .tex file from disk, create GL texture
    // Returns SmartPtr (null on failure)
    static Mortar::SmartPtr<Texture> Load(const char* path);

    // Upload raw RGBA8888 pixels (for converted textures)
    void UploadRGBA(int width, int height, const void* pixels);

    // Upload native format directly to GL (for .tex files without CPU conversion)
    void UploadNative(int width, int height, GLenum glFormat, GLenum glType, const void* pixels);

    // TODO: 0x00189d80 -- create a Texture2DFromFile_Bada from a memory buffer
    // (ptr,len), wrap in SmartPtr<Texture> and return it (out via 'this').
    void LoadFromMemory(void const*, int);
    // TODO: 0x00188da4 -- bind only if not already cached: if vtable IsCached
    // slot returns 0, call Set().
    void SetUnCached();
    // TODO: 0x00188d9c -- uncached unbind: forwards to UnSet().
    void UnSetUnCached();

#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    // Port specific: GL texture handle and dimensions — not in binary Mortar::Texture
    // base; binary stores these in the concrete subclass (Texture2DFromFile_Bada).
    // Placed at tail so binary field offsets (none) are unaffected.
    GLuint      m_TexId;   // GL texture handle (0 = unloaded)
    int         m_Width;   // texture width in pixels
    int         m_Height;  // texture height in pixels
    std::string m_Path;    // file path (for debugging/reload)

    // Tracker for the last-bound texture id (port-side; binary doesn't track
    // this). Renderer::DrawQuad reads it to detect untextured draws.
    static GLuint s_LastBoundTexId;
#endif
};

} // namespace Mortar

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
#include <cstddef>
static_assert(sizeof(Mortar::Texture) == 12, "Mortar::Texture size mismatch");
#endif

#endif
