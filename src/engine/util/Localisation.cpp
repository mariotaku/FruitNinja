// Analysed: 2026-04-25T14:30
//
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
//   GETSTRING_STR                      0x0011fb40

#include "Localisation.h"
#include "PathCI.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// Static state
bool           Localisation::s_loaded        = false;
HeaderLookup*  Localisation::s_header_entries = nullptr;
uint32_t       Localisation::s_count          = 0;
char*          Localisation::s_key_blob        = nullptr;
StringEntry*   Localisation::s_lang_entries    = nullptr;
char*          Localisation::s_str_blob        = nullptr;

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
// Adds a POSIX case-insensitive fallback (no-op on Windows) so binary-
// faithful Title-Case path components ("StringTables/Translations_*.str")
// resolve against lowercase shipped assets.
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

// LoadHeader — reads translations_header.str, populates s_header_entries + s_key_blob.
// Returns true on success.
// Mirrors Mortar::StringTable::LoadHeader at 0x0018a490.
static bool LoadHeader(const char* path) {
    size_t size = 0;
    char* data = ReadFile(path, &size);
    if (!data) return false;

    if (size < kEntriesStart + 4) { free(data); return false; }

    // Validate magic
    uint32_t magic = 0;
    memcpy(&magic, data, 4);
    if (magic != 1) { free(data); return false; }

    uint32_t count = 0;
    memcpy(&count, data + kCountField, 4);
    if (count == 0) { free(data); return false; }

    uint32_t key_blob_off = kEntriesStart + count * kHeaderEntrySize;
    if (key_blob_off >= size) { free(data); return false; }
    uint32_t key_blob_size = (uint32_t)size - key_blob_off;

    // Allocate and populate header entries
    HeaderLookup* entries = (HeaderLookup*)malloc(count * sizeof(HeaderLookup));
    char* key_blob = (char*)malloc(key_blob_size + 1);
    if (!entries || !key_blob) {
        free(entries); free(key_blob); free(data);
        return false;
    }

    // Copy key blob
    memcpy(key_blob, data + key_blob_off, key_blob_size);
    key_blob[key_blob_size] = '\0';

    // Parse each header entry
    for (uint32_t i = 0; i < count; i++) {
        const char* src = data + kEntriesStart + i * kHeaderEntrySize;
        uint32_t raw[10];
        memcpy(raw, src, 40);

        HeaderLookup* e = &entries[i];
        // raw[0] = key_offset (relative to key_blob_off, NOT to data start)
        // After load, resolve to pointer into key_blob heap buffer
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

    Localisation::s_header_entries = entries;
    Localisation::s_count          = count;
    Localisation::s_key_blob       = key_blob;

    free(data);
    return true;
}

// LoadLanguage — reads translations_<lang>.str, populates s_lang_entries + s_str_blob.
// Returns true on success.
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

    // Copy string blob
    memcpy(str_blob, data + str_blob_off, str_blob_size);
    str_blob[str_blob_size] = '\0';

    // Parse each string entry
    for (uint32_t i = 0; i < count; i++) {
        const char* src = data + kEntriesStart + i * kLangEntrySize;
        uint32_t raw[3];
        memcpy(raw, src, 12);

        StringEntry* e  = &entries[i];
        e->str_offset    = raw[0];
        e->strlen_cached = raw[1];
        e->strlen_cached2 = raw[2];
    }

    Localisation::s_lang_entries = entries;
    Localisation::s_str_blob     = str_blob;

    free(data);
    return true;
}

// ---------------------------------------------------------------------------

void Localisation::Load(const char* dataDir, int languageFlag) {
    Unload();

    // Clamp language flag to valid range
    if (languageFlag < 0 || languageFlag >= kLanguageCount)
        languageFlag = 0;
    const char* lang = kLanguageSuffix[languageFlag];

    // Build file paths
    char hdr_path[512], lang_path[512];
    snprintf(hdr_path,  sizeof(hdr_path),  "%s/stringtables/translations_header.str", dataDir);
    snprintf(lang_path, sizeof(lang_path), "%s/stringtables/translations_%s.str",     dataDir, lang);

    // Load header file (key->index table)
    if (!LoadHeader(hdr_path)) {
        printf("Localisation: failed to load header: %s\n", hdr_path);
        return;
    }

    // Load language body; fall back to english_us on failure
    // (mirrors StringTableUtilLoadStringsTable fallback at 0x0011faa8)
    if (!LoadLanguage(lang_path)) {
        printf("Localisation: failed to load language '%s', falling back to english_us\n", lang);
        snprintf(lang_path, sizeof(lang_path),
                 "%s/stringtables/translations_english_us.str", dataDir);
        if (!LoadLanguage(lang_path)) {
            printf("Localisation: fallback english_us also failed\n");
            return;
        }
    }

    s_loaded = true;
}

void Localisation::Unload() {
    // Mirrors StringTableUtilUnload at 0x0011f9b8
    free(s_header_entries); s_header_entries = nullptr;
    free(s_key_blob);       s_key_blob       = nullptr;
    free(s_lang_entries);   s_lang_entries   = nullptr;
    free(s_str_blob);       s_str_blob       = nullptr;
    s_count  = 0;
    s_loaded = false;
}

// Binary search — mirrors Mortar::StringTable::GetInfo at 0x0018a2cc.
// Compares up to min(entry.keylen, query_len)+1 bytes (includes null terminator
// comparison, which distinguishes prefix keys).
const HeaderLookup* Localisation::GetInfo(const char* key) {
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

// Mirrors Mortar::StringTable::GetString at 0x0011fec8.
const char* Localisation::Get(const char* key) {
    if (!s_loaded || !key) return key;
    const HeaderLookup* info = GetInfo(key);
    if (!info) return key;  // pass-through on miss (matches binary behaviour)
    if (info->str_idx >= s_count) return key;
    const StringEntry* entry = &s_lang_entries[info->str_idx];
    return s_str_blob + entry->str_offset;
}

bool Localisation::IsLoaded() {
    return s_loaded;
}
