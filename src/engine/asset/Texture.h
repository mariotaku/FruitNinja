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

    // Binary @ 0x00189d80 -- create a Texture2DFromFile_Bada from a memory
    // buffer (ptr,len) and wrap in SmartPtr<Texture>. The binary returns the
    // SmartPtr via the hidden sret pointer (the "this" arg is the return slot,
    // not a live instance), so this is effectively a static factory. Mirrors
    // Load() but parses an in-memory .tex blob instead of a file path.
    static Mortar::SmartPtr<Texture> LoadFromMemory(void const* buf, int len);

    // v1.6.1 addition: AlternativeTextureLoader path-rewrite toggle.
    // Binary: bool global @ data segment, default false.
    // When true, TextureManager::Load rewrites the texture path via
    // AlternativeTextureLoader::CreateLoader before opening the file.
    // Port: defaults to false (matching shipped binary default; Prefix/Postfix are empty
    // so the rewrite would be a no-op even if enabled).
    static bool UseAlternativeTextureLoader;

    // Binary @ 0x00188da4 -- cache-gated bind. The binary calls a virtual slot
    // (Texture2D::GetType, vtable +0xc) and only binds when it returns 0; for a
    // plain Texture2D GetType()==0 always, so Set() always runs. The port has
    // merged the concrete Texture2D into Texture (no GetType subtype virtual),
    // so the gate is always-true and this forwards to Set().
    void SetUnCached();
    // Binary @ 0x00188d9c -- uncached unbind: forwards to UnSet().
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

private:
    // Parse a raw .tex blob (header[0..2] = widthLog2, heightLog2, format;
    // pixel data at +12) and upload it into a fresh Texture. Returns null on
    // bad header / unsupported format. Shared by Load() (file path) and
    // LoadFromMemory() (memory buffer) -- mirrors the binary's
    // GPUafyTexture (0x001898d8) + TexFmtToGL (0x00189f78) path that both
    // Texture::Load and Texture2DFromFile_Bada::FromMemoryInit (0x001899dc)
    // funnel through.
    static Mortar::SmartPtr<Texture> ParseTexBuffer(const void* data,
                                                    long size,
                                                    const char* pathForLog);

    // v1.6.1 addition: parse a .tex3 container blob (4-byte magic + layered header +
    // N-layer table) and upload layer-0 into a fresh Texture. Returns null if the
    // magic does not match (caller should fall through to ParseTexBuffer for .tex files).
    // Binary: Mortar::TextureFileFormat::Tex3Format::Read @ 0x0022bd7c reads the full
    // multi-layer table; port uploads layer-0 only (mip layers // TODO: 0x0022bd7c).
    static Mortar::SmartPtr<Texture> ParseTex3Buffer(const void* data,
                                                     long size,
                                                     const char* pathForLog);
};

} // namespace Mortar

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
#include <cstddef>
static_assert(sizeof(Mortar::Texture) == 12, "Mortar::Texture size mismatch");
#endif

#endif
