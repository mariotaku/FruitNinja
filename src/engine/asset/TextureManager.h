#ifndef MORTAR_TEXTURE_MANAGER_H
#define MORTAR_TEXTURE_MANAGER_H

#include "asset/Texture.h"
#include "util/SmartPtr.h"
#include "util/StringHash.h"
#include "core/Singleton.h"
#include <map>
#include <cstdint>

namespace Mortar {

// Matches original TextureManager (singleton, 24 bytes)
// Cache of loaded textures keyed by StringHash(filename)
class TextureManager : public Singleton<TextureManager> {
    friend class Singleton<TextureManager>;

public:
    // Load texture by full path, using cache
    // Returns cached version if already loaded, otherwise loads from disk
    SmartPtr<Texture> Load(const char* path);

    // Matches LoadLocalisedTexture (0x0010a758)
    // Loads texture by name from the data/textures/ directory.
    // Tries localised path first (e.g. "textures/en/name"), falls back to "textures/name".
    static SmartPtr<Texture> LoadLocalisedTexture(const char* name);

    // Set the base data directory for texture loading (e.g. "/path/to/Data")
    static void SetDataDir(const char* dir);
    static const char* GetDataDir();

    // Find cached texture by hash
    SmartPtr<Texture> Find(uint32_t hash) const;

    // Find cached texture by name
    SmartPtr<Texture> Find(const char* name) const;

    // Add texture to cache
    void Add(uint32_t hash, SmartPtr<Texture> tex);
    void Add(const char* name, SmartPtr<Texture> tex);

    // Remove expired (zero ref) entries
    void PurgeExpired();

    // Clear all cached textures
    void Clear();

private:
    TextureManager();
    ~TextureManager();

    // WeakPtr-like behavior: store raw pointer, check ref count
    // Original uses WeakPtr<Texture> but we simplify with raw pointer tracking
    struct CacheEntry {
        Texture* ptr;
        CacheEntry() : ptr(NULL) {}
        CacheEntry(Texture* p) : ptr(p) {}
    };

    std::map<uint32_t, CacheEntry> m_Cache;

    static char s_DataDir[256];
};

} // namespace Mortar

#endif
