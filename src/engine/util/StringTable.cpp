// Mortar Engine string table loader and lookup.
// See docs/engine/localisation.md for full format documentation.
//
// Binary refs:
//   StringTableUtilLoadStrings         0x0011fb20
//   StringTableUtilLoadStringsTable    0x0011f9dc
//   Mortar::StringTable::LoadHeader    0x0018a490
//   Mortar::StringTable::LoadLanguage  0x0018a41c
//   Mortar::StringTable::GetInfo       0x0018a2cc
//   Mortar::StringTable::GetString     0x0011fec8
//   GETSTRING                          0x0011f958
//   GETSTRING_STR                      0x0011fb40
//   GETSTRING_CAST_0                   0x0010cff0
//   GETSTRING_CAST_0_STR               0x00109ec0

#include "StringTable.h"
#include "PathCI.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// Static state
bool           Mortar::StringTable::s_loaded        = false;
HeaderLookup*  Mortar::StringTable::s_header_entries = nullptr;
uint32_t       Mortar::StringTable::s_count          = 0;
char*          Mortar::StringTable::s_key_blob        = nullptr;
StringEntry*   Mortar::StringTable::s_lang_entries    = nullptr;
char*          Mortar::StringTable::s_str_blob        = nullptr;

// Language suffix table (indexed by languageFlag)
// Binary: switch in StringTableUtilLoadStringsTable at 0x0011fa02
// Default (0 or >=14) -> "english_us"
static const char* const kLanguageSuffix[] = {
    "english_us",  // 0 = default
    "german",      // 1
    "dutch",       // 2
    "french",      // 3
    "spanish",     // 4
    "italian",     // 5
    "swedish",     // 6
    "danish",      // 7
    "norwegian",   // 8
    "finnish",     // 9
    "korean",      // 10
    "japanese",    // 11
    "english_uk",  // 12
    "chinese",     // 13
    "english_us",  // 14 = same as default
};
static const int kLanguageCount = (int)(sizeof(kLanguageSuffix) / sizeof(kLanguageSuffix[0]));

// Miss fallback -- binary returns literal "STRING NOT FOUND" on all miss paths.
static const char* const kStringNotFound = "STRING NOT FOUND";

// --- File format constants ---
// (see docs/engine/localisation.md for full layout)

// translations_header.str layout:
//   [0x00] uint32 magic = 1
//   [0x04] uint8[64] token (64-byte GUID)
//   [0x44] uint32 blob_byte_size
//   [0x48] uint32 count  (number of HeaderLookup entries; 961 in this build)
//   [0x4c] HeaderLookup[count]  each 40 bytes
//   [0x4c + count*40] char[] key_blob  (null-terminated ASCII keys, sorted)

// translations_<lang>.str layout:
//   [0x00] uint32 magic = 1
//   [0x04] uint8[64] token (same GUID)
//   [0x44] uint32 blob_byte_size
//   [0x48] uint32 count
//   [0x4c] StringEntry[count]  each 12 bytes
//   [0x4c + count*12] char[] str_blob  (null-terminated UTF-8 strings)

static const uint32_t kFileHeaderSize   = 68;  // 4 (magic) + 64 (token)
static const uint32_t kHeaderEntrySize  = 40;  // sizeof(HeaderLookup) in file
static const uint32_t kLangEntrySize    = 12;  // sizeof(StringEntry) in file
static const uint32_t kBlobSizeField    = 68;  // file offset of blob_byte_size
static const uint32_t kCountField       = 72;  // file offset of count
static const uint32_t kEntriesStart     = 76;  // file offset of first entry

// Helper: read entire file into heap buffer.  Caller must free().
static char* ReadFile(const char* path, size_t* out_size) {
    FILE* fp = fopen(path, "rb");
    if (!fp) {
        std::string ci = Mortar::ResolvePathCI(path);
        if (!ci.empty()) fp = fopen(ci.c_str(), "rb");
    }
    if (!fp) return nullptr;
    fseek(fp, 0, SEEK_END);
    size_t sz = (size_t)ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char* buf = (char*)malloc(sz + 1);
    if (!buf) { fclose(fp); return nullptr; }
    size_t n = fread(buf, 1, sz, fp);
    fclose(fp);
    buf[n] = '\0';
    if (out_size) *out_size = n;
    return buf;
}

// LoadHeader -- reads translations_header.str, populates s_header_entries + s_key_blob.
// Mirrors Mortar::StringTable::LoadHeader at 0x0018a490.
static bool LoadHeader(const char* path) {
    size_t size = 0;
    char* data = ReadFile(path, &size);
    if (!data) return false;

    if (size < kEntriesStart + 4) { free(data); return false; }

    uint32_t magic = 0;
    memcpy(&magic, data, 4);
    if (magic != 1) { free(data); return false; }

    uint32_t count = 0;
    memcpy(&count, data + kCountField, 4);
    if (count == 0) { free(data); return false; }

    uint32_t key_blob_off = kEntriesStart + count * kHeaderEntrySize;
    if (key_blob_off >= size) { free(data); return false; }
    uint32_t key_blob_size = (uint32_t)size - key_blob_off;

    HeaderLookup* entries = (HeaderLookup*)malloc(count * sizeof(HeaderLookup));
    char* key_blob = (char*)malloc(key_blob_size + 1);
    if (!entries || !key_blob) {
        free(entries); free(key_blob); free(data);
        return false;
    }

    memcpy(key_blob, data + key_blob_off, key_blob_size);
    key_blob[key_blob_size] = '\0';

    for (uint32_t i = 0; i < count; i++) {
        const char* src = data + kEntriesStart + i * kHeaderEntrySize;
        uint32_t raw[10];
        memcpy(raw, src, 40);

        HeaderLookup* e = &entries[i];
        e->key_ptr       = key_blob + raw[0];
        e->unknown1      = raw[1];
        e->keylen        = raw[2];
        e->unknown2      = raw[3];
        e->unknown3      = raw[4];
        e->unknown4      = raw[5];
        e->unknown5      = raw[6];
        e->unknown6      = raw[7];
        e->unknown7      = raw[8];
        e->str_idx       = raw[9];  // at raw[9] = offset +0x24
    }

    Mortar::StringTable::s_header_entries = entries;
    Mortar::StringTable::s_count          = count;
    Mortar::StringTable::s_key_blob       = key_blob;

    free(data);
    return true;
}

// LoadLanguage -- reads translations_<lang>.str, populates s_lang_entries + s_str_blob.
// Mirrors Mortar::StringTable::LoadLanguage at 0x0018a41c.
static bool LoadLanguage(const char* path) {
    size_t size = 0;
    char* data = ReadFile(path, &size);
    if (!data) return false;

    if (size < kEntriesStart + 4) { free(data); return false; }

    uint32_t magic = 0;
    memcpy(&magic, data, 4);
    if (magic != 1) { free(data); return false; }

    uint32_t count = 0;
    memcpy(&count, data + kCountField, 4);
    if (count == 0) { free(data); return false; }

    uint32_t str_blob_off = kEntriesStart + count * kLangEntrySize;
    if (str_blob_off >= size) { free(data); return false; }
    uint32_t str_blob_size = (uint32_t)size - str_blob_off;

    StringEntry* entries = (StringEntry*)malloc(count * sizeof(StringEntry));
    char* str_blob = (char*)malloc(str_blob_size + 1);
    if (!entries || !str_blob) {
        free(entries); free(str_blob); free(data);
        return false;
    }

    memcpy(str_blob, data + str_blob_off, str_blob_size);
    str_blob[str_blob_size] = '\0';

    for (uint32_t i = 0; i < count; i++) {
        const char* src = data + kEntriesStart + i * kLangEntrySize;
        uint32_t raw[3];
        memcpy(raw, src, 12);

        StringEntry* e   = &entries[i];
        e->str_offset     = raw[0];
        e->strlen_cached  = raw[1];
        e->strlen_cached2 = raw[2];
    }

    Mortar::StringTable::s_lang_entries = entries;
    Mortar::StringTable::s_str_blob     = str_blob;

    free(data);
    return true;
}

// ---------------------------------------------------------------------------

void Mortar::StringTable::Load(const char* dataDir, int languageFlag) {
    Unload();

    if (languageFlag < 0 || languageFlag >= kLanguageCount)
        languageFlag = 0;
    const char* lang = kLanguageSuffix[languageFlag];

    char hdr_path[512], lang_path[512];
    snprintf(hdr_path,  sizeof(hdr_path),  "%s/stringtables/translations_header.str", dataDir);
    snprintf(lang_path, sizeof(lang_path), "%s/stringtables/translations_%s.str",     dataDir, lang);

    if (!LoadHeader(hdr_path)) {
        printf("StringTable: failed to load header: %s\n", hdr_path);
        return;
    }

    // Fall back to english_us on failure
    // (mirrors StringTableUtilLoadStringsTable fallback at 0x0011faa8)
    if (!LoadLanguage(lang_path)) {
        printf("StringTable: failed to load language '%s', falling back to english_us\n", lang);
        snprintf(lang_path, sizeof(lang_path),
                 "%s/stringtables/translations_english_us.str", dataDir);
        if (!LoadLanguage(lang_path)) {
            printf("StringTable: fallback english_us also failed\n");
            return;
        }
    }

    s_loaded = true;
}

void Mortar::StringTable::Unload() {
    // Mirrors StringTableUtilUnload at 0x0011f9b8
    free(s_header_entries); s_header_entries = nullptr;
    free(s_key_blob);       s_key_blob       = nullptr;
    free(s_lang_entries);   s_lang_entries   = nullptr;
    free(s_str_blob);       s_str_blob       = nullptr;
    s_count  = 0;
    s_loaded = false;
}

// Binary search -- mirrors Mortar::StringTable::GetInfo at 0x0018a2cc.
const HeaderLookup* Mortar::StringTable::GetInfo(const char* key) {
    if (!s_header_entries || s_count == 0) return nullptr;
    size_t key_len = strlen(key);
    size_t lo = 0, hi = s_count;
    while (lo != hi) {
        size_t mid = lo + (hi - lo) / 2;
        const HeaderLookup* e = &s_header_entries[mid];
        size_t cmp_len = (e->keylen <= key_len) ? e->keylen : key_len;
        int c = memcmp(key, e->key_ptr, cmp_len + 1);
        if (c < 0)
            hi = mid;
        else if (c > 0)
            lo = mid + 1;
        else
            return e;
    }
    return nullptr;
}

// GetString(LocalizedString id) -- binary @ 0x0011fec8 int-ID overload.
// Integer ID is a position in the header table (not the str_idx field).
const char* Mortar::StringTable::GetString(LocalizedString id) {
    if (!s_loaded) return kStringNotFound;
    uint32_t idx = (uint32_t)(int32_t)id;
    if (idx >= s_count) return kStringNotFound;
    const HeaderLookup* e = &s_header_entries[idx];
    if (e->str_idx >= s_count) return kStringNotFound;
    const StringEntry* entry = &s_lang_entries[e->str_idx];
    return s_str_blob + entry->str_offset;
}

// GetString(const char* key) -- binary @ 0x0011fec8 string-key overload.
const char* Mortar::StringTable::GetString(const char* key) {
    if (!s_loaded || !key) return kStringNotFound;
    const HeaderLookup* info = GetInfo(key);
    if (!info) return kStringNotFound;
    if (info->str_idx >= s_count) return kStringNotFound;
    const StringEntry* entry = &s_lang_entries[info->str_idx];
    return s_str_blob + entry->str_offset;
}

// GetString(const HeaderLookup* pre) -- pre-resolved overload.
const char* Mortar::StringTable::GetString(const HeaderLookup* pre) {
    if (!s_loaded || !pre) return kStringNotFound;
    if (pre->str_idx >= s_count) return kStringNotFound;
    const StringEntry* entry = &s_lang_entries[pre->str_idx];
    return s_str_blob + entry->str_offset;
}

bool Mortar::StringTable::IsLoaded() {
    return s_loaded;
}

// ---------------------------------------------------------------------------
// Free wrappers -- binary @ 0x0011f958, 0x0011fb40, 0x0010cff0, 0x00109ec0

// GETSTRING -- binary @ 0x0011f958.
// tableIdx 0 selects the default (only) table. Other values are out-of-range.
const char* Mortar::GETSTRING(LocalizedString id, int tableIdx) {
    (void)tableIdx;  // port has only one table; tableIdx ignored
    return Mortar::StringTable::GetString(id);
}

// GETSTRING_STR -- binary @ 0x0011fb40.
const char* Mortar::GETSTRING_STR(const char* key, int tableIdx) {
    (void)tableIdx;
    return Mortar::StringTable::GetString(key);
}

// GETSTRING_CAST_0 -- binary @ 0x0010cff0, thunk to GETSTRING(id, 0).
const char* Mortar::GETSTRING_CAST_0(LocalizedString id) {
    return Mortar::GETSTRING(id, 0);
}

// GETSTRING_CAST_0_STR -- binary @ 0x00109ec0, thunk to GETSTRING_STR(key, 0).
const char* Mortar::GETSTRING_CAST_0_STR(const char* key) {
    return Mortar::GETSTRING_STR(key, 0);
}
