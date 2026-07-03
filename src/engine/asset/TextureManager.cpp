#include "asset/TextureManager.h"
#include "asset/File.h"
#include "game/GameWork.h"
#include <cstdio>
#include <cstring>

namespace Mortar {

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

Mortar::SmartPtr<Texture> TextureManager::Load(const char* path,
    Mortar::SmartPtr<Mortar::TextureSource> /*source*/) {
    uint32_t hash = StringHash(path);

    // Check cache first
    Mortar::SmartPtr<Texture> existing = Find(hash);
    if (existing.IsValid()) {
        return existing;
    }

    // Cache miss -- load from disk.
    // Texture::Load internally calls AlternativeTextureLoader::CreateLoader which
    // handles the Prefix/Postfix path-rewrite when enabled. This is the binary-faithful
    // dispatch order: TextureManager::Load -> Texture::Load -> AlternativeTextureLoader
    // -> TextureLoader -> g_readers[].
    Mortar::SmartPtr<Texture> tex = Texture::Load(path);
    if (tex.IsValid()) {
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
