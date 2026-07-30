#ifndef FN_HUD_FRUIT_FACT_COMBO_H
#define FN_HUD_FRUIT_FACT_COMBO_H

// FruitFactCombo -- shared combo helpers extracted from the old v1.5.1 FruitFactControl.
// These are free functions / enum used by FruitFactControl, FruitFactZenPage, and
// GameOverScreen. Originally lived in src/hud/FruitFactControl.{h,cpp}; moved here
// so the faithful v1.6.1 FruitFactControl class can reclaim that filename.

#include "engine/util/SmartPtr.h"
#include "engine/asset/Texture.h"
#include <cstdint>

// COMBO_TYPE -- binary enum at mangled name 10COMBO_TYPE (used by GetComboStarText + GetComboStarTexture).
// 25 values (0..24); 0xFF = no combo (stored as uint8_t m_ComboType, 0xFF sentinel).
// Values derived from kComboStars table and CheckCombo logic.
enum COMBO_TYPE {
    COMBO_3_FRUIT         = 0,
    COMBO_4_FRUIT         = 1,
    COMBO_5_FRUIT         = 2,
    COMBO_6_FRUIT         = 3,
    COMBO_ALL_DIFFERENT   = 4,
    COMBO_7_FRUIT_PLUS    = 5,
    COMBO_ALL_APPLES      = 6,
    COMBO_ALL_ORANGES     = 7,
    COMBO_ALL_PINEAPPLES  = 8,
    COMBO_ALL_WATERMELONS = 9,
    COMBO_ALL_KIWIS       = 10,
    COMBO_ALL_MANGOES     = 11,
    COMBO_ALL_STRAWBERRIES= 12,
    COMBO_ALL_PEARS       = 13,
    COMBO_ALL_BANANAS     = 14,
    COMBO_ALL_LIMES       = 15,
    COMBO_ALL_LEMONS      = 16,
    COMBO_ALL_COCONUTS    = 17,
    COMBO_ALL_PASSIONFRUITS=18,
    COMBO_ALPHABETICAL    = 19,
    COMBO_FULLHOUSE       = 20,
    COMBO_2_PAIR          = 21,
    COMBO_3_OF_A_KIND     = 22,
    COMBO_4_OF_A_KIND     = 23,
    COMBO_5_OF_A_KIND     = 24
};

// v1.6.1 CheckCombo @0x001320b4 (binary is a free function; port namespaces it FruitFact)
// Classifies a run of sliced fruit type indices into a combo category.
// Returns a COMBO_TYPE byte (0..0x18 = 0..24); 0xFF = no combo.
// *outDominantType receives the most-frequent fruit type index.
namespace FruitFact {
    uint8_t CheckCombo(int* fruitTypeArray, int count, int* outDominantType);
} // namespace FruitFact

// GetComboStarTexture  binary: _Z19GetComboStarTexture10COMBO_TYPE @0x00132a94 (v1.6.1)
// Returns the star-burst texture SmartPtr for the given combo type.
Mortar::SmartPtr<Mortar::Texture> GetComboStarTexture(COMBO_TYPE comboType);

// GetComboStarText  binary: _Z16GetComboStarText10COMBO_TYPE @0x001325f8 (v1.6.1)
// Returns the localised string id (for GETSTRING) for the given combo type.
// Returns 0 if comboType > 0x18.
unsigned int GetComboStarText(COMBO_TYPE comboType);

// v1.6.1 GetComboName @0x00132094 (_Z12GetComboName10COMBO_TYPE)
// Returns the ASCII combo-name key string for the given combo type.
// Definition lives in GameOverScreen.cpp:914 (co-located with g_ComboNameTable).
const char* GetComboName(COMBO_TYPE starType);

#endif // FN_HUD_FRUIT_FACT_COMBO_H
