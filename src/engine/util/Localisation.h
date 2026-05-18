#ifndef FN_ENGINE_UTIL_LOCALISATION_H
#define FN_ENGINE_UTIL_LOCALISATION_H

// Localisation -- thin facade over Mortar::StringTable.
// Mirrors the binary's StringTableUtil* free functions and provides the
// Localisation::Load / Localisation::Get / Localisation::Unload / Localisation::IsLoaded
// surface described in docs/engine/localisation.md "Port Implementation Spec".
//
// Binary refs:
//   StringTableUtilLoadStrings     0x0011fb20  -> Localisation::Load
//   StringTableUtilLoadStringsTable 0x0011f9dc  -> Localisation::Load (inner)
//   GETSTRING_CAST_0_STR           0x00109ec0  -> Localisation::Get
//   StringTableUtilLoaded          0x0011f940  -> Localisation::IsLoaded
//   StringTableUtilUnload          0x0011f9b8  -> Localisation::Unload

#include <cstdint>

class Localisation {
public:
    // Load strings from the given data root (e.g. "Data/").
    // languageFlag selects the language (0 = english_us default, 1..13 = others).
    // Falls back to english_us if the language file cannot be opened.
    // Mirrors StringTableUtilLoadStrings @ 0x0011fb20.
    static void Load(const char* dataDir, int languageFlag);

    // Release all loaded data.
    // Mirrors StringTableUtilUnload @ 0x0011f9b8.
    static void Unload();

    // Look up key, return translated string or key itself on miss.
    // Mirrors GETSTRING_CAST_0_STR @ 0x00109ec0 -> GETSTRING_STR(key, 0).
    static const char* Get(const char* key);

    // Returns true if Load() has been called and succeeded.
    // Mirrors StringTableUtilLoaded @ 0x0011f940.
    static bool IsLoaded();
};

#endif // FN_ENGINE_UTIL_LOCALISATION_H
