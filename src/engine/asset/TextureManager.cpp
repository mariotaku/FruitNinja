#include "asset/TextureManager.h"
#include "asset/File.h"
#include "debug/Logger.h"
#include "game/GameWork.h"
#include "render/Renderer.h"
#include "render/gl_funcs.h"
#include <cstdio>
#include <cstring>

#if defined(FN_BLOCK_PRELOAD)
#include "resource/ResBlock.h"
#include <set>
#endif

namespace Mortar {

namespace {
    const char* kHdLogTag = "TextureManager";
    // Port specific: HD art is authored at 2x pixel dimensions of the
    // normal .tex. See the SetDimensions() call below.
    const int kHdScale = 2;
}

char TextureManager::s_DataDir[256] = "";

// Process-exit guard: file-static Mortar::SmartPtr<Texture> globals are destroyed
// AFTER this Meyers-static singleton because they were constructed at TU
// init time (before main()) while TextureManager is constructed lazily on
// first GetInstance(). When the SmartPtr drops the last ref, Texture::~Texture
// calls OnTextureDestroyed which would otherwise iterate a destructed
// std::map -> AV. The flag tells OnTextureDestroyed to skip the cache walk
// once the manager itself is being torn down. Set in the dtor and never
// cleared.
static bool s_TextureManagerDestroyed = false;

TextureManager::TextureManager() {
}

TextureManager::~TextureManager() {
    Clear();
    s_TextureManagerDestroyed = true;
}

// Port specific: no binary counterpart. Inserts "hd_" before the basename
// of `path` into `out` (e.g. "textures/fr/checked.tex" ->
// "textures/fr/hd_checked.tex"; a bare "checked.tex" -> "hd_checked.tex").
// Returns false (leaves `out` untouched) if `out` is too small or the
// basename already starts with "hd_" (avoids double-prefixing).
bool TextureManager::BuildHdPath(const char* path, char* out, size_t outSize) {
    const char* slash = strrchr(path, '/');
    const char* basename = slash ? (slash + 1) : path;

    if (strncmp(basename, "hd_", 3) == 0) {
        return false;
    }

    size_t prefixLen = (size_t)(basename - path);
    size_t needed = prefixLen + 3 /* "hd_" */ + strlen(basename) + 1 /* NUL */;
    if (needed > outSize) {
        return false;
    }

    memcpy(out, path, prefixLen);
    memcpy(out + prefixLen, "hd_", 3);
    strcpy(out + prefixLen + 3, basename);
    return true;
}

Mortar::SmartPtr<Texture> TextureManager::Load(const char* path,
    Mortar::SmartPtr<Mortar::TextureSource> /*source*/) {
    // Port specific: no binary counterpart. Opt-in HD texture fallback --
    // silently prefer an "hd_"-prefixed sibling file when present on disk.
    // The resolved path (HD or original) becomes the cache key so HD and
    // non-HD loads of the same logical texture never collide.
    char hdPath[512];
    const char* resolvedPath = path;
    bool isHd = false;
#ifdef FN_ENABLE_HD_ASSETS
    if (BuildHdPath(path, hdPath, sizeof(hdPath)) && Mortar::File::Exists(hdPath, 0)) {
        LOG_DEBUG(kHdLogTag, "Using HD texture: %s", hdPath);
        resolvedPath = hdPath;
        isHd = true;
    }
#else
    // HD fallback compiled out (FN_ENABLE_HD_ASSETS=OFF; default on Wii, whose
    // MEM1 ~24MB can't afford 2x textures) -- always load original-res.
    (void)hdPath;
#endif

    uint32_t hash = StringHash(resolvedPath);

    // Check cache first
    Mortar::SmartPtr<Texture> existing = Find(hash);
    if (existing.IsValid()) {
        return existing;
    }

    // Cache miss -- load from disk.
#if defined(FN_BLOCK_PRELOAD)
    // Task #36 Stage 1 -- fail-loud instrumentation (log-only; no preload yet,
    // see tmp/wii/loader-blueprint.md section 6/7). Fires once per unique
    // resolved path so a Dolphin run's log enumerates the per-block texture
    // set without per-frame spam.
    {
        static std::set<uint32_t> s_LoggedHashes;
        if (s_LoggedHashes.insert(hash).second) {
            LOG_INFO("BlockLoad", "[BlockLoad] block=%s loading %s (TEX)",
                     fn::wii::GetCurrentBlockName(), resolvedPath);
        }
    }
#endif
    // Texture::Load internally calls AlternativeTextureLoader::CreateLoader which
    // handles the Prefix/Postfix path-rewrite when enabled. This is the binary-faithful
    // dispatch order: TextureManager::Load -> Texture::Load -> AlternativeTextureLoader
    // -> TextureLoader -> g_readers[].
    Mortar::SmartPtr<Texture> tex = Texture::Load(resolvedPath);
    if (tex.IsValid()) {
        // Port specific: HD art is authored at 2x pixel dimensions. Halve the
        // reported apparent size so widgets/sprites that size their quads
        // from GetWidth()/GetHeight() draw at the SAME on-screen footprint
        // as the normal texture -- the full-res GL pixels are then sampled
        // over that (unchanged) footprint, i.e. crisper at no layout cost.
        // UV/sampling is unaffected: draw calls pass explicit [0,1] (or
        // Texture2D's precomputed UV verts) independent of apparentWidth/
        // Height, which are consumed only as (a) the glTexImage2D upload
        // size (already the full 2x pixels, set before this point by
        // Texture2D_Bada::Cache/UploadTex1ToGL) and (b) this draw-size
        // accessor -- so halving here only shrinks the reported size, never
        // the sampled image.
        if (isHd) {
            tex->SetDimensions(tex->GetWidth() / kHdScale, tex->GetHeight() / kHdScale);
        }
        Add(hash, tex);
    }
    return tex;
}

Mortar::SmartPtr<Texture> TextureManager::Find(uint32_t hash) const {
    std::map<uint32_t, CacheEntry>::const_iterator it = m_Cache.find(hash);
    if (it != m_Cache.end()) {
        return Mortar::SmartPtr<Texture>(it->second.ptr);
    }
    return Mortar::SmartPtr<Texture>();
}

Mortar::SmartPtr<Texture> TextureManager::Find(const char* name) const {
    return Find(StringHash(name));
}

void TextureManager::Add(uint32_t hash, Mortar::SmartPtr<Texture> tex) {
    m_Cache[hash].ptr = tex.Get();
}

void TextureManager::Add(const char* name, Mortar::SmartPtr<Texture> tex) {
    Add(StringHash(name), tex);
}

void TextureManager::PurgeExpired() {
}

void TextureManager::OnTextureDestroyed(Texture* tex) {
    if (!tex) return;
    if (s_TextureManagerDestroyed) return;
    for (std::map<uint32_t, CacheEntry>::iterator it = m_Cache.begin();
         it != m_Cache.end(); ) {
        if (it->second.ptr == tex) {
            m_Cache.erase(it++);
        } else {
            ++it;
        }
    }
}

void TextureManager::Clear() {
    m_Cache.clear();
}

void TextureManager::SetDataDir(const char* dir) {
    strncpy(s_DataDir, dir, sizeof(s_DataDir) - 1);
    s_DataDir[sizeof(s_DataDir) - 1] = '\0';
}

const char* TextureManager::GetDataDir() {
    return s_DataDir;
}

// ASM-spec v1.6.1 LoadLocalisedTexture @ 0x0011a768 (thunk 0x00112d3c):
//  1. Switch on game_work.bM_LangId (+0x03 byte):
//       2=fr, 3=es, 4=de, 5=it, 11=ko, 12=ja, 13=zh; others -> no localized dir
//  2. If lang has a dir: sprintf(buf, "textures/<lang>/%s", name)
//     File::Exists(buf,0) -> if true, goto load
//  3. Fallback: snprintf(buf, 0x200, "textures/%s", name)
//     File::Exists(buf,0) -> if false, return empty SmartPtr
//  4. load: return TextureManager::GetInstance().Load(buf)
// Note: v1.5.x (0x0010a758) did NOT have the locale switch; this changed in v1.6.1.
// Note: no textures/<lang>/ subdirs exist in the current asset tree; the fallback
//       fires for English assets. The localized path is latent until localized assets
//       are present.
Mortar::SmartPtr<Texture> TextureManager::LoadLocalisedTexture(const char* name) {
    static const char* kLangSuffix[14] = {
        NULL, NULL, "fr", "es", "de", "it",
        NULL, NULL, NULL, NULL, NULL,
        "ko", "ja", "zh"
    };
    const char* suffix = (game_work.languageFlag < 14)
        ? kLangSuffix[game_work.languageFlag] : NULL;

    char buf[512];

    if (suffix != NULL) {
        snprintf(buf, sizeof(buf), "textures/%s/%s", suffix, name);
        if (Mortar::File::Exists(buf, 0)) {
            return TextureManager::GetInstance().Load(buf);
        }
    }

    snprintf(buf, sizeof(buf), "textures/%s", name);
    if (!Mortar::File::Exists(buf, 0)) {
        return Mortar::SmartPtr<Texture>();
    }
    return TextureManager::GetInstance().Load(buf);
}

} // namespace Mortar

namespace Mortar {

// Binary @ 0x00188db8 -- body is literally `return this;`.
TextureManager* TextureManager::Destroy() {
    return this;
}

// Binary @ 0x00188de4 -- tail-calls InitialiseInternal().
void TextureManager::Initialise(int) {
    InitialiseInternal();
}

// Binary @ 0x000f609c -- thunk to empty body @ 0x001a73d0.
void TextureManager::InitialiseInternal() {
}

// ASM-spec v1.6.1 Mortar::TextureManager::LoadIndependent(void*, int) @0x00227230: ignores both params, returns SmartPtr<Texture>(NULL). No callers in v1.6.1 (dead API).
Mortar::SmartPtr<Texture> TextureManager::LoadIndependent(void* /*data*/, int /*size*/) {
    return Mortar::SmartPtr<Texture>();
}

// Port specific: no binary counterpart. 1x1 (or NxN via px replicate -- always
// 1x1 today, no caller needs larger) solid-colour GL texture, moved here from
// the debug-overlay TU so raw GL stays confined to engine/.
uint32_t TextureManager::CreateSolidTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
#if defined(__bada__) || defined(FN_GL_STUB)
    (void)r; (void)g; (void)b; (void)a;
    return 0;
#else
    GLuint id = 0;
    glGenTextures(1, &id);
    // Port specific: upload bind -- immediate, with Renderer shadow sync
    // (same in CreateTextureFromRGBA below).
    if (Renderer* rend = Renderer::GetInstance()) {
        rend->BindTextureForUpload((uint32_t)id);
    } else {
        glBindTexture(GL_TEXTURE_2D, id);
    }
    const uint8_t px[4] = { r, g, b, a };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return (uint32_t)id;
#endif
}

// Port specific: no binary counterpart. Moved here from src/hud/WidgetPlaceholderArt.h's
// four Make*Tex helpers, which all duplicated this exact glGenTextures/glTexImage2D
// sequence -- their SDF pixel-generation logic stays put, only the upload is shared.
uint32_t TextureManager::CreateTextureFromRGBA(const uint8_t* rgba, int w, int h, bool linearFilter) {
#if defined(__bada__) || defined(FN_GL_STUB)
    (void)rgba; (void)w; (void)h; (void)linearFilter;
    return 0;
#else
    GLuint id = 0;
    glGenTextures(1, &id);
    Renderer* rend = Renderer::GetInstance();
    if (rend) rend->BindTextureForUpload((uint32_t)id);
    else      glBindTexture(GL_TEXTURE_2D, id);
    const GLint filter = linearFilter ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (rend) rend->BindTextureForUpload(0);
    else      glBindTexture(GL_TEXTURE_2D, 0);
    return (uint32_t)id;
#endif
}

// Port specific: Bada VRAM is managed by the driver; no-op on SDL/OpenGL (faithful: binary is also constant false).
// v1.6.1 Mortar::DefragVRamNeeded @0x00229a68
bool DefragVRamNeeded() {
    return false;
}

// Port specific: Bada VRAM management; no-op on SDL/OpenGL (faithful: binary is also a no-op).
// v1.6.1 Mortar::DefragVRam @0x00229a70
void DefragVRam() {
}

}  // namespace Mortar

// ASM-spec v1.6.1 LoadTexture @0x14f88c: appends ".tex" to name, delegates to
// TextureManager::LoadLocalisedTexture. buf[64] matches the binary's 0x40-byte stack frame.
Mortar::SmartPtr<Mortar::Texture> LoadTexture(const char* name) {
    if (!name) return Mortar::SmartPtr<Mortar::Texture>();
    char buf[64];
    snprintf(buf, 64, "%s.tex", name);
    return Mortar::TextureManager::LoadLocalisedTexture(buf);
}
