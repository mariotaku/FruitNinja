#ifndef FN_ENGINE_ASSET_RELOADABLE_TEXTURE_H
#define FN_ENGINE_ASSET_RELOADABLE_TEXTURE_H

// Mortar::ReloadableTexture — 8-byte texture handle record.
//
// Binary layout (sizeof = 8, ctor @ 0x001213f0 / 0x00121400):
//   +0x00  Mortar::SmartPtr<Mortar::Texture>  (4 bytes, base sub-object)
//   +0x04  char*  m_pPath                     (4 bytes)
//
// DIFFERS: original = SmartPtr<Texture>(4B) base + char* m_pPath(4B) from ctor @ 0x001213f0;
//   port = char m_Name[4] + GLuint m_Handle (same 8-byte total size, different field types).
//   sizeof matches (8 == 8) so PurchaseInfo member offsets (+0xA8/+0xB0/+0xB8) are correct.
//   Changing to the binary layout would require porting SmartPtr<Texture> as the base class
//   and all the Load/Set/UnSet methods differently; deferred until ReloadableTexture is
//   fully RE'd.
//
// Ctor zero-inits both fields (inlined in PurchaseInfo ctor 0x0011bdd8).
// Unload() zeroes m_Handle (binary @ 0x00118334, ASM-verified 2026-05-18).
//
// Used as embedded fields in PurchaseInfo (+0xA8, +0xB0, +0xB8, 8 bytes each).

#include "render/gl_funcs.h"

namespace Mortar {

class ReloadableTexture {
public:
    // DIFFERS: original = SmartPtr<Texture> base at +0x00; port = char m_Name[4]
    char   m_Name[4];   // +0x00
    // DIFFERS: original = char* m_pPath at +0x04; port = GLuint m_Handle
    GLuint m_Handle;    // +0x04

    ReloadableTexture() : m_Handle(0) {
        m_Name[0] = '\0';
        m_Name[1] = '\0';
        m_Name[2] = '\0';
        m_Name[3] = '\0';
    }

    // Zeroes the GL handle (matches binary Unload at 0x00118334).
    void Unload() {
        m_Handle = 0;
    }

    // Port-specific: loads the texture named by filename via TextureManager::LoadLocalisedTexture
    // and stores the resulting GL handle. The binary's ReloadableTexture::Load reads the filename
    // from elsewhere (see PurchaseInfo::m_TextureFilenames[]); this method is a port-side bridge.
    void Load(const char* filename);

    // Bind this texture for rendering (equivalent to Texture::Set()).
    void Set() const;

    // Unbind (bind texture 0).
    void UnSet() const;

    // True if a valid GL texture is loaded.
    bool IsLoaded() const { return m_Handle != 0; }
};

} // namespace Mortar

#ifdef __bada__
#include <cstddef>
static_assert(sizeof(Mortar::ReloadableTexture) == 8, "ReloadableTexture sizeof mismatch");
static_assert(offsetof(Mortar::ReloadableTexture, m_Name)   == 0x00, "ReloadableTexture m_Name offset");
static_assert(offsetof(Mortar::ReloadableTexture, m_Handle) == 0x04, "ReloadableTexture m_Handle offset");
#endif

#endif // FN_ENGINE_ASSET_RELOADABLE_TEXTURE_H
