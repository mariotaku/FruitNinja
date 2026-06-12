#include "asset/TextureManager.h"
#include "asset/AlternativeTextureLoader.h"
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

Mortar::SmartPtr<Texture> TextureManager::Load(const char* path) {
    uint32_t hash = StringHash(path);

    // Check cache first
    Mortar::SmartPtr<Texture> existing = Find(hash);
    if (existing.IsValid()) {
        return existing;
    }

    // v1.6.1: AlternativeTextureLoader path-rewrite hook.
    // When Texture::UseAlternativeTextureLoader is true the path is rewritten via
    // AlternativeTextureLoader::CreateLoader before opening the file.
    // Binary dispatch point mirrors the loader-selection code in the 1.6.1 binary.
    // Port: UseAlternativeTextureLoader defaults false and Prefix/Postfix are empty,
    // so this branch is never taken in the shipped configuration.
    const char* loadPath = path;
    Mortar::SmartPtr<AlternativeTextureLoaderObj> altLoader;
    if (Texture::UseAlternativeTextureLoader) {
        Mortar::AsciiString pathStr(path);
        altLoader = AlternativeTextureLoader::CreateLoader(pathStr);
        if (altLoader.IsValid()) {
            loadPath = altLoader->m_ResolvedPath.c_str();
        }
    }

    // Cache miss — load from disk
    Mortar::SmartPtr<Texture> tex = Texture::Load(loadPath);
    if (tex.IsValid()) {
        Add(hash, tex);
    }
    return tex;
}

Mortar::SmartPtr<Texture> TextureManager::Find(uint32_t hash) const {
    std::map<uint32_t, CacheEntry>::const_iterator it = m_Cache.find(hash);
    if (it != m_Cache.end() && it->second.ptr != nullptr) {
        // The cache stores raw pointers; Texture::~Texture removes its
        // entry before the object is freed, so any non-null pointer
        // here is still alive. Wrap in a fresh SmartPtr — its ctor
        // bumps the strong refcount and keeps the texture alive while
        // the caller holds it.
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
    // Process-exit safety: file-static Mortar::SmartPtr<Texture> globals destroy
    // after our singleton (see s_TextureManagerDestroyed comment). Bail
    // before touching m_Cache.
    if (s_TextureManagerDestroyed) return;
    for (std::map<uint32_t, CacheEntry>::iterator it = m_Cache.begin();
         it != m_Cache.end(); ) {
        if (it->second.ptr == tex) {
            // C++03 map::erase(iterator) returns void; use post-increment idiom.
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
// Builds full path from data dir + "textures/" + name, loads via TextureManager cache.
Mortar::SmartPtr<Texture> TextureManager::LoadLocalisedTexture(const char* name) {
    // Pass LOGICAL path -- FileSystem_Direct::TranslateFileName prepends
    // the registered root (data_dir). Prepending s_DataDir here too would
    // double-prefix and break loads after the OpenCI -> File::Open
    // migration in R4 W5 commit 0b6143f.
    char path[512];
    snprintf(path, sizeof(path), "textures/%s", name);
    return TextureManager::GetInstance().Load(path);
}

} // namespace Mortar

namespace Mortar {

// Binary @ 0x00188db8 -- body is literally `return this;`. No teardown work;
// it is an identity no-op release hook. We mirror that exactly.
TextureManager* TextureManager::Destroy() {
    return this;
}

// Binary @ 0x00188de4 -- tail-calls InitialiseInternal(); the int argument is
// pushed by callers but never consumed (the eventual body reads nothing).
void TextureManager::Initialise(int) {
    InitialiseInternal();
}

// Binary @ 0x000f609c -- thunk through PTR_InitialiseInternal_001ecf84 to the
// real body @ 0x001a73d0, which is empty (`return;`). The texture cache is
// populated lazily on demand via Load(), so there is nothing to do at init.
void TextureManager::InitialiseInternal() {
}

// Binary @ 0x00188dd4 -- constructs an empty SmartPtr<Texture> into the RVO
// return slot and returns it; the int argument is never read. Faithful
// behaviour is an empty (null) texture handle.
Mortar::SmartPtr<Texture> TextureManager::LoadIndependent(int) {
    return Mortar::SmartPtr<Texture>();
}

}  // namespace Mortar
