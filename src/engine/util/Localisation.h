#ifndef FN_ENGINE_UTIL_LOCALISATION_H
#define FN_ENGINE_UTIL_LOCALISATION_H

// Localisation -- thin facade over Mortar::StringTable.
// Mirrors the binary's StringTableUtil* free functions and provides the
// Localisation::Load / Localisation::Get / Localisation::Unload / Localisation::IsLoaded
// surface described in docs/engine/localisation.md "Port Implementation Spec".
//
// Binary refs:
//   StringTableUtilLoadStrings      0x0014cccc  -> Localisation::Load
//   StringTableUtilLoadStringsTable 0x0014ca5c  -> Localisation::Load (inner)
//   GETSTRING_CAST_0_STR            0x001195f4  -> Localisation::Get
//   StringTableUtilLoaded           0x0014c984  -> Localisation::IsLoaded
//   StringTableUtilUnload           0x0014ca24  -> Localisation::Unload

#include <cstdint>

class Localisation {
public:
    // Load strings from the given data root (e.g. "Data/").
    // languageFlag selects the language (0 = english_us default, 1..21 = others).
    // Falls back to english_us if the language file cannot be opened.
    // Mirrors StringTableUtilLoadStrings @ 0x0014cccc.
    static void Load(const char* dataDir, int languageFlag);

    // Release all loaded data.
    // Mirrors StringTableUtilUnload @ 0x0014ca24.
    static void Unload();

    // Look up key, return translated string or key itself on miss.
    // Mirrors GETSTRING_CAST_0_STR @ 0x001195f4 -> GETSTRING_STR(key, 0).
    static const char* Get(const char* key);

    // Returns true if Load() has been called and succeeded.
    // Mirrors StringTableUtilLoaded @ 0x0014c984.
    static bool IsLoaded();
};

#endif // FN_ENGINE_UTIL_LOCALISATION_H
