// PurchaseInfo — binary ctor @ 0x0011bdd8, dtor @ 0x00118664.
//
// Ctor: calls ReloadableTexture::ReloadableTexture() on all three texture slots
// (m_Texture, m_InUseTexture, m_GreyTexture), then zero-inits the three int
// fields (m_MaxUses, m_Cost, m_CurrentUses). m_Name and m_DisplayName are left
// uninitialised by the ctor (callers set them via Parse).
//
// Dtor: trivial destructor (no heap frees; ReloadableTexture subobjects
// destruct in place). Binary dtor @ 0x00118664 is near-empty per RE.

#include "screens/PurchaseInfo.h"

PurchaseInfo::PurchaseInfo()
    : m_MaxUses(0)
    , m_Cost(0)
    , m_Texture()
    , m_InUseTexture()
    , m_GreyTexture()
    , m_CurrentUses(0)
{
    // m_Name and m_DisplayName not zeroed by binary ctor; set externally.
}

PurchaseInfo::~PurchaseInfo() {
    // Binary dtor @ 0x00118664: trivial — no explicit cleanup beyond
    // subobject destruction of three ReloadableTexture members.
}

void PurchaseInfo::Parse(tinyxml2::XMLElement* /*xml*/) {
    // TODO: implement Parse (binary addr unknown — re-analyst pass needed)
}

void PurchaseInfo::LoadTextures() {
    // TODO: implement LoadTextures (binary addr unknown — re-analyst pass needed)
}

void PurchaseInfo::UnloadTextures() {
    // Binary @ 0x00118334: three ReloadableTexture::Unload() calls in sequence.
    // ASM-verified: 2026-05-18 binary @ 0x00118334 (re-analyst)
    m_Texture.Unload();
    m_InUseTexture.Unload();
    m_GreyTexture.Unload();
}
