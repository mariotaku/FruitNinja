#ifndef MORTAR_TEXTURE_H
#define MORTAR_TEXTURE_H

#include "util/ReferenceCounter.h"
#include "util/SmartPtr.h"
#include "render/gl_funcs.h"
#include <string>

namespace Mortar {

// Matches original Texture2DFromFile_Bada (32 bytes)
// Ref-counted texture with GL handle
class Texture : public Mortar::ReferenceCounter {
public:
    GLuint m_TexId;     // GL texture handle (-1 = unloaded)
    int m_Width;        // +0x10 from .tex header
    int m_Height;       // +0x14 from .tex header
    std::string m_Path; // file path for debugging/reload

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

    // Tracker for the last-bound texture id (port-side; binary doesn't track
    // this). Renderer::DrawQuad reads it to detect untextured draws that
    // would otherwise sample default-white and produce stray white quads.
    static GLuint s_LastBoundTexId;

public:

public:

public:

public:

public:

public:
    // ---- AUTO-STUB MERGE: STUB -- gen_stubs.py ----
    // STUB: Texture::LoadFromMemory -- auto stub from binary missing-symbol set
    void LoadFromMemory(void const*, int);
    // STUB: Texture::SetUnCached -- auto stub from binary missing-symbol set
    void SetUnCached();
    // STUB: Texture::UnSetUnCached -- auto stub from binary missing-symbol set
    void UnSetUnCached();
    // ---- end AUTO-STUB MERGE ----
};

} // namespace Mortar

#endif
