// Analysed: 2026-05-03T00:00
#include "SlashModifier.h"
#include "ItemManager.h"
#include "ItemParseUtil.h"
#include "entities/SlashEntity.h"
#include "engine/math/Colour.h"
#include <cstdio>
#include <cstring>
#include "util/StringHash.h"

// ASM-verified: 2026-05-22 v1.6.1 binary @ 0x0017b4a8 (re-analyst).
// Maps XML "type" string to colour-type enum (NONE/LERP/PER_SLASH/CONTINUOUS).
// Binary uses StringHash + cached table hashes (__cxa_guard); port computes
// StringHash on every call -- semantically identical since StringHash is pure.
int ParseSlashModColourType(const char* s) {
    if (!s || !*s) return 0;
    uint32_t h = StringHash(s);
    if (h == StringHash("LERP"))       return 1;
    if (h == StringHash("PER_SLASH"))  return 2;
    if (h == StringHash("CONTINUOUS")) return 3;
    return 0;  // NONE / unknown
}

// ASM-verified: 2026-05-22 v1.6.1 binary @ 0x0017b3c8 (re-analyst).
// Maps XML "type" string to power-mask bit (1<<index). Six entries; results
// OR into SlashEntity::s_ModPowerMask which gates attract/repel/bomb-suppress/
// fruit-bounce blade behaviours. Prior port stub returned 0 -- those bits
// never lit.
uint32_t ParseSlashPowerMask(const char* s) {
    if (!s || !*s) return 0;
    uint32_t h = StringHash(s);
    static const char* const kNames[6] = {
        "PUSH_FRUIT", "PULL_FRUIT", "PUSH_BOMB", "PULL_BOMB", "BOMB_HIT", "FRUIT_BOUNCE"
    };
    for (int i = 0; i < 6; ++i) {
        if (h == StringHash(kNames[i])) return 1u << i;
    }
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
// ASM-verified: 2026-05-03 v1.6.1 binary @ 0x0011f31c (asm-inspector)
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
// ASM-verified: 2026-05-03 v1.6.1 binary @ 0x0011f2e0 (asm-inspector)
void SlashModifier::RemoveModifier() {
    if (m_Applied) {
        if (--ItemManager::EquippedSlashModCount <= 0) {
            ItemManager* mgr = ItemManager::GetInstance();
            ItemInfo* def = *reinterpret_cast<ItemInfo**>(mgr);  // first field
            mgr->SetEquippedItem(0, def);
        }
    }
}

// ASM-spec v1.6.1 SlashModifier::ParseSpecific @ 0x0011f464:
//   - Binary wraps whole body in if (xml != null) { ... }; port early-returns.
//   - speed -> m_ColourSpeed (default 1.0) via QueryDoubleAttribute
//   - type -> ParseSlashModColourType (unconditional, null-safe)
//   - particles -> CloneString (unconditional, null-safe)
//   - texture -> new char[0x40], snprintf( ,4,"%s.tex", ) (confirmed at 0x0011f4d4)
//   - <slash_power type="..."/> children -> m_PowerMask OR
//   - <colour> children: binary reads m_NumColours directly (no zero reset),
//     increments each iteration, then uses post-loop value for alloc.
//   - alloc (N+2)*4 raw bytes + custom 8B header [4, N]; port uses new[].
// DIFFERS: Colour allocation uses raw operator_new + custom header [type=4, count]
//   (binary @ 0x0011f544); port uses new Colour[m_NumColours+2]. m_pColours points
//   to first Colour in both cases. Downstream (SetModColours) only reads palette data.
void SlashModifier::ParseSpecific(TiXmlElement* xml) {
    if (xml == nullptr) return;

    double speed = 1.0;
    if (xml->QueryDoubleAttribute("speed", &speed) == TIXML_SUCCESS)
        m_ColourSpeed = (float)speed;

    m_ColourType = ParseSlashModColourType(xml->Attribute("type"));

    CloneString(&m_pTexture1, xml->Attribute("particles"));

    const char* texture = xml->Attribute("texture");
    if (texture && texture[0]) {
        m_pTexture2 = new char[0x40];
        snprintf(m_pTexture2, 4, "%s.tex", texture);
    }

    // <slash_power type="..."/> children
    for (TiXmlElement sp = xml->FirstChildElement("slash_power"); sp;
         sp = sp.NextSiblingElement("slash_power")) {
        const char* maskAttr = sp.Attribute("type");
        if (maskAttr) m_PowerMask |= ParseSlashPowerMask(maskAttr);
    }

    // <colour> children: binary reads m_NumColours directly (no zero reset),
    // increments each iteration.
    for (TiXmlElement c = xml->FirstChildElement("colour"); c;
         c = c.NextSiblingElement("colour")) {
        ++m_NumColours;
    }

    if (m_NumColours > 0) {
        // DIFFERS: binary = raw operator_new((N+2)*4) + 8B header [4, N] + per-element
        // Colour::Colour() ctor; port uses new Colour[m_NumColours+2] (default-ctors all).
        m_pColours = new Colour[m_NumColours + 2];
        int idx = 0;
        for (TiXmlElement c = xml->FirstChildElement("colour"); c;
             c = c.NextSiblingElement("colour"), ++idx) {
            ParseColour(&m_pColours[idx], c.GetText());
        }
    } else if (m_pTexture1 || m_pTexture2) {
        // DIFFERS: binary = 12B raw alloc + 8B header [4,1] + 1 Colour; port uses
        // new Colour[3] (2 extra Colour slots, unused downstream).
        m_NumColours = 1;
        m_pColours = new Colour[3];
        m_pColours[0] = Colour(255, 255, 255, 255);
    }
}

// @ 0x0014b018 — deep copy: new(0x40), copy base 0x1c + own 0x1c, clear base+0xc (m_BonusAccum).
GameModifier* SlashModifier::Clone() {
    SlashModifier* c = new SlashModifier();
    *c = *this;
    c->m_BonusAccum = 0.0f;
    return c;
}
