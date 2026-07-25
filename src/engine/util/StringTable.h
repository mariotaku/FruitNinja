#ifndef FN_ENGINE_UTIL_STRINGTABLE_H
#define FN_ENGINE_UTIL_STRINGTABLE_H

// Mortar Engine string table localisation system.
// Loads translations_header.str (key->index map) and
// translations_<lang>.str (per-language string blob) from the Data/
// stringtables directory.  Provides binary-search key lookup.
//
// Binary refs (v1.6.1):
//   StringTableUtilLoadStrings         0x0014cccc
//   StringTableUtilLoadStringsTable    0x0014ca5c
//   Mortar::StringTable::GetInfo(ulong)       0x14d1a4  (unported overload)
//   Mortar::StringTable::GetString(ulong)       0x14d1dc
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

namespace Mortar { class File; }

// languageFlag enum values (bM_LangId / g_GameData+0x03):
//   0x00 = english_us (default / fallback)
//   0x01 = english_uk
//   0x02 = french
//   0x03 = spanish
//   0x04 = german
//   0x05 = italian
//   0x06 = dutch
//   0x07 = swedish
//   0x08 = danish
//   0x09 = norwegian
//   0x0a = finnish
//   0x0b = korean
//   0x0c = japanese
//   0x0d = chinese
//   0x0e = traditional chinese
//   0x0f = latin spanish
//   0x10 = polish
//   0x11 = portuguese (pt)
//   0x12 = portuguese (br)
//   0x13 = russian
//   0x14 = arabic
//   0x15 = fake debug language

// Integer IDs: flat positional index into the sorted HeaderLookup[] array in
// translations_header.str. Re-derived for v1.6.1 by key-name lookup in the
// shipped header (see tmp/remap_lstr.py for the derivation script).
// Tag_ABI_enum_size = small: sized to int32_t (values exceed 0xFFFF).
enum LocalizedString {
    LSTR_BEST_COMBO          = 0xab,  // CODE_BEST_COMBO       "BEST COMBO: %i FRUIT!"
    LSTR_FRUIT_FACT_TITLE    = 0xae,  // CODE_FRUIT_FACT_TITLE "SENSEI'S FRUIT FACT"
    LSTR_ZEN_NO_COMBO_LINE1  = 0xc4,  // FruitFactZenPage no-combo branch, first message line (binary @ 0x00180ebc LSTR 0xC4)
    LSTR_REWARDS_TITLE       = 0x15d, // CODE_REWARDS_TITLE    "REWARDS" (FruitFactRewardsPage title)
    LSTR_ZEN_NO_COMBO_BODY   = 0x2ef, // FruitFactZenPage no-combo branch, body text (binary @ 0x00180ec8 LSTR 0x2EF)
    // Defunct: key removed in v1.6.1 (CODE_FACT_MODE call sites refactored away)
    LSTR_FACT_MODE           = 0,
    LSTR_BEST                = 0xc8,  // CODE_SCORE_BEST       "BEST:"
    LSTR_SHOP_BACKGROUND     = 0xc9,  // CODE_SHOP_BACKGROUND  "BACKGROUND"
    LSTR_SHOP_BLADE          = 0xca,  // CODE_SHOP_BLADE       "BLADE"
    LSTR_SHOP_FULL_VERSION   = 0xcb,  // CODE_SHOP_FULL_VERSION "FULL VERSION"
    LSTR_SHOP_SPECIAL        = 0x12F, // CODE_SHOP_SPECIAL (DojoScreen/ShopScreen; v1.6.1 binary index 0x12F)
    LSTR_DJ_BAMBOO_BLADE_NOT_PLAYED_TODAY = 0xce,  // CODE_DJ_BAMBOO_BLADE_NOT_PLAYED_TODAY
    LSTR_DJ_BAMBOO_BLADE_PLAYED_TODAY     = 0xcf,  // CODE_DJ_BAMBOO_BLADE_PLAYED_TODAY
    LSTR_DJ_DARK_BLADE_UNLOCK_RIGHTWAYUP  = 0xd7,  // CODE_DJ_DARK_BLADE_UNLOCK_RIGHTWAYUP
    LSTR_DJ_DARK_BLADE_UNLOCK_UPSIDEDOWN  = 0xd8,  // CODE_DJ_DARK_BLADE_UNLOCK_UPSIDEDOWN
    LSTR_DOJO_TITLE                       = 0x397, // DOJO ring label -- MainScreen ring (CreateButtons @0x001961f8)
    LSTR_NEW_GAME                         = 0x398, // "NEW GAME" ring label -- MainScreen ring (CreateButtons @0x001961f8)
    LSTR_MORE_GAMES                       = 0x39c, // "MORE GAMES" ring label -- MainScreen ring (CreateButtons @0x001961f8)
    LSTR_MENU_TEXTURE_13                  = 0x39d, // CODE_MENU_TEXTURE_13 "SLICE FRUIT TO BEGIN"
    LSTR_BONUS_PAGE_TITLE                 = 0x412, // T_1035 @ 0x00173d84 -- page title for FruitFactBonusFactPage (GETSTRING(0x412,0))
    LSTR_LEADERBOARD_GLOBAL               = 0x7b,  // FruitFactLeaderboard title (global/top scores); binary GETSTRING(0x7b,0) @ 0x00176980
    LSTR_LEADERBOARD_FRIENDS              = 0x363, // FruitFactLeaderboard title (friends scores);  binary GETSTRING(0x363,0) @ 0x00176980

    // AboutScreen v1.6.1 string IDs (AboutScreen ctor @0x0015b764)
    LSTR_ABOUT_HEADING   = 0x349, // id 841  -- AboutScreen heading text (also MarqueeText heading label)
    LSTR_ABOUT_MARQUEE_LEAD0 = 0x347, // id 839  -- marquee colour-leader line 0 (CreateCreditsMarquee @0x0015ac0c)
    LSTR_ABOUT_MARQUEE_LEAD1 = 0x348, // id 840  -- marquee colour-leader line 1 (CreateCreditsMarquee @0x0015ac0c)
    LSTR_ABOUT_MARQUEE_LEAD2 = 0x34a, // id 842  -- marquee colour-leader line 2 (CreateCreditsMarquee @0x0015ac0c)
    LSTR_ABOUT_CREDIT0   = 0x34b, // id 843  -- credit line 0
    LSTR_ABOUT_CREDIT1   = 0x34c, // id 844  -- credit line 1
    LSTR_ABOUT_CREDIT2   = 0x34d, // id 845  -- credit line 2
    LSTR_ABOUT_CREDIT3   = 0x34e, // id 846  -- credit line 3
    LSTR_ABOUT_CREDIT4   = 0x34f, // id 847  -- credit line 4
    LSTR_ABOUT_CREDIT5   = 0x350, // id 848  -- credit line 5
    LSTR_ABOUT_TITLE     = 0x3c3, // id 963  -- AboutScreen title text

    // ScoreControl v1.6.1 string IDs (ScoreControl ctor @0x001ad5fc)
    LSTR_SCORE           = 0x323, // "SCORE" -- ScoreControl m_pScoreBox label

    // DojoScreen v1.6.1 string IDs (DojoScreen::CreateButtons @0x0016ad9c / ctor @0x0016bad8)
    LSTR_DJ_BACK_BUTTON  = 0x352, // Back/Play ring label (CreateButtons @0x0016ad9c)
    LSTR_DJ_SHOP_BUTTON  = 0x3c2, // Shop ring label (CreateButtons @0x0016ad9c)
    LSTR_SOCIAL_FACEBOOK = 0x11e, // Facebook social share button label (DojoScreen ctor @0x0016bad8)
    LSTR_SOCIAL_TWITTER  = 0x11f, // Twitter social share button label (DojoScreen ctor @0x0016bad8)

    // GameModeScreen v1.6.1 string IDs (GameModeScreen::CreateControls @0x001819bc)
    LSTR_GM_ARCADE        = 0x379, // Arcade mode ring label (街机模式)
    LSTR_GM_CLASSIC       = 0x37a, // Classic mode ring label (经典模式)
    LSTR_GM_ZEN           = 0x37b, // Zen mode ring label (禅模式)
    LSTR_GM_ONLINE        = 0x3ca, // "ONLINE" (MENU_TEXTURE_58) -- VS ring label, connected state
    // MP-revival: no LSTR key is literally "CONNECT TO PLAY ONLINE"'s own id (that's
    // 0x2f4, too long for a ring label); GAME_TEXTURE_115 is the standalone "CONNECT"
    // in the same cluster (113 CONNECTING / 114 CONNECT TO PLAY ONLINE / 115 CONNECT),
    // confirmed by decoding translations_header.str + translations_english_us.str.
    LSTR_GM_CONNECT       = 0x2f5, // "CONNECT" (GAME_TEXTURE_115) -- VS ring label, disconnected state

    // PauseScreen v1.6.1 string IDs (PauseScreen ctor @0x001a7204 / Update @0x001a5ebc)
    LSTR_PAUSED          = 0x3c8, // "PAUSED" -- m_PausedText label (ctor @0x001a7204)
    LSTR_QUIT            = 0x35f, // quit label for BSButton (Update @0x001a5ebc GETSTRING(0x35f,0))

    // IngamePopup v1.6.1 string IDs (IngamePopup ctor @0x0016dbac)
    LSTR_GAME_TEXTURE_02 = 0x2dc, // "NEW BEST!" -- type 0x0F NEW BEST SCORE banner
    LSTR_MENU_TEXTURE_09 = 0x399, // "NEW"       -- type 0x10 shop new-item badge
    LSTR_MENU_TEXTURE_53 = 0x3c5, // "SELECTED"  -- type 0x11 shop selected-item badge
};

namespace Mortar {

// --- StringTableData (nesting wrapper matching binary layout) ---
// Binary GetString mangles ...NS_15StringTableData12HeaderLookupE, proving
// StringEntry / HeaderLookup are nested inside Mortar::StringTableData
// rather than free structs.
struct StringTableData {
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
// Ctors: 0x0022d604 (C1/C2) — identical bodies.
// Dtor:  0x0022d6dc — destroys two FileData members in reverse.
// Binary methods:
//   LoadHeader(char*)          @ 0x0022d800
//   LoadHeader(File&)          @ 0x0022d7b0
//   LoadLanguage(char*)        @ 0x0022d74c
//   LoadLanguage(File&)        @ 0x0022d6fc
//   GetInfo(char*)             @ 0x0022d630 (binary search by key)
//   GetInfo(ulong)             @ 0x0014d1a4 (unported overload)
//   GetString(HeaderLookup*)   @ 0x0014d1c0
//   GetString(ulong)           @ 0x0014d1dc
//   GetString(char*)           @ 0x0014d1f8
//   Clear                      @ 0x0022d6b4
//   FileHeader::Check          @ 0x0022d598
//   FileData<HeaderLookup>::Load @ 0x0022d95c
//   FileData<StringEntry>::Load  @ 0x0022d8cc
class StringTable {
public:
    // --- FileData<T> (8 bytes) ---
    // Binary: Mortar::StringTable::FileData<T> {uint32_t m_Count @+0, T* m_pData @+4}.
    // TODO: re-verify v1.6.1 addr -- ctor cited as 0x0018a4d4 predates this pass,
    // not in the restamp list; zeroes both words.
    template<typename T>
    struct FileData {
        uint32_t m_Count;   // +0x00
        T*       m_pData;   // +0x04

        FileData() : m_Count(0), m_pData(0) {}
    };

    StringTable();
    ~StringTable();

    // Instance methods.
    // Binary @ 0x0022d800 LoadHeader(char*) — File(path,0,0), Open, Read 76-byte
    //   header, CheckHeader (magic/GUID), single allocation for entries + blob,
    //   fixup key_ptr absolute pointers in-place.
    // Binary @ 0x0022d74c LoadLanguage(char*) — same File pattern, single
    //   allocation, fixup str_offset to absolute pointer in-place.
    // Returns true on success.
    //
    // Binary also has File& overloads:
    //   LoadHeader(Mortar::File&) @ 0x0022d7b0
    //   LoadLanguage(Mortar::File&) @ 0x0022d6fc
    bool LoadHeader(const char* path);
    bool LoadHeader(Mortar::File& file);
    bool LoadLanguage(const char* path);
    bool LoadLanguage(Mortar::File& file);

    // Mirrors Mortar::StringTableData::FileHeader::Check @ 0x0022d598.
    // file_guid points at the 64-byte token field (file offset +4) of the
    // just-read FileHeader. Validates magic==1, then: if the token equals the
    // already-loaded m_HeaderBuffer it passes; if m_HeaderBuffer is still all
    // zero (first load) it copies the token in and passes; otherwise the token
    // mismatches an already-loaded value and it fails. magic is the file's
    // first word.
    bool CheckHeader(uint32_t magic, const uint8_t* file_guid);

    // Binary search -- mirrors Mortar::StringTable::GetInfo(char*) at 0x0022d630.
    const StringTableData::HeaderLookup* GetInfo(const char* key) const;

    // Binary @ 0x0014d1dc -- instance lookup by integer ID.
    const char* GetString(unsigned long id) const;
    // Binary @ 0x0014d1f8 -- instance lookup by string key.
    const char* GetString(const char* key) const;
    // Pre-resolved overload. Binary @ 0x0014d1c0.
    const char* GetString(const StringTableData::HeaderLookup* pre) const;

    // --- Binary instance fields ---
    uint8_t m_HeaderBuffer[64];                          // +0x00
    FileData<StringTableData::HeaderLookup> m_HeaderLookup;    // +0x40 (8B)
    FileData<StringTableData::StringEntry>  m_StringEntries;   // +0x48 (8B)

    // --- Port-side static API wrapper ---
    // The static methods below provide the global single-table API used by
    // game code. They delegate to the static instance s_DefaultTable.
    static void Load(const char* dataDir, int languageFlag);
    static void Unload();
    static const char* GetStringS(LocalizedString id);
    static const char* GetStringS(const char* key);
    static bool IsLoaded();
    static const StringTableData::HeaderLookup* GetInfoS(const char* key);

    // Returns the language flag (0..21) for a language name like "french",
    // "korean", "english_uk", etc. Case-insensitive. Returns -1 on no match.
    static int LanguageFlagFromName(const char* name);

    // ASM-spec v1.6.1 Mortar::StringTable::Clear @ 0x0022d6b4 (called by StringTableUtilUnloadTable @0x14c9f8).
    // Clears one string table slot. Binary frees FileData allocations + zeroes fields.
    // Port: no-op stub; game_work.m_StringTable is a placeholder with no live allocs.
    void Clear();

};

#if defined(__bada__)
static_assert(sizeof(StringTable) == 0x50, "StringTable sizeof mismatch");
#endif

} // namespace Mortar

// v1.6.1 StringTableUtilInit @0x14c980: empty no-op (binary returns immediately).
void StringTableUtilInit();

// v1.6.1 StringTableUtilUnloadTable @0x14c9f8: clears one string table slot by index.
void StringTableUtilUnloadTable(int idx);

// ASM-spec v1.6.1 StringTableUtilLoaded @0x0014c984: returns the tables_loaded flag.
// NOTE: set unconditionally by StringTableUtilLoadStrings(); NOT the same as IsLoaded().
bool StringTableUtilLoaded();

// ASM-spec v1.6.1 StringTableUtilUnload @0x0014ca24: guard + unload slot 0 + clear flag.
void StringTableUtilUnload();

// ASM-spec v1.6.1 StringTableUtilLoadStringsTable @0x0014ca5c: reads game_work.languageFlag,
// builds paths "stringtables/translations_{header,<lang>}.str", loads slot.
// Only slot==0 is used in v1.6.1; slot>0 produces NULL base (undefined in binary,
// never reached).
bool StringTableUtilLoadStringsTable(int slot);

// ASM-spec v1.6.1 StringTableUtilLoadStrings @0x0014cccc: loads slot 0 for current language,
// sets tables_loaded=1 UNCONDITIONALLY (even on failure).
bool StringTableUtilLoadStrings();

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

#endif // FN_ENGINE_UTIL_STRINGTABLE_H
