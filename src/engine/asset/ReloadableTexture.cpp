// Mortar::ReloadableTexture — see header for the full binary-layout spec.

#include "asset/ReloadableTexture.h"
#include "asset/TextureManager.h"

// v1.6.1 ReloadableTexture::Load @0x0014fad8.
void Mortar::ReloadableTexture::Load() {
    if (m_pPath && !m_Texture.IsValid()) {
        m_Texture = ::LoadTexture(m_pPath);
    }
}

// Port specific: see header comment -- filename is already fully-suffixed by the
// caller, so load directly instead of going through Load()'s ".tex" auto-append.
void Mortar::ReloadableTexture::Load(const char* filename) {
    *this = filename;
    if (m_pPath && !m_Texture.IsValid()) {
        m_Texture = Mortar::TextureManager::LoadLocalisedTexture(m_pPath);
    }
}
