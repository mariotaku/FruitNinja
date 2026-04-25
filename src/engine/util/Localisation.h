#ifndef FN_LOCALISATION_H
#define FN_LOCALISATION_H

// Analysed: 2026-04-25T14:30
//
// Mortar Engine string table localisation system.
// Loads translations_header.str (key->index map) and
// translations_<lang>.str (per-language string blob) from the Data/
// stringtables directory.  Provides binary-search key lookup.
//
// Binary refs:
//   StringTableUtilLoadStrings         0x0011fb20
//   StringTableUtilLoadStringsTable    0x0011f9dc
//   Mortar::StringTable::GetInfo       0x0018a2cc
//   Mortar::StringTable::GetString     0x0011fec8
//   GETSTRING_STR                      0x0011fb40
//   GETSTRING_CAST_0_STR               0x00109ec0
//
// See docs/engine/localisation.md for full format + algorithm.

#include <cstdint>
#include <cstddef>

// languageFlag enum values (from g_GameData+0x03):
//   0  = english_us (default / fallback)
//   1  = german
//   2  = dutch
//   3  = french
//   4  = spanish
//   5  = italian
//   6  = swedish
//   7  = danish
//   8  = norwegian
//   9  = finnish
//   10 = korean
//   11 = japanese
//   12 = english_uk
//   13 = chinese

// --- StringEntry (12 bytes) ---
// StringEntry[str_idx].str_offset + str_blob_base -> const char* translation
struct StringEntry {
    uint32_t str_offset;       // byte offset into str_blob
    uint32_t strlen_cached;    // strlen of translated string (unused by port)
    uint32_t strlen_cached2;   // duplicate of above (unused by port)
};

// --- HeaderLookup (40 bytes) ---
// One entry per key, sorted ascending by key for binary search.
struct HeaderLookup {
    const char* key_ptr;    // pointer into key_blob (set at load time)
    uint32_t    unknown1;   // observed == keylen
    uint32_t    keylen;     // strlen of key (no null) — used by GetInfo
    uint32_t    unknown2;   // 0x3c05 in all observed entries
    uint32_t    unknown3;
    uint32_t    unknown4;
    uint32_t    unknown5;
    uint32_t    unknown6;
    uint32_t    unknown7;
    uint32_t    str_idx;    // at +0x24: index into StringEntry[]
};

// --- Localisation singleton ---
class Localisation {
public:
    // Load string tables from the given data root (e.g. "Data/").
    // languageFlag selects the language (0 = english_us default).
    // Falls back to english_us if the language file cannot be opened.
    static void Load(const char* dataDir, int languageFlag);

    // Release all heap memory and reset to unloaded state.
    static void Unload();

    // Look up a localisation key.  Returns the translated string, or
    // key itself if not found or not loaded (matches binary pass-through).
    static const char* Get(const char* key);

    // Returns true if Load() has completed successfully.
    static bool IsLoaded();

    // Binary search implementation — mirrors Mortar::StringTable::GetInfo
    static const HeaderLookup* GetInfo(const char* key);

    // Internal state (public so file-scope helper functions in Localisation.cpp
    // can populate them without friendship declarations)
    static bool           s_loaded;
    static HeaderLookup*  s_header_entries;  // heap, count = s_count
    static uint32_t       s_count;
    static char*          s_key_blob;        // heap copy of key strings
    static StringEntry*   s_lang_entries;    // heap, count = s_count
    static char*          s_str_blob;        // heap copy of translated strings
};

#endif // FN_LOCALISATION_H
