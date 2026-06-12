#include "asset/Texture.h"
#include "asset/TextureManager.h"
#include "asset/AlternativeTextureLoader.h"
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

    // v1.6.1: peek 4 bytes and dispatch to the .tex3 parser if magic matches;
    // fall through to the existing .tex (Tex1) parser otherwise.
    // TODO: 0x002d4b20 -- full TextureFileFormat polymorphic registry dispatch.
    Mortar::SmartPtr<Texture> tex =
        ParseTex3Buffer(f.Data(), (long)f.Size(), path);
    if (!tex.IsValid()) {
        tex = ParseTexBuffer(f.Data(), (long)f.Size(), path);
    }
    if (tex.IsValid()) {
        tex->m_Path = path;
    }
    return tex;
}

// Matches GPUafyTexture (0x001898d8) + TexFmtToGL (0x00189f78). Shared by
// Load() and LoadFromMemory().
Mortar::SmartPtr<Texture> Texture::ParseTexBuffer(const void* data, long size,
                                                  const char* pathForLog) {
    if (size < 12) {
        return Mortar::SmartPtr<Texture>();
    }

    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint8_t widthLog2  = bytes[0];
    uint8_t heightLog2 = bytes[1];
    uint8_t format     = bytes[2];
    int width  = 1 << widthLog2;
    int height = 1 << heightLog2;

    long dataSize = size - 12;
    const uint8_t* raw = bytes + 12;
    if (dataSize <= 0) {
        return Mortar::SmartPtr<Texture>();
    }

    Texture* tex = new Texture();

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
            LOG_ERROR("TEXTURE/Load", "unsupported format 0x%02x in '%s'", format,
                      pathForLog ? pathForLog : "<memory>");
            delete tex;
            return Mortar::SmartPtr<Texture>();
    }

    return Mortar::SmartPtr<Texture>(tex);
}

// v1.6.1 addition: .tex3 container parser.
// Binary: Mortar::TextureFileFormat::Tex3Format::Read @ 0x0022bd7c,
//   _GLOBAL__N_1::ReadFormatInternal @ 0x0022bc6c (reads via DataStreamReader).
// The .tex3 magic is a 4-byte FourCC set at static-init by
//   _GLOBAL__I_Tex3Format.cpp @ 0x0022be94 (copied from a GOT slot).
// Port: magic bytes are the literal 4-char sequence used by Halfbrick ('TEX3' expected;
// exact value not statically readable from the binary -- treat as a 4-byte sentinel).
// Minimal viable: parse magic + layer-0 TextureInfo (format/w/h) + layer-0 blob and
// UploadNative on layer-0. Multi-layer mip handling // TODO: 0x0022bd7c.
// TODO: 0x002d4b20 -- full Mortar::TextureFileFormat polymorphic reader registry
// (Tex1/Tex2/Tex3/DDS entries); port uses a magic-byte if/else instead.
Mortar::SmartPtr<Texture> Texture::ParseTex3Buffer(const void* data, long size,
                                                    const char* pathForLog)
{
    // The .tex3 magic is 4 bytes at offset +0. If the file doesn't start with
    // the expected FourCC, return null so the caller can fall through to the
    // existing .tex (Tex1) path.
    // TODO: 0x0022be94 -- confirm exact 4-byte FourCC from static-init GOT slot.
    // Using placeholder sentinel; real magic must be confirmed by re-analyst before
    // matching real .tex3 files.
    static const char kTex3Magic[4] = { 'T', 'E', 'X', '3' };
    if (size < 4) {
        return Mortar::SmartPtr<Texture>();
    }
    if (memcmp(data, kTex3Magic, 4) != 0) {
        // Not a .tex3 file; caller should try the .tex parser.
        return Mortar::SmartPtr<Texture>();
    }

    // TODO: 0x0022bc6c -- ReadFormatInternal: allocate Tex3Data (0x4c bytes), read
    // TextureInfo fields (format/numLayersX/numLayersY via MakeIntFormat helpers),
    // then read the per-layer size table and accumulate layer-data offsets.
    // The port stubs this out as a null result for now; .tex3 files are not present
    // in the shipped 1.5.1/1.6.1 asset packs used by this port target.
    LOG_INFO("TEXTURE/ParseTex3Buffer", "tex3 container detected in '%s'; "
             "full decode not yet implemented (TODO: 0x0022bc6c)",
             pathForLog ? pathForLog : "<memory>");
    return Mortar::SmartPtr<Texture>();
}

} // namespace Mortar

namespace Mortar {

// Binary @ 0x00189d80 Mortar::Texture::LoadFromMemory(void const*, int).
// The binary does:
//   t = operator new(0x20);
//   Texture2DFromFile_Bada::ctor(t, buf, len, 0xffffffff);  // FromMemoryInit
//   wrap t in SmartPtr<Texture> and return it (via the hidden sret slot).
// FromMemoryInit (0x001899dc) parses the same .tex layout as Load() through
// GPUafyTexture + TexFmtToGL, so the port routes the in-memory blob through
// the shared ParseTexBuffer helper. No file path -> no m_Path / overload
// fallback (the file-path variant lives in Load()).
Mortar::SmartPtr<Texture> Texture::LoadFromMemory(void const* buf, int len) {
    return ParseTexBuffer(buf, (long)len, nullptr);
}

// Binary @ 0x00188da4 Mortar::Texture::SetUnCached().
// Binary: if (vtable_slot3() == 0) Set();  where slot3 is Texture2D::GetType
// (vtable +0xc), which returns 0 for a plain Texture2D -- so Set() always
// runs. The port has merged the concrete Texture2D into Texture and has no
// GetType subtype virtual, making the gate unconditionally true.
void Texture::SetUnCached() {
    Set();
}

// Binary @ 0x00188d9c Mortar::Texture::UnSetUnCached() -- forwards to UnSet().
void Texture::UnSetUnCached() {
    UnSet();
}

}  // namespace Mortar
