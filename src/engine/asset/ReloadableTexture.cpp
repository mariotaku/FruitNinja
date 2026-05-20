// Mortar::ReloadableTexture — 8-byte reloadable texture record.
// Binary ctor inlined in PurchaseInfo ctor @ 0x0011bdd8.
// Unload @ 0x00118334 (three calls, ASM-verified 2026-05-18).

#include "asset/ReloadableTexture.h"
#include "render/gl_funcs.h"

void Mortar::ReloadableTexture::Set() const {
    if (m_Handle != 0) {
        glBindTexture(GL_TEXTURE_2D, m_Handle);
    }
}

void Mortar::ReloadableTexture::UnSet() const {
    glBindTexture(GL_TEXTURE_2D, 0);
}
