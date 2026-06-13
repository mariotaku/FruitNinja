#ifndef FN_ENGINE_UTIL_STRINGTABLE_H
#define FN_ENGINE_UTIL_STRINGTABLE_H

// Mortar Engine string table localisation system.
// Loads translations_header.str (key->index map) and
// translations_<lang>.str (per-language string blob) from the Data/
// stringtables directory.  Provides binary-search key lookup.
//
// Binary refs (v1.6.1):
//   StringTableUtilLoadStrings         0x0011fb20
//   StringTableUtilLoadStringsTable    0x0011f9dc
//   Mortar::StringTable::GetInfo       0x14d1a4
//   Mortar::StringTable::GetString(int)        0x14d1dc
//   Mortar::StringTable::GetString(HeaderLookup*)  0x14d1c0
//   Mortar::StringTable::GetString(char*)      0x14d1f8
//   Mortar::StringTable::GetInfo(char*)        0x22d630
//   GETSTRING                          0x14c9a0
//   GETSTRING_STR                      0x0011fb40
//   GETSTRING_CAST_0                   0x0010cff0
//   GETSTRING_CAST_0_STR               0x00109ec0
//   LoadStringsTable                   0x14ca5c
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

// Integer IDs: flat positional index into the sorted HeaderLookup[] array in
// translations_header.str. Re-derived for v1.6.1 by key-name lookup in the
// shipped header (see tmp/remap_lstr.py for the derivation script).
// Tag_ABI_enum_size = small: sized to int32_t (values exceed 0xFFFF).
enum LocalizedString {
    LSTR_BEST_COMBO          = 0xab,  // CODE_BEST_COMBO       "BEST COMBO: %i FRUIT!"
    LSTR_FRUIT_FACT_TITLE    = 0xae,  // CODE_FRUIT_FACT_TITLE "SENSEI'S FRUIT FACT"
    LSTR_REWARDS_TITLE       = 0x15d, // CODE_REWARDS_TITLE    "REWARDS" (FruitFactRewardsPage title)
    // Defunct: key removed in v1.6.1 (CODE_FACT_MODE call sites refactored away)
    LSTR_FACT_MODE           = 0,
    LSTR_BEST                = 0xc8,  // CODE_SCORE_BEST       "BEST:"
    LSTR_SHOP_BACKGROUND     = 0xc9,  // CODE_SHOP_BACKGROUND  "BACKGROUND"
    LSTR_SHOP_BLADE          = 0xca,  // CODE_SHOP_BLADE       "BLADE"
    LSTR_SHOP_FULL_VERSION   = 0xcb,  // CODE_SHOP_FULL_VERSION "FULL VERSION"
    // Defunct: key removed in v1.6.1 (CODE_SHOP_SPECIAL call sites refactored away)
    LSTR_SHOP_SPECIAL        = 0,
    LSTR_DJ_BAMBOO_BLADE_NOT_PLAYED_TODAY = 0xce,  // CODE_DJ_BAMBOO_BLADE_NOT_PLAYED_TODAY
    LSTR_DJ_BAMBOO_BLADE_PLAYED_TODAY     = 0xcf,  // CODE_DJ_BAMBOO_BLADE_PLAYED_TODAY
    LSTR_DJ_DARK_BLADE_UNLOCK_RIGHTWAYUP  = 0xd7,  // CODE_DJ_DARK_BLADE_UNLOCK_RIGHTWAYUP
    LSTR_DJ_DARK_BLADE_UNLOCK_UPSIDEDOWN  = 0xd8,  // CODE_DJ_DARK_BLADE_UNLOCK_UPSIDEDOWN
    LSTR_MENU_TEXTURE_13                  = 0x39d, // CODE_MENU_TEXTURE_13 "SLICE FRUIT TO BEGIN"
};

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
    uint32_t    keylen;     // strlen of key (no null) -- used by GetInfo
    uint32_t    unknown2;   // 0x3c05 in all observed entries
    uint32_t    unknown3;
    uint32_t    unknown4;
    uint32_t    unknown5;
    uint32_t    unknown6;
    uint32_t    unknown7;
    uint32_t    str_idx;    // at +0x24: index into StringEntry[]
};

namespace Mortar {

// --- FileData<T> (8 bytes) ---
// Binary: Mortar::StringTable::FileData<T> {void* m_pData @+0, uint32_t m_Count @+4}.
// Ctor @ 0x0018a4d4 zeroes both words.
template<typename T>
struct StringTableFileData {
    T*       m_pData;   // +0x00
    uint32_t m_Count;   // +0x04

    StringTableFileData() : m_pData(0), m_Count(0) {}
};

// Mortar::StringTable -- binary sizeof == 0x50 (80).
// NON-POLYMORPHIC: no vtable, no inheritance.
// Instances are slots in a static array (stride 0x50, base at +0x5b4 within the
// string-table globals block) indexed by language/table ID.
// StringTableUtilLoadStringsTable @0x0011f9dc computes element address as
// base + index*0x50 + 0x5b4, proving the stride.
//
// Instance layout:
//   +0x00  uint8_t[64]  m_HeaderBuffer   -- 64-byte file-header/identifier buffer
//   +0x40  FileData<StringTableData::HeaderLookup> m_HeaderLookup (8B)
//   +0x48  FileData<StringTableData::StringEntry>  m_StringEntries (8B)
//   Total: 0x50 (80)
//
// Ctors: 0x0018a374 (C1) / 0x0018a394 (C2) — identical bodies.
// Dtor:  0x0018a324 — destroys two FileData members in reverse.
// Binary methods:
//   LoadHeader   @ 0x0018a490
//   LoadLanguage @ 0x0018a41c
//   GetInfo      @ 0x0018a2cc (binary search by key)
//   GetString    @ 0x0011fec8
class StringTable {
public:
    StringTable();
    ~StringTable();

    // Instance methods.
    // Binary @ 0x0018a490 LoadHeader(char*) -> opens File, delegates to
    //   LoadHeader(File&) @ 0x0018a460: File::Read<FileHeader>(0x48 bytes),
    //   FileHeader::Check (magic/GUID), FileData<HeaderLookup>::Load.
    // Binary @ 0x0018a41c LoadLanguage(char*) -> opens File, delegates to
    //   LoadLanguage(File&) @ 0x0018a3ec: same header read + Check, then
    //   FileData<StringEntry>::Load.
    // Returns true on success (binary returns the Check result; Load only runs
    // when Check passed).
    bool LoadHeader(const char* path);
    bool LoadLanguage(const char* path);

    // Mirrors Mortar::StringTableData::FileHeader::Check @ 0x0018a3b4.
    // file_guid points at the 64-byte token field (file offset +4) of the
    // just-read FileHeader. Validates magic==1, then: if the token equals the
    // already-loaded m_HeaderBuffer it passes; if m_HeaderBuffer is still all
    // zero (first load) it copies the token in and passes; otherwise the token
    // mismatches an already-loaded value and it fails. magic is the file's
    // first word.
    bool CheckHeader(uint32_t magic, const uint8_t* file_guid);

    // Binary search -- mirrors Mortar::StringTable::GetInfo at 0x0018a2cc.
    const HeaderLookup* GetInfo(const char* key) const;

    // Binary @ 0x0011fec8 -- instance lookup by integer ID.
    const char* GetString(LocalizedString id) const;
    // Binary @ 0x0011fec8 -- instance lookup by string key.
    const char* GetString(const char* key) const;
    // Pre-resolved overload.
    const char* GetString(const HeaderLookup* pre) const;

    // --- Binary instance fields ---
    uint8_t m_HeaderBuffer[64];                          // +0x00
    StringTableFileData<HeaderLookup> m_HeaderLookup;    // +0x40 (8B)
    StringTableFileData<StringEntry>  m_StringEntries;   // +0x48 (8B)

    // --- Port-side static API wrapper ---
    // The static methods below provide the global single-table API used by
    // game code. They delegate to the static instance s_DefaultTable.
    static void Load(const char* dataDir, int languageFlag);
    static void Unload();
    static const char* GetStringS(LocalizedString id);
    static const char* GetStringS(const char* key);
    static bool IsLoaded();
    static const HeaderLookup* GetInfoS(const char* key);

    // Port-only static state (not in binary; held by the static array globals)
    // These are used by the port's static Load/Unload implementation.
    // Guard with !defined(__bada__) so the cross-build sizeof assert is accurate.
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    static bool    s_loaded;
    static char*   s_key_blob;
    static char*   s_str_blob;
#endif
};

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
static_assert(sizeof(StringTable) == 0x50, "StringTable sizeof mismatch");
#endif

// Free wrapper: binary @ 0x0011f958.
// Looks up string ID in table index tableIdx (0 = default table).
const char* GETSTRING(LocalizedString id, int tableIdx);

// Free wrapper: binary @ 0x0011fb40.
// Looks up string key in table index tableIdx (0 = default table).
const char* GETSTRING_STR(const char* key, int tableIdx);

// Thunk to GETSTRING(id, 0): binary @ 0x0010cff0.
const char* GETSTRING_CAST_0(LocalizedString id);

// Thunk to GETSTRING_STR(key, 0): binary @ 0x00109ec0.
const char* GETSTRING_CAST_0_STR(const char* key);

} // namespace Mortar

#endif // FN_ENGINE_UTIL_STRINGTABLE_H
