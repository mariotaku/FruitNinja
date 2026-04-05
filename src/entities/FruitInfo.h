#ifndef FN_FRUIT_INFO_H
#define FN_FRUIT_INFO_H

//
// FRUIT_INFO — per-fruit-type data loaded from Data/xml/fruitlist.xml
// Matches original Fruit::LoadInfo (0x17987c, 519 lines) which parses XML
// into a FRUIT_INFO array (0x330 bytes per entry in original).
//
// Currently only fields needed for rendering are parsed.
// Struct will grow as more gameplay features are implemented.
//

#include <cstdint>

// Matches original FRUIT_INFO (0x330 bytes, only essential fields for now)
struct FruitInfo {
    char name[64];           // XML "name" attr (e.g. "apple", "watermelon")
    float scale;             // XML "scale" attr (e.g. 60, 75) — visual scale input
    float collision;         // XML "collision" attr (e.g. 5)
    int chance;              // XML "chance" attr (e.g. 100)
    uint32_t nameHash;       // StringHash(name) for fast lookup
    // TODO: add colour, factColour, factTexture, singular/plural strings, etc.
};

// Maximum fruit types (matches original array allocation)
static const int FRUIT_INFO_MAX = 16;

// Matches Fruit::LoadInfo (0x17987c) — parses Data/xml/fruitlist.xml
// Called from GameInitialise
void FruitInfo_Load(const char* xmlPath);

// Access loaded data
const FruitInfo* FruitInfo_Get(int type);
int FruitInfo_GetCount();

#endif
