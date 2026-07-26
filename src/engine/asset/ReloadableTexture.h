#ifndef FN_ENGINE_ASSET_RELOADABLE_TEXTURE_H
#define FN_ENGINE_ASSET_RELOADABLE_TEXTURE_H

// Mortar::ReloadableTexture — 8-byte texture handle record. NO vtable.
//
// Binary layout (sizeof = 8, v1.6.1 ReloadableTexture::ReloadableTexture ctor @0x0014f8e8):
//   +0x00  Mortar::SmartPtr<Mortar::Texture>  m_Texture  (4 bytes)
//   +0x04  char*                              m_pPath    (4 bytes; OWNED heap buffer,
//                                                          new[]/delete[]/strcpy -- NOT a
//                                                          raw pointer into XML/caller data)
//
// Methods (all non-virtual __thiscall):
//   ctor()                    @0x0014f8e8: m_Texture() null, m_pPath=0.
//   ctor(const char*)         @0x0014f9c0: init + operator=(path).
//   copy ctor                 @0x0014f920: init, operator=(rhs.m_pPath) [deep-copy path],
//                                          then m_Texture=rhs.m_Texture.
//   operator=(const char*)    @0x0014f7fc: path==NULL -> delete[] m_pPath; m_pPath=0.
//                                          Else strlen; if existing buffer length differs,
//                                          delete[] + new[](len+1); strcpy. (Reuses buffer
//                                          on equal length.) Does NOT load.
//   Load() (no-arg)           @0x0014fad8: if (m_pPath && !m_Texture.IsValid())
//                                          m_Texture = LoadTexture(m_pPath).
//   Unload()                  @0x0014f878: m_Texture.SetNull().
//   GetTexture()               @0x0011344c: return &m_Texture.
//   dtor: delete[] m_pPath (+ SmartPtr auto-release). Inlined at call sites in the binary;
//         the port gives it a real out-of-line body.
//
// Rule-of-three is MANDATORY: EffectImage (which derives from this class) lives in a
// std::vector<EffectImage>; a vector reallocation copies the base sub-object, which owns
// m_pPath. Without a deep-copying copy-ctor/copy-assign, a reallocation double-frees or
// leaks the path buffer.

#include <cstddef>
#include <cstring>
#include "util/SmartPtr.h"
#include "asset/Texture.h"
#include "render/gl_funcs.h"

namespace Mortar {

class ReloadableTexture {
public:
    // +0x00
    Mortar::SmartPtr<Mortar::Texture> m_Texture;
    // +0x04: owned heap buffer (new[]/delete[]/strcpy).
    char* m_pPath;

    // v1.6.1 ReloadableTexture::ReloadableTexture @0x0014f8e8.
    ReloadableTexture() : m_Texture(), m_pPath(nullptr) {}

    // v1.6.1 ReloadableTexture::ReloadableTexture(const char*) @0x0014f9c0.
    explicit ReloadableTexture(const char* path) : m_Texture(), m_pPath(nullptr) {
        *this = path;
    }

    // v1.6.1 ReloadableTexture copy ctor @0x0014f920: deep-copies m_pPath, then
    // copies the SmartPtr.
    ReloadableTexture(const ReloadableTexture& rhs) : m_Texture(), m_pPath(nullptr) {
        *this = rhs.m_pPath;
        m_Texture = rhs.m_Texture;
    }

    ReloadableTexture& operator=(const ReloadableTexture& rhs) {
        if (this != &rhs) {
            *this = rhs.m_pPath;
            m_Texture = rhs.m_Texture;
        }
        return *this;
    }

    ~ReloadableTexture() {
        delete[] m_pPath;
    }

    // v1.6.1 ReloadableTexture::operator=(const char*) @0x0014f7fc: NULL frees the
    // owned buffer; otherwise reuses the buffer if the new string is the same length,
    // else delete[]+new[](len+1). Does not load.
    ReloadableTexture& operator=(const char* path) {
        if (!path) {
            delete[] m_pPath;
            m_pPath = nullptr;
            return *this;
        }
        size_t newLen = strlen(path);
        size_t oldLen = m_pPath ? strlen(m_pPath) : (size_t)-1;
        if (oldLen != newLen) {
            delete[] m_pPath;
            m_pPath = new char[newLen + 1];
        }
        strcpy(m_pPath, path);
        return *this;
    }

    // v1.6.1 ReloadableTexture::Load @0x0014fad8: if (m_pPath && !m_Texture.IsValid())
    // m_Texture = LoadTexture(m_pPath). LoadTexture (global, TextureManager.h) appends
    // ".tex" and forwards to TextureManager::LoadLocalisedTexture.
    void Load();

    // Port specific: convenience bridge for callers (PurchaseInfo) that already carry
    // a fully-suffixed filename (e.g. "arcade_item_01_buy.tex"). Sets the path directly
    // and loads via TextureManager without the ".tex" auto-append Load() applies via the
    // global LoadTexture() -- calling the bare Load() here would double-suffix ".tex.tex"
    // for those callers. Not a binary method; the binary has no such overload.
    void Load(const char* filename);

    // v1.6.1 ReloadableTexture::Unload @0x0014f878: m_Texture.SetNull().
    void Unload() { m_Texture.SetNull(); }

    // v1.6.1 ReloadableTexture::GetTexture @0x0011344c: return &m_Texture.
    Mortar::SmartPtr<Mortar::Texture>* GetTexture() { return &m_Texture; }

    // Bind this texture for rendering (equivalent to Texture::Set()).
    // Port specific: routed through Renderer::BindTexture2D (lazy sampling
    // bind); body out-of-line in ReloadableTexture.cpp to keep Renderer.h out
    // of this widely-included header.
    void Set() const;

    // Unbind (bind texture 0). Same lazy routing as Set().
    void UnSet() const;

    // True if a valid texture is loaded.
    bool IsLoaded() const { return m_Texture.IsValid(); }
};

} // namespace Mortar

#if defined(__bada__)
static_assert(sizeof(Mortar::ReloadableTexture) == 8, "ReloadableTexture sizeof mismatch");
static_assert(offsetof(Mortar::ReloadableTexture, m_Texture) == 0x00, "ReloadableTexture m_Texture offset");
static_assert(offsetof(Mortar::ReloadableTexture, m_pPath)   == 0x04, "ReloadableTexture m_pPath offset");
#endif

#endif // FN_ENGINE_ASSET_RELOADABLE_TEXTURE_H
