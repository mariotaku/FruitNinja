#include "SlashModifier.h"
#include "entities/SlashEntity.h"
#include "math/Colour.h"

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
    // Binary delete[] guards mirror the `new[]` header layout. Port uses
    // plain C++ delete[] on the owned allocations.
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

// Matches SlashModifier::ApplyModifier (0x0011f31c). Binary calls base
// GameModifier::ApplyModifier, then (gated on !m_Applied) calls
// SlashEntity::SetModColours(m_pColours, m_NumColours, m_ColourType,
// m_ColourSpeed, m_pTexture1, m_pTexture2, false, NULL, NULL) and
// increments ItemManager::EquippedSlashModCount.
//
// Port stub: applies the base duration reset. SetModColours +
// ItemManager aren't ported; logging disabled. When the blade palette
// system lands, wire the call here.
void SlashModifier::ApplyModifier(bool isPurchased, float* extra) {
    GameModifier::ApplyModifier(isPurchased, extra);
    if (!m_Applied) {
        m_Applied = true;
        // TODO: SlashEntity::SetModColours(m_pColours, m_NumColours,
        //                                  m_ColourType, m_ColourSpeed,
        //                                  m_pTexture1, m_pTexture2,
        //                                  false, nullptr, nullptr);
        // TODO: ++ItemManager::EquippedSlashModCount
    }
}

// Matches SlashModifier::RemoveModifier (0x0011f2e0). Binary decrements
// ItemManager::EquippedSlashModCount; if it reaches 0 calls
// ItemManager::SetEquippedItem(0, default) to restore the default blade.
void SlashModifier::RemoveModifier() {
    GameModifier::RemoveModifier();
    // TODO: --ItemManager::EquippedSlashModCount; if (count == 0)
    //       ItemManager::SetEquippedItem(0, default);
    m_Applied = false;
}

// Matches SlashModifier::ParseSpecific (0x0011f464). Reads:
//   speed        → m_ColourSpeed (default 1.0)
//   colour_type  → m_ColourType via ParseSlashModColourType
//   texture      → m_pTexture1 (CloneString)
//   texture2     → m_pTexture2 (new char[0x40], snprintf — note binary
//                  size arg is 4, which truncates — preserved as-is)
//   <power colour_type="..."/> children → m_PowerMask |= ParseSlashPowerMask
//   <colour>text</colour> children      → m_pColours[i] = ParseColour
//
// Port stub: not yet wired to the port's XML loader (TinyXML2 via
// PowerUp::Parse). The shape is here so the method is callable once
// PowerUp::Parse is ported.
void SlashModifier::ParseSpecific(TiXmlElement* /*xml*/) {
    // TODO: TinyXML2 port — mirror binary Parse (see header comment).
}
