#include "asset/Texture.h"
#include "asset/TextureManager.h"
#include "asset/AlternativeTextureLoader.h"
#include "asset/TextureFileFormat.h"
#include "asset/File.h"
#include "render/DisplayManager.h"
#include "render/Renderer.h"
#include "debug/Logger.h"
#include "util/Endian.h"
#include <cstring>
#include <vector>

#if defined(FRUIT_PLATFORM_WII)
// Port specific: pre-tiled "GXT1" textures (all transcodable Tex1 game art +
// widgets) upload via Wii_UploadTiledGX (see the kGxtxTexFmt cases below).
// <gccore.h> must precede gl_funcsWii.h (GXTexObj typedef).
#include <gccore.h>
#include "render/gl_funcsWii.h"
#endif

#if defined(FN_BIG_ENDIAN)
namespace {
// Port specific: Tex1 16-bit-per-texel formats (RGBA5551/RGBA4444/RGB565) are
// little-endian 2-byte values on disk (matching the binary's ARM-LE format).
// GL/GX both consume them as native uint16_t texels, so on FN_BIG_ENDIAN
// targets (Wii) the whole pixel buffer must be byteswapped once before upload.
// RGB888/RGBA8888 are plain byte streams and need no swap.
void SwapPixels16(const void* src, void* dstBuf, size_t pixelCount) {
    const uint16_t* s = static_cast<const uint16_t*>(src);
    uint16_t* d = static_cast<uint16_t*>(dstBuf);
    for (size_t i = 0; i < pixelCount; ++i) {
        d[i] = Endian::fnByteSwap16(s[i]);
    }
}
}
#endif

namespace Mortar {

Mortar::Texture* Texture::s_CurrentlySetTexture = 0;

GLuint Texture::s_LastBoundTexId = 0;

// v1.6.1 addition: AlternativeTextureLoader path-rewrite toggle.
// Binary: bool global @ data segment; default false (Prefix/Postfix are empty in shipped data).
bool Texture::UseAlternativeTextureLoader = false;

// Binary base ctor @0x00268ea4.
Texture::Texture()
    : m_HasAlpha(true)
    , m_DeviceResVptr(0)
    , m_RegistryNext(0)
{
    // Defunct: DeviceResource registry -- no-op stub; v1.6.1 Texture::Texture @0x00268ea4
}

Texture::~Texture() {
    // Notify the TextureManager so its cache drops the entry pointing at us BEFORE
    // the GL handle is freed. Mirrors the binary's WeakPtr cleanup path.
    TextureManager::GetInstance().OnTextureDestroyed(this);
    // Defunct: DeviceResource unregister -- no-op stub; v1.6.1 Texture::~Texture @0x00268e1c
}

// Vtable slot 3 -- base default: no GL state (subclass Texture2D_Bada overrides).
void Texture::Set() {
}

// Vtable slot 4 -- base default: no GL state (subclass Texture2D_Bada overrides).
void Texture::UnSet(bool /*flag*/) {
}

// Binary @0x00188da4 -- cache-gated bind: forwards to Set().
void Texture::SetUnCached() {
    Set();
}

// Binary @0x00188d9c -- uncached unbind: forwards to UnSet().
void Texture::UnSetUnCached() {
    UnSet();
}

// Binary ctor @0x00268d44.
Texture2D::Texture2D()
    : m_MeshVptr(0)
{
    // DIFFERS: original sets m_MeshVptr to UVMesh secondary vtable pointer;
    //   port = null placeholder (UVMesh interface unused in live render path).
}

Texture2D::~Texture2D() {
}

} // namespace Mortar

// ---------------------------------------------------------------------------
// Mortar::Bada::Texture2D_Bada
// ---------------------------------------------------------------------------
namespace Mortar {
namespace Bada {

// Port specific: no binary counterpart (used by the UploadTex1ToGL DIFFERS load path).
Texture2D_Bada::Texture2D_Bada()
    : m_PrimType(0)
    , m_TexId(0)
    , m_Pad5c(0)
    , m_Source()
{
}

// Binary ctor @0x0022a7d8.
Texture2D_Bada::Texture2D_Bada(const Mortar::SmartPtr<TextureSource>& src, unsigned long param2)
    : m_PrimType(0)
    , m_TexId(0)
    , m_Pad5c(0)
    , m_Source()
{
    SetSource(src, param2);
}

// Binary dtor @0x00229b8c (in-place).
Texture2D_Bada::~Texture2D_Bada() {
    ReleaseCache();
}

// Vtable slot 3 @0x00229710 -- bind this GL texture.
void Texture2D_Bada::Set() {
    // Only the GL/Renderer bind is platform-gated. The m_TexId==0 early-out and
    // both global stores are the binary's own control flow and must compile in
    // every build -- Renderer::DrawQuad reads s_CurrentlySetTexture unguarded
    // (the same predicate Mesh::DrawQuadUnCached @0x00240a70 evaluates).
    if (m_TexId == 0) {
        static bool s_warned = false;
        if (!s_warned) {
            LOG_WARN("TEXTURE/Set", "m_TexId==0 (load failed or not yet uploaded); skipping bind");
            s_warned = true;
        }
        Texture::s_LastBoundTexId = 0;
        Texture::s_CurrentlySetTexture = 0;
        return;
    }
#if !defined(__bada__)
    // Port specific: sampling bind goes through the Renderer's lazy shadow
    // (real glBindTexture happens at the next draw). Bookkeeping unchanged.
    if (Renderer* r = Renderer::GetInstance()) {
        r->BindTexture2D((uint32_t)m_TexId);
    } else {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_TexId);
    }
#endif
    Texture::s_LastBoundTexId = m_TexId;
    Texture::s_CurrentlySetTexture = this;
}

// Vtable slot 4 @0x002296ac -- unbind.
void Texture2D_Bada::UnSet(bool /*flag*/) {
#if !defined(__bada__)
    if (Renderer* r = Renderer::GetInstance()) {
        r->BindTexture2D(0);
    } else {
        glBindTexture(GL_TEXTURE_2D, 0);
    }
#endif
    Texture::s_LastBoundTexId = 0;
    Texture::s_CurrentlySetTexture = 0;
}

// Vtable slot 5 @0x0022964c -- return hash from source.
unsigned int Texture2D_Bada::GetHash() const {
    if (m_Source.IsValid()) {
        return m_Source->GetHash();
    }
    return 0;
}

// Vtable slot 6 @0x0022a8d0 -- debug string.
const char* Texture2D_Bada::Debug_ToString() {
    return "Texture2D_Bada";
}

// Vtable slot 7 @0x0022a8b4 -- UVMesh ID (stub: 0).
unsigned int Texture2D_Bada::GetUVMeshID() const {
    return 0;
}

// ReleaseCache: delete GL texture and reset m_TexId.
void Texture2D_Bada::ReleaseCache() {
#if !defined(__bada__)
    if (m_TexId != 0) {
        glDeleteTextures(1, &m_TexId);
        // Port specific: keep the Renderer's texture shadow off the dead name.
        if (Renderer* r = Renderer::GetInstance()) {
            r->NotifyTextureDeleted((uint32_t)m_TexId);
        }
        m_TexId = 0;
    }
#endif
}

// Cache @0x0022a4f4: fill DataInfo dims, glGenTextures, upload pixels.
// Port specific: implements the binary Cache() body which calls FindBestFormat,
// fills DataInfo, then glGenTextures + glTexImage2D. The binary calls LockLayers
// on m_Source to get the parsed data; port does the same.
void Texture2D_Bada::Cache() {
    if (!m_Source.IsValid()) {
        return;
    }

    TextureSourceData* raw = m_Source->LockLayers();
    if (!raw) {
        return;
    }

    TextureFileFormat::Tex1Data* tex1 =
        static_cast<TextureFileFormat::Tex1Data*>(raw);

    if (tex1) {
        int width  = (int)tex1->info.apparentWidth;
        int height = (int)tex1->info.apparentHeight;
        const uint8_t* pixels = static_cast<const uint8_t*>(tex1->pixels);

        // Fill DataInfo in the base Texture with apparent dimensions.
        m_DataInfo.apparentWidth  = (uint32_t)width;
        m_DataInfo.apparentHeight = (uint32_t)height;
        m_DataInfo.rawWidth       = tex1->info.rawWidth;
        m_DataInfo.rawHeight      = tex1->info.rawHeight;

#if !defined(__bada__)
        DisplayManager& dm = DisplayManager::GetInstance();

        if (m_TexId == 0) {
            glGenTextures(1, &m_TexId);
        }
        // Port specific: upload bind -- immediate, with Renderer shadow sync.
        if (Renderer* r = Renderer::GetInstance()) {
            r->BindTextureForUpload((uint32_t)m_TexId);
        } else {
            glBindTexture(GL_TEXTURE_2D, m_TexId);
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, dm.GetPlatformMagFilter());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, dm.GetPlatformMinFilter());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, dm.GetPlatformWrapS());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, dm.GetPlatformWrapT());

        switch (tex1->texFmt) {
            case 0x00: // RGB888
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0,
                             GL_RGB, GL_UNSIGNED_BYTE, pixels);
                break;
            case 0x01: // RGBA8888
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                             GL_RGBA, GL_UNSIGNED_BYTE, pixels);
                break;
            case 0x0f: { // RGBA5551
#if defined(FN_BIG_ENDIAN)
                std::vector<unsigned char> swapped((size_t)width * (size_t)height * 2);
                SwapPixels16(pixels, swapped.data(), (size_t)width * (size_t)height);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                             GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, swapped.data());
#else
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                             GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, pixels);
#endif
                break;
            }
            case 0x10: { // RGBA4444 -> CPU-unpack to RGBA8888
                const size_t pixCount = (size_t)width * (size_t)height;
                std::vector<unsigned char> rgba(pixCount * 4);
                const unsigned short* src16 = reinterpret_cast<const unsigned short*>(pixels);
                size_t i;
                for (i = 0; i < pixCount; ++i) {
                    unsigned short p = src16[i];
#if defined(FN_BIG_ENDIAN)
                    // On-disk value is little-endian; the native load above
                    // assembled it byte-swapped, so swap back before unpacking.
                    p = Endian::fnByteSwap16(p);
#endif
                    unsigned char r = (unsigned char)((p >> 12) & 0xF);
                    unsigned char g = (unsigned char)((p >>  8) & 0xF);
                    unsigned char b = (unsigned char)((p >>  4) & 0xF);
                    unsigned char a = (unsigned char)((p >>  0) & 0xF);
                    rgba[i*4 + 0] = (r << 4) | r;
                    rgba[i*4 + 1] = (g << 4) | g;
                    rgba[i*4 + 2] = (b << 4) | b;
                    rgba[i*4 + 3] = (a << 4) | a;
                }
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                             GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
                break;
            }
            case 0x11: { // RGB565
#if defined(FN_BIG_ENDIAN)
                std::vector<unsigned char> swapped((size_t)width * (size_t)height * 2);
                SwapPixels16(pixels, swapped.data(), (size_t)width * (size_t)height);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0,
                             GL_RGB, GL_UNSIGNED_SHORT_5_6_5, swapped.data());
#else
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0,
                             GL_RGB, GL_UNSIGNED_SHORT_5_6_5, pixels);
#endif
                break;
            }
#if defined(FRUIT_PLATFORM_WII)
            case TextureFileFormat::kGxtxTexFmt:
                // Port specific: "GXT1" pre-tiled native GX textures; actual
                // GX format travels in gxNativeFmt (see the identical case in
                // UploadTex1ToGL below).
                Wii_UploadTiledGX(m_TexId, pixels,
                                  (unsigned int)tex1->pixelsSize, width, height,
                                  tex1->gxNativeFmt);
                break;
#endif
            default:
                LOG_ERROR("TEXTURE/Cache", "unsupported format 0x%02x", (unsigned)tex1->texFmt);
                break;
        }
#endif
    }

    m_Source->UnlockLayers(raw);
}

// SetSource @0x0022a7a8.
void Texture2D_Bada::SetSource(const Mortar::SmartPtr<TextureSource>& src, unsigned long /*param2*/) {
    ReleaseCache();
    m_Source = src;
    Cache();
}

} // namespace Bada
} // namespace Mortar

// ---------------------------------------------------------------------------
// Texture::Load factory + helpers (binary @0x0022a854 Texture2D::Load)
// ---------------------------------------------------------------------------
namespace Mortar {

// Upload a parsed Tex1Data to GL using a Texture2D_Bada. Called by Load() and LoadFromMemory().
// Port specific: GL boundary. Matches GPUafyTexture (0x001898d8) + TexFmtToGL (0x00189f78).
static Mortar::SmartPtr<Texture> UploadTex1ToGL(
        Bada::Texture2D_Bada* tex,
        const TextureFileFormat::Tex1Data* d,
        const char* pathForLog)
{
    int width  = (int)d->info.apparentWidth;
    int height = (int)d->info.apparentHeight;
    const uint8_t* raw = static_cast<const uint8_t*>(d->pixels);

    tex->m_DataInfo.apparentWidth  = (uint32_t)width;
    tex->m_DataInfo.apparentHeight = (uint32_t)height;
    tex->m_DataInfo.rawWidth       = d->info.rawWidth;
    tex->m_DataInfo.rawHeight      = d->info.rawHeight;

#if !defined(__bada__)
    DisplayManager& dm = DisplayManager::GetInstance();

    if (tex->m_TexId == 0) {
        glGenTextures(1, &tex->m_TexId);
    }
    // Port specific: upload bind -- immediate, with Renderer shadow sync.
    if (Renderer* r = Renderer::GetInstance()) {
        r->BindTextureForUpload((uint32_t)tex->m_TexId);
    } else {
        glBindTexture(GL_TEXTURE_2D, tex->m_TexId);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, dm.GetPlatformMagFilter());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, dm.GetPlatformMinFilter());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, dm.GetPlatformWrapS());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, dm.GetPlatformWrapT());

    switch (d->texFmt) {
        case 0x00: // RGB888
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0,
                         GL_RGB, GL_UNSIGNED_BYTE, raw);
            break;
        case 0x01: // RGBA8888
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, raw);
            break;
        case 0x0f: { // RGBA5551
#if defined(FN_BIG_ENDIAN)
            std::vector<unsigned char> swapped((size_t)width * (size_t)height * 2);
            SwapPixels16(raw, swapped.data(), (size_t)width * (size_t)height);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                         GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, swapped.data());
#else
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                         GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, raw);
#endif
            break;
        }
        case 0x10: { // RGBA4444 -> CPU-unpack to RGBA8888
            const size_t pixCount = (size_t)width * (size_t)height;
            std::vector<unsigned char> rgba(pixCount * 4);
            const unsigned short* src16 = reinterpret_cast<const unsigned short*>(raw);
            size_t i;
            for (i = 0; i < pixCount; ++i) {
                unsigned short p = src16[i];
#if defined(FN_BIG_ENDIAN)
                // On-disk value is little-endian; the native load above
                // assembled it byte-swapped, so swap back before unpacking.
                p = Endian::fnByteSwap16(p);
#endif
                unsigned char r = (unsigned char)((p >> 12) & 0xF);
                unsigned char g = (unsigned char)((p >>  8) & 0xF);
                unsigned char b = (unsigned char)((p >>  4) & 0xF);
                unsigned char a = (unsigned char)((p >>  0) & 0xF);
                rgba[i*4 + 0] = (r << 4) | r;
                rgba[i*4 + 1] = (g << 4) | g;
                rgba[i*4 + 2] = (b << 4) | b;
                rgba[i*4 + 3] = (a << 4) | a;
            }
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
            break;
        }
        case 0x11: { // RGB565
#if defined(FN_BIG_ENDIAN)
            std::vector<unsigned char> swapped((size_t)width * (size_t)height * 2);
            SwapPixels16(raw, swapped.data(), (size_t)width * (size_t)height);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0,
                         GL_RGB, GL_UNSIGNED_SHORT_5_6_5, swapped.data());
#else
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0,
                         GL_RGB, GL_UNSIGNED_SHORT_5_6_5, raw);
#endif
            break;
        }
#if defined(FRUIT_PLATFORM_WII)
        case TextureFileFormat::kGxtxTexFmt:
            // Port specific: "GXT1" pre-tiled native GX textures
            // (stage-assets.py --wii + tools/lib/gx_encoder.py; reader
            // TextureFileFormat::ReadGxtx). All transcodable Tex1 game
            // textures plus the WebP-only UI widget art are decoded + tiled
            // at staging time, with the GX format (d->gxNativeFmt: RGB565=4
            // / RGB5A3=5 / RGBA8=6) preserving the source bit-depth; the
            // file bytes are already in GX tiled layout and upload directly
            // -- no glTexImage2D, no runtime tiling. width/height are the
            // true apparent dims (no isHd halving applies:
            // FN_ENABLE_HD_ASSETS is OFF on Wii and hd_* widget art is not
            // staged).
            Wii_UploadTiledGX(tex->m_TexId, raw,
                              (unsigned int)d->pixelsSize, width, height,
                              d->gxNativeFmt);
            break;
#endif
        default:
            LOG_ERROR("TEXTURE/Load", "unsupported format 0x%02x in '%s'", (unsigned)d->texFmt,
                      pathForLog ? pathForLog : "<memory>");
            return Mortar::SmartPtr<Texture>();
    }
#else
    (void)raw; (void)pathForLog;
#endif

    return Mortar::SmartPtr<Texture>(tex);
}

// Texture2D::Load @0x0022a854.
// Binary flow: src null -> null SmartPtr; else operator new(100) -> Texture2D_Bada ctor
//   -> SetSource(src,param2).
// Port: we don't have a TextureSource at Load(path) time, so we create the Texture2D_Bada
// and call Cache() directly after parsing the Tex1 data.
// DIFFERS: binary calls SetSource which calls Cache(); port calls UploadTex1ToGL inline
//   (same GL result; SetSource/Cache is the correct binary path but TextureSource
//   setup for file-path-based loading is TODO: 0x0022a854).
Mortar::SmartPtr<Texture> Texture::Load(const char* path) {
    // v1.6.1: AlternativeTextureLoader::CreateLoader handles both the fast path
    // (Prefix/Postfix empty -> returns TextureLoader over original path) and the
    // slow path (Prefix/Postfix set -> SubstituteApparentSizeTextureSource).
    AsciiString pathStr(path);
    SmartPtr<TextureSource> src = AlternativeTextureLoader::CreateLoader(pathStr);

    if (!src.IsValid()) {
        // Port-specific fallback: try TextureOverloadPrefix path.
        // DIFFERS: binary has no TextureOverloadPrefix; SDL asset-root override.
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

    // TODO: re-verify v1.6.1 Mortar::TextureFileFormat format-dispatch address -- binary type-dispatch mechanism for multi-format raw*.
    TextureFileFormat::Tex1Data* tex1 =
        static_cast<TextureFileFormat::Tex1Data*>(raw);

    Mortar::SmartPtr<Texture> result;
    if (tex1) {
        Bada::Texture2D_Bada* t = new Bada::Texture2D_Bada();
        result = UploadTex1ToGL(t, tex1, path);
        if (!result.IsValid()) {
            delete t;
        } else {
#if !defined(__bada__)
            t->m_Path = path;
#endif
        }
    } else {
        // TODO: v1.6.1 TextureFileFormat::Tex3Format::ReadFormatInternal @0x0022bc6c -- Tex3 full decode + GL upload.
        // TODO: v1.6.1 TextureFileFormat::DDSFormat::ReadFormatInternal @0x0022c7d4 -- DDS full decode + GL upload.
        // TODO: v1.6.1 TextureFileFormat::Tex2Format::ReadFormatInternal @0x0022b404 -- Tex2 full decode + GL upload.
        LOG_INFO("TEXTURE/Load", "tex3/dds/tex2 format not yet decoded for '%s'"
                 " (TODO: 0x0022bc6c / 0x0022c7d4 / 0x0022b404)", path);
    }

    src->UnlockLayers(raw);
    return result;
}

// Binary @0x00189d80 Mortar::Texture::LoadFromMemory(void const*, int).
// DIFFERS: binary constructs Texture2DFromFile_Bada and parses via GPUafyTexture;
//   port calls ReadTex1Format directly (same logic, no TextureLoader wrapping needed
//   for memory blobs -- LoadFromMemory is never called with Tex3/Tex2/DDS data).
Mortar::SmartPtr<Texture> Texture::LoadFromMemory(void const* buf, int len) {
    return ParseTexBuffer(buf, (long)len, 0);
}

// ParseTexBuffer -- shared helper for LoadFromMemory and the test path.
Mortar::SmartPtr<Texture> Texture::ParseTexBuffer(const void* data, long size,
                                                  const char* pathForLog) {
    TextureSourceData* raw = TextureFileFormat::ReadTex1Format(data, (unsigned long)size);
    if (!raw) {
        return Mortar::SmartPtr<Texture>();
    }
    TextureFileFormat::Tex1Data* d = static_cast<TextureFileFormat::Tex1Data*>(raw);
    Bada::Texture2D_Bada* tex = new Bada::Texture2D_Bada();
    Mortar::SmartPtr<Texture> result = UploadTex1ToGL(tex, d, pathForLog);
    if (!result.IsValid()) {
        delete tex;
    }
    delete raw;
    return result;
}

// ParseTex3Buffer -- magic-check only; full decode is TODO.
// Binary: Mortar::TextureFileFormat::Tex3Format::Read @0x0022bd7c.
Mortar::SmartPtr<Texture> Texture::ParseTex3Buffer(const void* data, long size,
                                                    const char* pathForLog)
{
    TextureSourceData* raw = TextureFileFormat::ReadTex3Format(data, (unsigned long)size);
    if (!raw) {
        return Mortar::SmartPtr<Texture>();
    }
    // TODO: v1.6.1 TextureFileFormat::Tex3Format::ReadFormatInternal @0x0022bc6c -- Tex3Data GL upload path (no Tex3 assets in shipped packs).
    delete raw;
    (void)pathForLog;
    return Mortar::SmartPtr<Texture>();
}

} // namespace Mortar

// ASM-spec v1.6.1 GetTextureScale1to1 @ 0x0014fa48
// Returns Vec3(GetWidth(), GetHeight(), 0) for a valid texture; Vec3::Zero otherwise.
// Binary: (w*480)/480 = w, (h*320)/320 = h (compiler artefact -- nets to identity).
_Vector3<float> GetTextureScale1to1(Mortar::SmartPtr<Mortar::Texture> tex) {
    if (tex) {
        float w = (float)tex->GetWidth();
        float h = (float)tex->GetHeight();
        return _Vector3<float>(w, h, 0.0f);
    }
    return _Vector3<float>::Zero();
}
