#include "asset/Texture.h"
#include "asset/TextureManager.h"
#include "render/DisplayManager.h"
#include <cstdio>
#include <cstring>
#include <vector>

namespace Mortar {

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

void Texture::Set() {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_TexId);
}

void Texture::UnSet() {
    glBindTexture(GL_TEXTURE_2D, 0);
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
SmartPtr<Texture> Texture::Load(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        DisplayManager& dm = DisplayManager::GetInstance();
        if (dm.m_TextureOverloadPrefix[0] != '\0') {
            std::string altPath = std::string(dm.m_TextureOverloadPrefix) + path;
            f = fopen(altPath.c_str(), "rb");
        }
        if (!f) {
            fprintf(stderr, "Texture::Load: failed to open '%s'\n", path);
            return SmartPtr<Texture>();
        }
    }

    uint8_t header[12];
    if (fread(header, 1, 12, f) != 12) {
        fclose(f);
        return SmartPtr<Texture>();
    }

    uint8_t widthLog2  = header[0];
    uint8_t heightLog2 = header[1];
    uint8_t format     = header[2];
    int width  = 1 << widthLog2;
    int height = 1 << heightLog2;

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    long dataSize = fileSize - 12;
    fseek(f, 12, SEEK_SET);

    std::vector<uint8_t> raw(dataSize);
    if ((long)fread(raw.data(), 1, dataSize, f) != dataSize) {
        fclose(f);
        return SmartPtr<Texture>();
    }
    fclose(f);

    Texture* tex = new Texture();
    tex->m_Path = path;

    // Matches TexFmtToGL (0x00189f78) — verified from Ghidra decompilation
    switch (format) {
        case 0x00: // RGB888
            tex->UploadNative(width, height, GL_RGB, GL_UNSIGNED_BYTE, raw.data());
            break;
        case 0x01: // RGBA8888
            tex->UploadNative(width, height, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());
            break;
        case 0x0f: // RGBA5551
            tex->UploadNative(width, height, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, raw.data());
            break;
        case 0x10: // RGBA4444
            tex->UploadNative(width, height, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, raw.data());
            break;
        case 0x11: // RGB565
            tex->UploadNative(width, height, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, raw.data());
            break;
        // case 0x0b..0x0e: PVRTC compressed (not supported on desktop GL)
        default:
            fprintf(stderr, "Texture::Load: unsupported format 0x%02x in '%s'\n", format, path);
            delete tex;
            return SmartPtr<Texture>();
    }

    return SmartPtr<Texture>(tex);
}

} // namespace Mortar
