#include "asset/Texture.h"
#include "asset/TextureManager.h"
#include "asset/File.h"
#include "render/DisplayManager.h"
#include <cstdio>

namespace Mortar {

GLuint Texture::s_LastBoundTexId = 0;

Texture::Texture()
    : m_TexId(0)
    , m_Width(0)
    , m_Height(0)
{
}

Texture::~Texture() {
    // Notify the TextureManager so its cache drops the entry pointing
    // at us BEFORE the GL handle is freed. Mirrors the binary's
    // WeakPtr cleanup path — without this the next Find() for our
    // hash would return a dangling pointer.
    TextureManager::GetInstance().OnTextureDestroyed(this);

    if (m_TexId != 0) {
        glDeleteTextures(1, &m_TexId);
        m_TexId = 0;
    }
}

// Matches Bada::Texture2DFromFile_Bada::Set (0x001897c0).
// Binary gates the enable+bind on a cache flag that toggles when the
// same texture is re-set; the port always enables and binds since we
// don't cache "last bound" state. glActiveTexture is a port addition —
// binary relies on TU0 being preselected by the frame setup.
void Texture::Set() {
    if (m_TexId == 0) {
        // Texture object exists (SmartPtr is valid) but GL upload didn't
        // happen (load failed mid-stream, or upload was skipped). Binding 0
        // makes GL sample the default-white texture -- producing stray
        // white quads. Warn once per Texture instance and skip the bind.
        static bool s_warned = false;
        if (!s_warned) {
            fprintf(stderr,
                "[Mortar::Texture::Set] WARN: m_TexId==0 for path='%s' -- "
                "load failed mid-stream or upload skipped. Skipping bind.\n",
                m_Path.c_str());
            s_warned = true;
        }
        s_LastBoundTexId = 0;  // mark untextured so DrawQuad skips
        return;
    }
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, m_TexId);
    s_LastBoundTexId = m_TexId;
}

// Matches Bada::Texture2DFromFile_Bada::UnSet (0x00189790).
void Texture::UnSet() {
    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    s_LastBoundTexId = 0;
}

void Texture::UploadRGBA(int width, int height, const void* pixels) {
    if (m_TexId != 0) {
        glDeleteTextures(1, &m_TexId);
    }

    m_Width = width;
    m_Height = height;

    DisplayManager& dm = DisplayManager::GetInstance();

    glGenTextures(1, &m_TexId);
    glBindTexture(GL_TEXTURE_2D, m_TexId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, dm.GetPlatformMagFilter());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, dm.GetPlatformMinFilter());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, dm.GetPlatformWrapS());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, dm.GetPlatformWrapT());
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}

void Texture::UploadNative(int width, int height, GLenum glFormat, GLenum glType,
                           const void* pixels) {
    if (m_TexId != 0) {
        glDeleteTextures(1, &m_TexId);
    }

    m_Width = width;
    m_Height = height;

    DisplayManager& dm = DisplayManager::GetInstance();

    glGenTextures(1, &m_TexId);
    glBindTexture(GL_TEXTURE_2D, m_TexId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, dm.GetPlatformMagFilter());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, dm.GetPlatformMinFilter());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, dm.GetPlatformWrapS());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, dm.GetPlatformWrapT());
    glTexImage2D(GL_TEXTURE_2D, 0, glFormat, width, height, 0,
                 glFormat, glType, pixels);
}

// Matches GPUafyTexture (0x001898d8) + Texture::Load (0x00189dd4)
Mortar::SmartPtr<Texture> Texture::Load(const char* path) {
    Mortar::File f(path, 0, 0);
    bool opened = f.Open();
    if (!opened) {
        DisplayManager& dm = DisplayManager::GetInstance();
        if (dm.m_TextureOverloadPrefix[0] != '\0') {
            std::string altPath = std::string(dm.m_TextureOverloadPrefix) + path;
            Mortar::File fAlt(altPath.c_str(), 0, 0);
            if (fAlt.Open()) {
                return Texture::Load(altPath.c_str());
            }
        }
        fprintf(stderr, "Texture::Load: failed to open '%s'\n", path);
        return Mortar::SmartPtr<Texture>();
    }

    if (!f.Load(nullptr, 0)) {
        fprintf(stderr, "Texture::Load: failed to load '%s'\n", path);
        return Mortar::SmartPtr<Texture>();
    }

    unsigned long fileSize = f.Size();
    if (fileSize < 12) {
        return Mortar::SmartPtr<Texture>();
    }

    const uint8_t* data = static_cast<const uint8_t*>(f.Data());
    uint8_t widthLog2  = data[0];
    uint8_t heightLog2 = data[1];
    uint8_t format     = data[2];
    int width  = 1 << widthLog2;
    int height = 1 << heightLog2;

    long dataSize = (long)fileSize - 12;
    const uint8_t* raw = data + 12;
    if (dataSize <= 0) {
        return Mortar::SmartPtr<Texture>();
    }

    Texture* tex = new Texture();
    tex->m_Path = path;

    // Matches TexFmtToGL (0x00189f78) — verified from Ghidra decompilation
    switch (format) {
        case 0x00: // RGB888
            tex->UploadNative(width, height, GL_RGB, GL_UNSIGNED_BYTE, raw);
            break;
        case 0x01: // RGBA8888
            tex->UploadNative(width, height, GL_RGBA, GL_UNSIGNED_BYTE, raw);
            break;
        case 0x0f: // RGBA5551
            tex->UploadNative(width, height, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, raw);
            break;
        case 0x10: // RGBA4444
            tex->UploadNative(width, height, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, raw);
            break;
        case 0x11: // RGB565
            tex->UploadNative(width, height, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, raw);
            break;
        // case 0x0b..0x0e: PVRTC compressed (not supported on desktop GL)
        default:
            fprintf(stderr, "Texture::Load: unsupported format 0x%02x in '%s'\n", format, path);
            delete tex;
            return Mortar::SmartPtr<Texture>();
    }

    return Mortar::SmartPtr<Texture>(tex);
}

} // namespace Mortar

// ---- AUTO-STUB MERGE: STUB -- gen_stubs.py ----
namespace Mortar {
// STUB: Texture::LoadFromMemory -- auto stub
void Texture::LoadFromMemory(void const*, int) {}
// STUB: Texture::SetUnCached -- auto stub
void Texture::SetUnCached() {}
// STUB: Texture::UnSetUnCached -- auto stub
void Texture::UnSetUnCached() {}
}  // namespace Mortar
// ---- end AUTO-STUB MERGE ----
