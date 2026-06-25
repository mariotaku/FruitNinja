// Localisation -- thin facade over Mortar::StringTable.
// See docs/engine/localisation.md for full format + algorithm documentation.
//
// Binary refs (all delegated to Mortar::StringTable / Mortar::GETSTRING_*):
//   StringTableUtilLoadStrings      0x0011fb20
//   StringTableUtilLoadStringsTable 0x0011f9dc
//   StringTableUtilLoaded           0x0011f940
//   StringTableUtilUnload           0x0011f9b8
//   GETSTRING_CAST_0_STR            0x00109ec0

#include "Localisation.h"
#include "StringTable.h"

// Localisation::Load -- mirrors StringTableUtilLoadStrings @ 0x0011fb20.
// Delegates to Mortar::StringTable::Load which implements the full
// language-switch, header+body load, and english_us fallback.
// ASM-verified: 2026-05-18 v1.6.1 binary @ 0x0011fb20 (re-analyst)
void Localisation::Load(const char* dataDir, int languageFlag) {
    Mortar::StringTable::Load(dataDir, languageFlag);
}

// Localisation::Unload -- mirrors StringTableUtilUnload @ 0x0011f9b8.
void Localisation::Unload() {
    Mortar::StringTable::Unload();
}

// Localisation::Get -- mirrors GETSTRING_CAST_0_STR @ 0x00109ec0.
// Returns the translated string for key, or key itself on miss.
// Note: the binary returns key on miss at the GETSTRING_STR level;
// Mortar::StringTable::GetString returns "STRING NOT FOUND" on miss.
// GETSTRING_CAST_0_STR is used as the public call site -- its binary
// implementation delegates through GETSTRING_STR which returns key on miss.
// ASM-verified: 2026-05-18 v1.6.1 Localisation::Get @ 0x00114b50 (re-analyst)
const char* Localisation::Get(const char* key) {
    return GETSTRING_CAST_0_STR(key);
}

// Localisation::IsLoaded -- mirrors StringTableUtilLoaded @ 0x0011f940.
bool Localisation::IsLoaded() {
    return Mortar::StringTable::IsLoaded();
}
