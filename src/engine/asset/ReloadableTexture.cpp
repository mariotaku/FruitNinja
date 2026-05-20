// Mortar::ReloadableTexture — 8-byte reloadable texture record.
// Binary ctor inlined in PurchaseInfo ctor @ 0x0011bdd8.
// Unload @ 0x00118334 (three calls, ASM-verified 2026-05-18).

#include "asset/ReloadableTexture.h"
#include "asset/TextureManager.h"
#include "asset/Texture.h"
#include "render/gl_funcs.h"

void Mortar::ReloadableTexture::Load(const char* filename) {
    Mortar::SmartPtr<Mortar::Texture> tex = Mortar::TextureManager::LoadLocalisedTexture(filename);
    if (tex.IsValid()) {
        m_Handle = tex->m_TexId;
    } else {
        m_Handle = 0;
    }
}

void Mortar::ReloadableTexture::Set() const {
    if (m_Handle != 0) {
        glBindTexture(GL_TEXTURE_2D, m_Handle);
    }
}

void Mortar::ReloadableTexture::UnSet() const {
    glBindTexture(GL_TEXTURE_2D, 0);
}
