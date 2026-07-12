#ifndef MORTAR_TEXTURE_MANAGER_H
#define MORTAR_TEXTURE_MANAGER_H

#include "asset/Texture.h"
#include "asset/TextureSource.h"
#include "util/SmartPtr.h"
#include "util/StringHash.h"
#include "core/Singleton.h"
#include <map>
#include <cstddef>
#include <cstdint>

namespace Mortar {

// Matches original TextureManager (singleton, 24 bytes)
// Cache of loaded textures keyed by StringHash(filename)
class TextureManager : public Singleton<TextureManager> {
    friend class Singleton<TextureManager>;

public:
    // Load texture by full path, using cache
    // Returns cached version if already loaded, otherwise loads from disk
    // DIFFERS: binary has second SmartPtr<TextureSource> param (v1.6.1 Mortar::TextureManager::Load @0x002274d0).
    //
    // Port specific: no binary counterpart. Opt-in "HD texture" fallback --
    // before opening `path`, probes for an "hd_"-prefixed sibling next to the
    // basename (e.g. "textures/checked.tex" -> "textures/hd_checked.tex").
    // If that file exists on disk, it is loaded (and cached) INSTEAD of
    // `path`; the cache key is the HD path actually loaded, so it never
    // collides with the non-HD entry. Silent fallback to `path` when no HD
    // variant exists (the common case today -- no hd_ assets ship yet); no
    // error/log on the miss.
    Mortar::SmartPtr<Texture> Load(const char* path,
        Mortar::SmartPtr<Mortar::TextureSource> source = Mortar::SmartPtr<Mortar::TextureSource>());

    // v1.6.1 LoadLocalisedTexture @ 0x0011a768 (thunk @ 0x00112d3c)
    // Loads texture by name from the data/textures/ directory.
    // If game_work.languageFlag selects a known locale (2=fr/3=es/4=de/5=it/11=ko/12=ja/13=zh),
    // tries "textures/<lang>/<name>" first (File::Exists gate). Falls back to "textures/<name>".
    // Returns empty SmartPtr if neither path exists on disk.
    // DIFFERS from v1.5.x @0x0010a758 (base path only -- no locale switch).
    static Mortar::SmartPtr<Texture> LoadLocalisedTexture(const char* name);

    // Set the base data directory for texture loading (e.g. "/path/to/Data")
    static void SetDataDir(const char* dir);
    static const char* GetDataDir();

    // Find cached texture by hash
    Mortar::SmartPtr<Texture> Find(uint32_t hash) const;

    // Find cached texture by name
    Mortar::SmartPtr<Texture> Find(const char* name) const;

    // Add texture to cache
    void Add(uint32_t hash, Mortar::SmartPtr<Texture> tex);
    void Add(const char* name, Mortar::SmartPtr<Texture> tex);

    // Remove expired (zero ref) entries
    void PurgeExpired();

    // Clear all cached textures
    void Clear();

private:
    TextureManager();
    ~TextureManager();

    // Port specific: no binary counterpart. Opt-in HD texture support --
    // inserts "hd_" before the basename of `path` into `out`. Fails (returns
    // false) if the buffer is too small or the basename already starts with
    // "hd_" (avoids double-prefixing on repeat calls / already-HD paths).
    static bool BuildHdPath(const char* path, char* out, size_t outSize);

    // WeakPtr-equivalent cache: stores raw Texture* — the Texture
    // destructor calls OnTextureDestroyed() which removes its entry
    // before the object is freed, so the cache never holds a dangling
    // pointer. Matches the binary's `std::map<ulong, WeakPtr<Texture>>`
    // semantics: Find returns null when the referent has been destroyed,
    // and the next Load re-creates from disk. Self-cleaning, no manual
    // PurgeExpired needed.
    struct CacheEntry {
        Texture* ptr;
        CacheEntry() : ptr(nullptr) {}
        CacheEntry(Texture* p) : ptr(p) {}
    };

    std::map<uint32_t, CacheEntry> m_Cache;

public:
    // Called by Texture::~Texture before the GL handle is deleted.
    // Walks the cache and removes any entry pointing to the destroyed
    // texture so subsequent Find() calls return null and trigger a
    // fresh load.
    void OnTextureDestroyed(Texture* tex);

    static char s_DataDir[256];

public:
    // Binary @ 0x00188db8 -- identity no-op release hook: the binary body is
    // `return this;` with no teardown work. Returns TextureManager* to match
    // the binary's calling convention (callers ignore the result).
    TextureManager* Destroy();
    // Binary @ 0x00188de4 -- forwards to InitialiseInternal(). The int arg is
    // accepted but unused by the binary (passed in the call but never read by
    // the empty InitialiseInternal body).
    void Initialise(int);
    // Binary @ 0x000f609c -- thunk through PTR_InitialiseInternal_001ecf84,
    // which resolves to the real body @ 0x001a73d0. That body is empty
    // (`return;`) -- no cache/state population happens here.
    void InitialiseInternal();
    // Binary @ 0x00227230 -- constructs and returns an empty SmartPtr<Texture>
    // (the body is just `SmartPtr<Texture>(nullptr)` written to the RVO return
    // slot). The int arg is accepted but never used. Faithful behaviour is to
    // return a null texture handle.
    Mortar::SmartPtr<Texture> LoadIndependent(void* data, int size);
};

// v1.6.1 Mortar::DefragVRamNeeded @0x00229a68 -- `mov r0,#0; bx lr` (always false).
// Port specific: Bada VRAM is managed by the driver; no-op on SDL/OpenGL (faithful: binary is also constant false).
bool DefragVRamNeeded();

// v1.6.1 Mortar::DefragVRam @0x00229a70 -- `bx lr` (true no-op).
// Port specific: Bada VRAM management; no-op on SDL/OpenGL (faithful: binary is also a no-op).
void DefragVRam();

} // namespace Mortar

// ASM-spec v1.6.1 LoadTexture @0x14f88c: free function (global namespace).
// Appends ".tex" to name and forwards to TextureManager::LoadLocalisedTexture.
// Binary uses struct-return ABI (SmartPtr returned via hidden out-param).
Mortar::SmartPtr<Mortar::Texture> LoadTexture(const char* name);

#endif
