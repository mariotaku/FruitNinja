#include "asset/TextureManager.h"
#include <cstdio>
#include <cstring>

namespace Mortar {

char TextureManager::s_DataDir[256] = "";

TextureManager::TextureManager() {
}

TextureManager::~TextureManager() {
    Clear();
}

SmartPtr<Texture> TextureManager::Load(const char* path) {
    uint32_t hash = StringHash(path);

    // Check cache first
    SmartPtr<Texture> existing = Find(hash);
    if (existing.IsValid()) {
        return existing;
    }

    // Cache miss — load from disk
    SmartPtr<Texture> tex = Texture::Load(path);
    if (tex.IsValid()) {
        Add(hash, tex);
    }
    return tex;
}

SmartPtr<Texture> TextureManager::Find(uint32_t hash) const {
    std::map<uint32_t, CacheEntry>::const_iterator it = m_Cache.find(hash);
    if (it != m_Cache.end() && it->second.ptr != nullptr) {
        // The cache stores raw pointers; Texture::~Texture removes its
        // entry before the object is freed, so any non-null pointer
        // here is still alive. Wrap in a fresh SmartPtr — its ctor
        // bumps the strong refcount and keeps the texture alive while
        // the caller holds it.
        return SmartPtr<Texture>(it->second.ptr);
    }
    return SmartPtr<Texture>();
}

SmartPtr<Texture> TextureManager::Find(const char* name) const {
    return Find(StringHash(name));
}

void TextureManager::Add(uint32_t hash, SmartPtr<Texture> tex) {
    m_Cache[hash].ptr = tex.Get();
}

void TextureManager::Add(const char* name, SmartPtr<Texture> tex) {
    Add(StringHash(name), tex);
}

void TextureManager::PurgeExpired() {
    // No-op — the binary's TextureManager has no PurgeExpired method
    // either. Cache entries clean themselves up via OnTextureDestroyed
    // when the last SmartPtr ref to a Texture is released. Kept as a
    // stub so existing call sites compile.
}

void TextureManager::OnTextureDestroyed(Texture* tex) {
    // Linear scan — cache size is small (a few hundred textures max)
    // and destroy events are rare, so the O(N) walk isn't worth a
    // reverse map. Matches the binary's WeakPtr cleanup path which
    // also walks the map on weak-ref decrement.
    if (!tex) return;
    for (std::map<uint32_t, CacheEntry>::iterator it = m_Cache.begin();
         it != m_Cache.end(); ) {
        if (it->second.ptr == tex) {
            it = m_Cache.erase(it);
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
// Builds full path from data dir + "textures/" + name, loads via TextureManager cache.
SmartPtr<Texture> TextureManager::LoadLocalisedTexture(const char* name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/textures/%s", s_DataDir, name);
    return TextureManager::GetInstance().Load(path);
}

} // namespace Mortar
