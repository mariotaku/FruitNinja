// Mortar::ReloadableTexture — see header for the full binary-layout spec.

#include "asset/ReloadableTexture.h"
#include "asset/TextureManager.h"
#include "render/Renderer.h"

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

// Port specific: sampling binds go through the Renderer's lazy texture shadow
// (see Renderer.h "state cache" doc).
void Mortar::ReloadableTexture::Set() const {
    if (m_Texture.IsValid()) {
        uint32_t id = (uint32_t)m_Texture->GetTexId();
        if (Renderer* r = Renderer::GetInstance()) {
            r->BindTexture2D(id);
        } else {
            glBindTexture(GL_TEXTURE_2D, (GLuint)id);
        }
    }
}

void Mortar::ReloadableTexture::UnSet() const {
    if (Renderer* r = Renderer::GetInstance()) {
        r->BindTexture2D(0);
    } else {
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}
