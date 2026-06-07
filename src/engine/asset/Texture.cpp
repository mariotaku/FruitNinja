#include "asset/Texture.h"
#include "asset/TextureManager.h"
#include "asset/File.h"
#include "render/DisplayManager.h"
#include "debug/Logger.h"
#include <vector>

namespace Mortar {

#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
GLuint Texture::s_LastBoundTexId = 0;
#endif

Texture::Texture()
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    : m_TexId(0)
    , m_Width(0)
    , m_Height(0)
#endif
{
}

Texture::~Texture() {
    // Notify the TextureManager so its cache drops the entry pointing
    // at us BEFORE the GL handle is freed. Mirrors the binary's
    // WeakPtr cleanup path — without this the next Find() for our
    // hash would return a dangling pointer.
    TextureManager::GetInstance().OnTextureDestroyed(this);

#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    if (m_TexId != 0) {
        glDeleteTextures(1, &m_TexId);
        m_TexId = 0;
    }
#endif
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
            LOG_WARN("TEXTURE/Set", "m_TexId==0 for path='%s' (load failed mid-stream); skipping bind",
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
        // Many texture loads are optional (HUD fruit-icons absent from the
        // Bada slow-hardware texture pack, defunct "coming_soon" placeholder,
        // fruit_shadow.tex only shipped on fast-hardware packages, etc.).
        // Callers handle missing files via SmartPtr.IsValid() gates -- this
        // is not an ERROR-class condition. Keep the trace at INFO so it's
        // still grep-able without polluting the console. Parse failures
        // below stay at ERROR (real corruption / bad header).
        LOG_INFO("TEXTURE/Load", "no such file '%s' (caller handles via IsValid)", path);
        return Mortar::SmartPtr<Texture>();
    }

    if (!f.Load(nullptr, 0)) {
        LOG_ERROR("TEXTURE/Load", "failed to parse '%s'", path);
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
        case 0x10: { // RGBA4444 -> CPU-unpack to RGBA8888
            // GL_UNSIGNED_SHORT_4_4_4_4 nibble order differs across desktop GL drivers.
            // Binary stores LE uint16: bits 15..12=R, 11..8=G, 7..4=B, 3..0=A.
            // Unpack on CPU and upload as GL_UNSIGNED_BYTE to avoid driver variance.
            const size_t pixCount = (size_t)width * (size_t)height;
            std::vector<unsigned char> rgba(pixCount * 4);
            const unsigned short* src16 = reinterpret_cast<const unsigned short*>(raw);
            for (size_t i = 0; i < pixCount; ++i) {
                unsigned short p = src16[i];
                unsigned char r = (unsigned char)((p >> 12) & 0xF);
                unsigned char g = (unsigned char)((p >>  8) & 0xF);
                unsigned char b = (unsigned char)((p >>  4) & 0xF);
                unsigned char a = (unsigned char)((p >>  0) & 0xF);
                rgba[i*4 + 0] = (r << 4) | r;
                rgba[i*4 + 1] = (g << 4) | g;
                rgba[i*4 + 2] = (b << 4) | b;
                rgba[i*4 + 3] = (a << 4) | a;
            }
            tex->UploadNative(width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
            break;
        }
        case 0x11: // RGB565
            tex->UploadNative(width, height, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, raw);
            break;
        // case 0x0b..0x0e: PVRTC compressed (not supported on desktop GL)
        default:
            LOG_ERROR("TEXTURE/Load", "unsupported format 0x%02x in '%s'", format, path);
            delete tex;
            return Mortar::SmartPtr<Texture>();
    }

    return Mortar::SmartPtr<Texture>(tex);
}

} // namespace Mortar

namespace Mortar {
// TODO: 0x00189d80 -- create a Texture2DFromFile_Bada from a memory buffer
// (ptr,len) via operator new(0x20), wrap in SmartPtr<Texture> and return it.
void Texture::LoadFromMemory(void const*, int) {}
// TODO: 0x00188da4 -- bind only if not already cached: if the IsCached vtable
// slot returns 0, call Set().
void Texture::SetUnCached() {}
// TODO: 0x00188d9c -- uncached unbind: forwards to UnSet().
void Texture::UnSetUnCached() {}
}  // namespace Mortar
