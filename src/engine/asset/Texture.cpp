#include "asset/Texture.h"
#include "asset/TextureManager.h"
#include "asset/AlternativeTextureLoader.h"
#include "asset/TextureFileFormat.h"
#include "asset/File.h"
#include "render/DisplayManager.h"
#include "debug/Logger.h"
#include <cstring>
#include <vector>

namespace Mortar {

#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
GLuint Texture::s_LastBoundTexId = 0;
#endif

// v1.6.1 addition: AlternativeTextureLoader path-rewrite toggle.
// Binary: bool global @ data segment; default false (Prefix/Postfix are empty in shipped data).
bool Texture::UseAlternativeTextureLoader = false;

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
    // WeakPtr cleanup path -- without this the next Find() for our
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
void Texture::Set() {
    if (m_TexId == 0) {
        static bool s_warned = false;
        if (!s_warned) {
            LOG_WARN("TEXTURE/Set", "m_TexId==0 for path='%s' (load failed mid-stream); skipping bind",
                m_Path.c_str());
            s_warned = true;
        }
        s_LastBoundTexId = 0;
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

// Upload a parsed Tex1Data to GL. Called by Load() and LoadFromMemory().
// This is the "// Port specific: GL boundary" sink fed by the reader's parsed DataInfo + blob.
// Matches GPUafyTexture (0x001898d8) + TexFmtToGL (0x00189f78).
static Mortar::SmartPtr<Texture> UploadTex1ToGL(
        Texture* tex,
        const TextureFileFormat::Tex1Data* d,
        const char* pathForLog)
{
    int width  = d->info.width;
    int height = d->info.height;
    const uint8_t* raw = static_cast<const uint8_t*>(d->pixels);

    switch (d->texFmt) {
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
            LOG_ERROR("TEXTURE/Load", "unsupported format 0x%02x in '%s'", (unsigned)d->texFmt,
                      pathForLog ? pathForLog : "<memory>");
            return Mortar::SmartPtr<Texture>();
    }
    return Mortar::SmartPtr<Texture>(tex);
}

// Matches GPUafyTexture (0x001898d8) + Texture::Load (0x00189dd4).
// Reconciled to use TextureFileFormat::g_readers[] dispatch via TextureLoader/LockLayers.
// Binary flow: AlternativeTextureLoader::CreateLoader -> TextureLoader -> LockLayers ->
//   g_readers[i](data, size) -> Tex1Data* -> GL upload.
Mortar::SmartPtr<Texture> Texture::Load(const char* path) {
    // v1.6.1: AlternativeTextureLoader::CreateLoader handles both the fast path
    // (Prefix/Postfix empty -> returns TextureLoader over original path) and the
    // slow path (Prefix/Postfix set -> SubstituteApparentSizeTextureSource).
    // CreateLoader gates on File::Exists internally (via TextureLoader::CreateLoader).
    AsciiString pathStr(path);
    SmartPtr<TextureSource> src = AlternativeTextureLoader::CreateLoader(pathStr);

    if (!src.IsValid()) {
        // File not found (TextureLoader::CreateLoader returned null).
        // Port-specific fallback: try TextureOverloadPrefix path.
        // DIFFERS: binary has no TextureOverloadPrefix; this is a port-specific feature
        //   for the SDL asset-root override. Mirrors the old Texture::Load behaviour.
        DisplayManager& dm = DisplayManager::GetInstance();
        if (dm.m_TextureOverloadPrefix[0] != '\0') {
            std::string altPath = std::string(dm.m_TextureOverloadPrefix) + path;
            return Texture::Load(altPath.c_str());
        }
        LOG_INFO("TEXTURE/Load", "no such file '%s' (caller handles via IsValid)", path);
        return Mortar::SmartPtr<Texture>();
    }

    // Lock the source to get parsed pixel data.
    TextureSourceData* raw = src->LockLayers();
    if (!raw) {
        LOG_ERROR("TEXTURE/Load", "failed to parse '%s'", path);
        return Mortar::SmartPtr<Texture>();
    }

    // Identify and upload. Currently the only live format is Tex1.
    // The binary was built -fno-rtti so dynamic_cast is unavailable; static_cast
    // is safe here because LockLayers() iterates g_readers[] and returns the first
    // non-null result -- in the shipped 1.5.1/1.6.1 packs the only reader that
    // accepts actual data is ReadTex1Format. Tex2/DDS/Tex3 return null for all
    // shipped assets (their full decode is TODO), so raw is always Tex1Data* in
    // practice. When those decoders land, replace this with a type-tag field.
    // TODO: 0x00189dd4 -- binary type-dispatch mechanism for multi-format raw*.
    TextureFileFormat::Tex1Data* tex1 =
        static_cast<TextureFileFormat::Tex1Data*>(raw);

    Mortar::SmartPtr<Texture> result;
    if (tex1) {
        Texture* t = new Texture();
        result = UploadTex1ToGL(t, tex1, path);
        if (!result.IsValid()) {
            delete t;
        } else {
            t->m_Path = path;
        }
    } else {
        // TODO: 0x0022bc6c -- Tex3 full decode + GL upload.
        // TODO: 0x0022c7d4 -- DDS full decode + GL upload.
        // TODO: 0x0022b404 -- Tex2 full decode + GL upload.
        LOG_INFO("TEXTURE/Load", "tex3/dds/tex2 format not yet decoded for '%s'"
                 " (TODO: 0x0022bc6c / 0x0022c7d4 / 0x0022b404)", path);
    }

    src->UnlockLayers(raw);
    return result;
}

// Binary @ 0x00189d80 Mortar::Texture::LoadFromMemory(void const*, int).
// Port: routes the in-memory blob directly through the Tex1 reader (no file I/O).
// DIFFERS: binary constructs Texture2DFromFile_Bada and parses via GPUafyTexture;
//   port calls ReadTex1Format directly (same logic, no TextureLoader wrapping needed
//   for memory blobs -- LoadFromMemory is never called with Tex3/Tex2/DDS data).
Mortar::SmartPtr<Texture> Texture::LoadFromMemory(void const* buf, int len) {
    return ParseTexBuffer(buf, (long)len, nullptr);
}

// ParseTexBuffer -- kept as the shared helper for LoadFromMemory and the test path.
// Calls the Tex1 reader directly (bypasses the registry for in-memory blobs).
// Matches GPUafyTexture (0x001898d8) + TexFmtToGL (0x00189f78).
Mortar::SmartPtr<Texture> Texture::ParseTexBuffer(const void* data, long size,
                                                  const char* pathForLog) {
    TextureSourceData* raw = TextureFileFormat::ReadTex1Format(data, (unsigned long)size);
    if (!raw) {
        return Mortar::SmartPtr<Texture>();
    }
    TextureFileFormat::Tex1Data* d = static_cast<TextureFileFormat::Tex1Data*>(raw);
    Texture* tex = new Texture();
    Mortar::SmartPtr<Texture> result = UploadTex1ToGL(tex, d, pathForLog);
    if (!result.IsValid()) {
        delete tex;
    }
    delete raw;
    return result;
}

// ParseTex3Buffer -- magic-check only; full decode is TODO.
// Kept for call-site compatibility (previously called by Load()).
// Now Load() routes through AlternativeTextureLoader -> TextureLoader -> g_readers,
// so this function is effectively dead but preserved as a named binary landmark.
// Binary: Mortar::TextureFileFormat::Tex3Format::Read @ 0x0022bd7c.
Mortar::SmartPtr<Texture> Texture::ParseTex3Buffer(const void* data, long size,
                                                    const char* pathForLog)
{
    TextureSourceData* raw = TextureFileFormat::ReadTex3Format(data, (unsigned long)size);
    if (!raw) {
        return Mortar::SmartPtr<Texture>();
    }
    // TODO: 0x0022bc6c -- Tex3Data GL upload path (no Tex3 assets in shipped packs).
    delete raw;
    (void)pathForLog;
    return Mortar::SmartPtr<Texture>();
}

} // namespace Mortar

namespace Mortar {

// Binary @ 0x00188da4 Mortar::Texture::SetUnCached().
void Texture::SetUnCached() {
    Set();
}

// Binary @ 0x00188d9c Mortar::Texture::UnSetUnCached() -- forwards to UnSet().
void Texture::UnSetUnCached() {
    UnSet();
}

}  // namespace Mortar
