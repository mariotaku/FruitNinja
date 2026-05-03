// Analysed: 2026-05-03T00:00
#include "SlashModifier.h"
#include "ItemManager.h"
#include "ItemParseUtil.h"
#include "entities/SlashEntity.h"
#include "engine/math/Colour.h"
#include <tinyxml2.h>
#include <cstdio>
#include <cstring>

// TODO: ParseSlashModColourType + ParseSlashPowerMask helper tables not yet ported.

// ParseSlashModColourType — maps XML "type" string to colour-type int.
// TODO: full lookup table from binary @ TBD
static int ParseSlashModColourType(const char* /*type*/) {
    return 0;
}

// ParseSlashPowerMask — maps XML "type" string to power-mask bit.
// TODO: bit-mask table from binary @ TBD
static uint32_t ParseSlashPowerMask(const char* /*type*/) {
    return 0;
}

// Matches SlashModifier::SlashModifier (0x0011f1fc).
SlashModifier::SlashModifier()
    : GameModifier(),
      m_pColours(nullptr), m_NumColours(0),
      m_ColourType(0), m_ColourSpeed(1.0f),
      m_pTexture1(nullptr), m_pTexture2(nullptr),
      m_PowerMask(0), m_Applied(false) {
}

// Matches ~SlashModifier (0x0011f36c / 0x0011f3c4).
SlashModifier::~SlashModifier() {
    delete[] m_pColours;
    delete[] m_pTexture1;
    delete[] m_pTexture2;
}

// Matches SlashModifier::UpdateSpecific (0x0011f288). OR the modifier's
// cached mask bits into the global every frame — PowerUpManager's
// SetDefaults wiped the global to 0 at the top of the update pass, so
// this is what keeps the bits live while the modifier is active.
int SlashModifier::UpdateSpecific(float /*dt*/) {
    SlashEntity::s_ModPowerMask |= m_PowerMask;
    return 0;
}

// Binary @ 0x0011f31c. Two gates: m_pColours != nullptr AND !m_Applied.
// ASM-verified: 2026-05-03 binary @ 0x0011f31c (asm-inspector)
void SlashModifier::ApplyModifier(bool isPurchased, float* extra) {
    GameModifier::ApplyModifier(isPurchased, extra);
    if (m_pColours != nullptr && !m_Applied) {
        m_Applied = true;
        ++ItemManager::EquippedSlashModCount;
        SlashEntity::SetModColours(
            m_pColours, m_NumColours, m_ColourType, m_ColourSpeed,
            m_pTexture1, m_pTexture2,
            false, nullptr, nullptr);
    }
}

// Binary @ 0x0011f2e0. NOTE: binary does NOT call GameModifier::RemoveModifier
// and does NOT clear m_Applied — those are port-introduced bugs to remove.
// ASM-verified: 2026-05-03 binary @ 0x0011f2e0 (asm-inspector)
void SlashModifier::RemoveModifier() {
    if (m_Applied) {
        if (--ItemManager::EquippedSlashModCount <= 0) {
            ItemManager* mgr = ItemManager::GetInstance();
            ItemInfo* def = *reinterpret_cast<ItemInfo**>(mgr);  // first field
            mgr->SetEquippedItem(0, def);
        }
    }
}

// Matches SlashModifier::ParseSpecific (0x0011f464). Reads:
//   speed        -> m_ColourSpeed (default 1.0)
//   type         -> m_ColourType via ParseSlashModColourType
//   particles    -> m_pTexture1 (CloneString)
//   texture      -> m_pTexture2 (new char[0x40], snprintf — note binary
//                  size arg is 4, which truncates — preserved as-is)
//   <slash_power type="..."/> children -> m_PowerMask |= ParseSlashPowerMask
//   <colour>text</colour> children     -> m_pColours[i] = ParseColour
void SlashModifier::ParseSpecific(TiXmlElement* xml) {
    GameModifier::ParseSpecific(xml);

    double speed = 1.0;
    if (xml->QueryDoubleAttribute("speed", &speed) == tinyxml2::XML_SUCCESS)
        m_ColourSpeed = (float)speed;

    const char* type = xml->Attribute("type");
    if (type) m_ColourType = ParseSlashModColourType(type);

    const char* particles = xml->Attribute("particles");
    if (particles) CloneString(&m_pTexture1, particles);

    const char* texture = xml->Attribute("texture");
    if (texture) {
        m_pTexture2 = new char[0x40];
        snprintf(m_pTexture2, 4, "%s.tex", texture);   // binary truncates at 3 chars + NUL
    }

    // <slash_power type="..."/> children
    for (TiXmlElement* sp = xml->FirstChildElement("slash_power"); sp;
         sp = sp->NextSiblingElement("slash_power")) {
        const char* maskAttr = sp->Attribute("type");
        if (maskAttr) m_PowerMask |= ParseSlashPowerMask(maskAttr);
    }

    // <colour>...</colour> children — first pass count, then alloc, then parse
    int colourCount = 0;
    for (TiXmlElement* c = xml->FirstChildElement("colour"); c;
         c = c->NextSiblingElement("colour")) ++colourCount;
    if (colourCount > 0) {
        m_NumColours = colourCount;
        m_pColours = new Colour[colourCount + 2];
        int idx = 0;
        for (TiXmlElement* c = xml->FirstChildElement("colour"); c;
             c = c->NextSiblingElement("colour"), ++idx) {
            ParseColour(&m_pColours[idx], c->GetText());
        }
    } else if (m_pTexture1 || m_pTexture2) {
        // Fallback: 1-element Colour::White array
        m_NumColours = 1;
        m_pColours = new Colour[1];
        m_pColours[0] = Colour(255, 255, 255, 255);
    }
}
