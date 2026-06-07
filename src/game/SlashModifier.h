#ifndef FN_SLASH_MODIFIER_H
#define FN_SLASH_MODIFIER_H

//
// SlashModifier : GameModifier — power-up that changes blade behaviour.
// Binary size 0x40 bytes. Controls blade colour palette, texture, speed
// cycling, and OR's a power-mask into SlashEntity::ModPowerMask each
// frame while active. Those bits gate fruit/bomb attract-repel,
// explosion suppression, and zen-mode mirror-bounce.
//
// Binary addresses:
//   ctor            0x0011f1fc
//   dtor (D0/D1)    0x0011f36c / 0x0011f3c4
//   ResetSpecific   0x0011f274
//   UpdateSpecific  0x0011f288  <- OR's m_PowerMask into g_mask
//   RemoveModifier  0x0011f2e0
//   ApplyModifier   0x0011f31c
//   ParseSpecific   0x0011f464
//
// Bits in SlashEntity::s_ModPowerMask (set by m_PowerMask via OR):
//   0x01  Fruit attract   — pulls fruit toward the blade
//   0x02  Fruit repel     — pushes fruit away
//   0x04  Bomb attract
//   0x08  Bomb repel
//   0x10  Bomb suppress explosion — blade applies force+10 upward, no blast
//   0x20  Zen mirror bounce — Fruit reflects pos+vel at the ±192 X limit
//   0x40  Menu scrolling conflict (managed by ScrollingMenu, not SlashModifier)
//
// TODO: 0x0011f1fc — structure and methods are declared so that
// porting PowerUpManager later can dispatch UpdateSpecific without
// further changes. Method bodies should match the binary's semantics
// once SlashEntity palette + PowerUpManager + ItemManager land.
//

#include "GameModifier.h"
#include <cstdint>

struct Colour;

class SlashModifier : public GameModifier {
public:
    // +0x20: array of blade palette colours (Colour[m_NumColours]).
    // Allocated via new[]; freed in dtor.
    Colour* m_pColours;

    // +0x24: number of Colour entries in m_pColours.
    int m_NumColours;

    // +0x28: colour-type enum (0=default,1,2,3) — selected by XML
    // `colour_type` via ParseSlashModColourType.
    int m_ColourType;

    // +0x2c: palette cycle speed. XML `speed` attr, default 1.0.
    float m_ColourSpeed;

    // +0x30: blade texture name (XML `texture`). Owned string.
    char* m_pTexture1;

    // +0x34: secondary texture name (XML `texture2`). Owned string.
    char* m_pTexture2;

    // +0x38: bitmask OR'd into SlashEntity::s_ModPowerMask each frame
    // while the modifier is active. Accumulated at parse from
    // <power colour_type="..."/> child elements via ParseSlashPowerMask.
    uint32_t m_PowerMask;

    // +0x3c: set on first ApplyModifier call — used to gate the
    // one-shot SlashEntity::SetModColours call.
    bool m_Applied;

    SlashModifier();
    ~SlashModifier() override;

    // 0x0011f288 — OR m_PowerMask into SlashEntity::s_ModPowerMask every
    // tick. PowerUpManager::SetDefaults clears the mask at the top of
    // PowerUpManager::Update, so this function running each frame is
    // what keeps the bits set while the modifier is active.
    int UpdateSpecific(float dt) override;

    // 0x0011f31c — one-shot on activation. Binary calls
    // SlashEntity::SetModColours(...) + increments
    // ItemManager::EquippedSlashModCount.
    // TODO: 0x0011f31c — wire SetModColours + ItemManager counter once
    // the SlashEntity blade-palette + ItemManager land.
    void ApplyModifier(bool isPurchased, float* extra) override;

    // 0x0011f2e0 — decrement equipped-mod counter; if it reaches 0,
    // restore the default blade palette via SetEquippedItem.
    void RemoveModifier() override;

    int GetType() override { return 3; }

    // 0x0011f464 — parse XML <slash ...> element.
    void ParseSpecific(TiXmlElement* xml) override;
};

#endif
