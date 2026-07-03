// PurchaseInfo — binary ctor @ 0x0011bdd8, dtor @ 0x00118664.
//
// Ctor: calls ReloadableTexture::ReloadableTexture() on all three texture slots
// (m_Texture, m_InUseTexture, m_GreyTexture), then zero-inits the three int
// fields (m_MaxUses, m_Cost, m_CurrentUses). m_Description and m_DisplayName are
// left uninitialised by the ctor (callers set them via Parse).
//
// Dtor: NOT trivial as of #359 -- each ReloadableTexture subobject now owns a
// heap m_pPath buffer, freed by its own destructor when the three subobjects
// destruct in place. Binary dtor @ 0x00118664 is near-empty per RE (the frees
// happen in the base ReloadableTexture dtors, not in PurchaseInfo's own body).

#include "screens/PurchaseInfo.h"
#include <cstring>
#include <cstdio>

PurchaseInfo::PurchaseInfo()
    : m_MaxUses(0)
    , m_Cost(0)
    , m_Texture()
    , m_InUseTexture()
    , m_GreyTexture()
    , m_CurrentUses(0)
{
    // m_Description and m_DisplayName not zeroed by binary ctor; set externally.
    m_Description[0] = '\0';
    m_DisplayName[0] = '\0';
    m_TextureFilenames[0][0] = '\0';
    m_TextureFilenames[1][0] = '\0';
    m_TextureFilenames[2][0] = '\0';
}

PurchaseInfo::~PurchaseInfo() {
    // Binary dtor @ 0x00118664: no explicit body of its own -- cleanup happens
    // via subobject destruction of the three ReloadableTexture members, each of
    // which now frees an owned m_pPath heap buffer (#359).
}

// Binary @ 0x00118474
void PurchaseInfo::Parse(TiXmlElement* el) {
    char buf[64];
    const char* s;

    el->QueryIntAttribute("games", &m_MaxUses);
    m_CurrentUses = m_MaxUses;
    el->QueryIntAttribute("cost", &m_Cost);

    s = el->Attribute("title");
    if (s && *s) {
        strncpy(m_DisplayName, s, sizeof(m_DisplayName) - 1);
        m_DisplayName[sizeof(m_DisplayName) - 1] = '\0';
    }

    s = el->Attribute("texture");
    if (!s || !*s) s = "arcade_item_01_buy";
    snprintf(buf, sizeof(buf), "%s.tex", s);
    strncpy(m_TextureFilenames[0], buf, sizeof(m_TextureFilenames[0]) - 1);
    m_TextureFilenames[0][sizeof(m_TextureFilenames[0]) - 1] = '\0';

    s = el->Attribute("selectedTexture");
    if (!s || !*s) s = "arcade_item_01_selected";
    snprintf(buf, sizeof(buf), "%s.tex", s);
    strncpy(m_TextureFilenames[1], buf, sizeof(m_TextureFilenames[1]) - 1);
    m_TextureFilenames[1][sizeof(m_TextureFilenames[1]) - 1] = '\0';

    s = el->Attribute("usedTexture");
    if (!s || !*s) s = "arcade_item_01_used";
    snprintf(buf, sizeof(buf), "%s.tex", s);
    strncpy(m_TextureFilenames[2], buf, sizeof(m_TextureFilenames[2]) - 1);
    m_TextureFilenames[2][sizeof(m_TextureFilenames[2]) - 1] = '\0';

    TiXmlElement desc = el->FirstChildElement("description");
    if (desc) {
        const char* t = desc.GetText();
        if (t && *t) {
            strncpy(m_Description, t, sizeof(m_Description) - 1);
            m_Description[sizeof(m_Description) - 1] = '\0';
        }
    }
}

// Binary @ 0x001183d4
void PurchaseInfo::LoadTextures() {
    m_Texture.Load(m_TextureFilenames[0]);
    m_InUseTexture.Load(m_TextureFilenames[1]);
    m_GreyTexture.Load(m_TextureFilenames[2]);
}

void PurchaseInfo::UnloadTextures() {
    // Binary @ 0x00118334: three ReloadableTexture::Unload() calls in sequence.
    // ASM-verified: 2026-05-18 v1.6.1 binary @ 0x00118334 (re-analyst)
    m_Texture.Unload();
    m_InUseTexture.Unload();
    m_GreyTexture.Unload();
}
