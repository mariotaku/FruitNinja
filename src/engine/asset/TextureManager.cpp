#include "asset/TextureManager.h"
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

// Matches LoadLocalisedTexture (0x0010a758)
Mortar::SmartPtr<Texture> TextureManager::LoadLocalisedTexture(const char* name) {
    char path[512];
    snprintf(path, sizeof(path), "textures/%s", name);
    return TextureManager::GetInstance().Load(path);
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

// Binary @ 0x00188dd4 -- returns empty SmartPtr<Texture>.
Mortar::SmartPtr<Texture> TextureManager::LoadIndependent(int) {
    return Mortar::SmartPtr<Texture>();
}

}  // namespace Mortar
