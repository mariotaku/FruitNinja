// Mortar Engine string table loader and lookup.
// See docs/engine/localisation.md for full format documentation.
//
// Binary refs (v1.6.1):
//   StringTableUtilLoadStrings         0x0011fb20
//   StringTableUtilLoadStringsTable    0x0011f9dc
//   LoadStringsTable                   0x14ca5c
//   Mortar::StringTable::LoadHeader    0x0022d800
//   Mortar::StringTable::LoadLanguage  0x0022d74c
//   Mortar::StringTable::GetInfo       0x14d1a4
//   Mortar::StringTable::GetInfo(char*)        0x22d630
//   Mortar::StringTable::GetString(int)        0x14d1dc
//   Mortar::StringTable::GetString(HeaderLookup*)  0x14d1c0
//   Mortar::StringTable::GetString(char*)      0x14d1f8
//   GETSTRING                          0x14c9a0
//   GETSTRING_STR                      0x0011fb40
//   GETSTRING_CAST_0                   0x0010cff0
//   GETSTRING_CAST_0_STR               0x00109ec0

#include "StringTable.h"
#include "asset/File.h"
#include "debug/Logger.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// --- Constructors / Destructor ---

Mortar::StringTable::StringTable() {
    memset(m_HeaderBuffer, 0, sizeof(m_HeaderBuffer));
    // FileData members default-constructed (zeroed)
}

Mortar::StringTable::~StringTable() {
    // Dtor @ 0x0018a324 — destroys two FileData members (frees heaps) in reverse.
    free(m_StringEntries.m_pData);
    m_StringEntries.m_pData  = 0;
    m_StringEntries.m_Count  = 0;
    free(m_HeaderLookup.m_pData);
    m_HeaderLookup.m_pData   = 0;
    m_HeaderLookup.m_Count   = 0;
}

// Default instance used by the port's static wrappers
static Mortar::StringTable s_DefaultTable;

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
static const uint32_t kHeaderEntrySize  = 40;  // sizeof(HeaderLookup) in file
static const uint32_t kLangEntrySize    = 12;  // sizeof(StringEntry) in file

// --- Instance methods ---

// CheckHeader -- mirrors Mortar::StringTableData::FileHeader::Check @ 0x0018a3b4.
// Validates the 72-byte FileHeader wrapper shared by both .str files.
bool Mortar::StringTable::CheckHeader(uint32_t magic, const uint8_t* file_guid) {
    if (magic != 1) return false;                          // 0x0018a3ba cmp #1
    if (memcmp(file_guid, m_HeaderBuffer, 0x40) == 0)      // 0x0018a3c4 memcmp
        return true;
    // Token differs from m_HeaderBuffer: accept only if m_HeaderBuffer is still
    // all-zero (first / header load), in which case adopt the file's token.
    for (int i = 0; i < 0x40; ++i)                         // 0x0018a3d0 loop
        if (m_HeaderBuffer[i] != 0) return false;
    memcpy(m_HeaderBuffer, file_guid, 0x40);               // 0x0018a3de memcpy
    return true;
}

// LoadHeader -- opens translations_header.str, validates the wrapper, and loads
// HeaderLookup[] into m_HeaderLookup. Binary @ 0x0022d800.
// Uses Mortar::File for I/O: single allocation for entries + key blob,
// then fixup each entry's key_offset to an absolute key_ptr.
//
// char* overload opens the file and delegates to File& overload.
bool Mortar::StringTable::LoadHeader(const char* path) {
    File file(path, 0, 0);
    if (!file.Open()) return false;
    return LoadHeader(file);
}

// File& overload -- binary @ 0x0021d7b0. Reads from an already-opened file
// (positioned at offset 0, immediately after Open).
bool Mortar::StringTable::LoadHeader(Mortar::File& file) {

    // Read FileHeader: magic(4) + token[64] + blob_byte_size(4) + count(4) = 76 bytes.
    uint8_t hdr[76];
    if (!file.Read(hdr, 76)) return false;

    uint32_t magic;
    memcpy(&magic, hdr, 4);
    if (!CheckHeader(magic, hdr + 4)) return false;

    uint32_t blob_byte_size, count;
    memcpy(&blob_byte_size, hdr + 0x44, 4);
    memcpy(&count, hdr + 0x48, 4);
    if (count == 0) return false;

    // Read raw entries + key blob into a temporary buffer.
    // blob_byte_size includes the count field (4 bytes); minus 4 gives the
    // combined size of entries (count * 40 bytes) + key blob.
    uint32_t raw_size = blob_byte_size - 4;
    uint8_t* raw = (uint8_t*)malloc(raw_size);
    if (!raw) return false;
    if (!file.Read(raw, raw_size)) { free(raw); return false; }

    // Single final allocation: HeaderLookup[count] + key blob.
    // Layout: [HeaderLookup entries] [key blob data]
    // key_ptr in each entry points into the key blob tail.
    uint32_t key_blob_size = raw_size - count * kHeaderEntrySize;
    size_t final_size = count * sizeof(HeaderLookup) + key_blob_size;
    uint8_t* buf = (uint8_t*)malloc(final_size);
    if (!buf) { free(raw); return false; }

    HeaderLookup* entries = (HeaderLookup*)buf;
    char* key_blob = (char*)(buf + count * sizeof(HeaderLookup));

    // Copy key blob.
    memcpy(key_blob, raw + count * kHeaderEntrySize, key_blob_size);

    // Copy + fixup entries: each file entry is 10 x uint32_t, first is key_offset.
    uint32_t* raw_entries = (uint32_t*)raw;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t* src = raw_entries + i * 10;
        entries[i].key_ptr  = key_blob + src[0];
        entries[i].unknown1 = src[1];
        entries[i].keylen   = src[2];
        entries[i].unknown2 = src[3];
        entries[i].unknown3 = src[4];
        entries[i].unknown4 = src[5];
        entries[i].unknown5 = src[6];
        entries[i].unknown6 = src[7];
        entries[i].unknown7 = src[8];
        entries[i].str_idx  = src[9];
    }

    free(raw);

    free(m_HeaderLookup.m_pData);
    m_HeaderLookup.m_pData = entries;
    m_HeaderLookup.m_Count = count;
    return true;
}

// LoadLanguage -- opens translations_<lang>.str, validates the wrapper, and
// loads StringEntry[] into m_StringEntries. Binary @ 0x0022d74c.
// Uses Mortar::File for I/O: single allocation for entries + str blob,
// then fixup each entry's str_offset to an alloc-relative offset.
// DIFFERS: original stores absolute pointer in uint32_t (ARM32 pointer
// fits in 4 bytes); port stores alloc-relative offset for x64 safety.
// Equivalent on ARM32 since alloc base is the same as absolute via
// m_StringEntries.m_pData + entry->str_offset.
//
// char* overload opens the file and delegates to File& overload.
bool Mortar::StringTable::LoadLanguage(const char* path) {
    File file(path, 0, 0);
    if (!file.Open()) return false;
    return LoadLanguage(file);
}

// File& overload -- binary @ 0x0021d6fc. Reads from an already-opened file.
bool Mortar::StringTable::LoadLanguage(Mortar::File& file) {

    // Read FileHeader: magic(4) + token[64] + blob_byte_size(4) + count(4) = 76 bytes.
    uint8_t hdr[76];
    if (!file.Read(hdr, 76)) return false;

    uint32_t magic;
    memcpy(&magic, hdr, 4);
    if (!CheckHeader(magic, hdr + 4)) return false;

    uint32_t blob_byte_size, count;
    memcpy(&blob_byte_size, hdr + 0x44, 4);
    memcpy(&count, hdr + 0x48, 4);
    if (count == 0) return false;

    // Single allocation: StringEntry[count] + str blob.
    // StringEntry is all uint32_t fields, so sizeof(StringEntry) == kLangEntrySize == 12
    // on all platforms. The file layout matches directly.
    uint32_t raw_size = blob_byte_size - 4;
    uint8_t* alloc = (uint8_t*)malloc(raw_size);
    if (!alloc) return false;
    if (!file.Read(alloc, raw_size)) { free(alloc); return false; }

    // Fix up str_offset in-place: convert from file-relative (offset within the
    // str blob that follows the entries) to alloc-relative (offset from alloc[0]
    // to the string).  On 32-bit ARM the binary stores an absolute pointer in
    // this uint32_t field; the port uses alloc-relative for x64 safety.
    StringEntry* entries = (StringEntry*)alloc;
    for (uint32_t i = 0; i < count; i++) {
        entries[i].str_offset += count * kLangEntrySize;
    }

    free(m_StringEntries.m_pData);
    m_StringEntries.m_pData = entries;
    m_StringEntries.m_Count = count;
    return true;
}

// GetInfo -- binary search @ 0x14d1a4 (v1.6.1).
const HeaderLookup* Mortar::StringTable::GetInfo(const char* key) const {
    if (!m_HeaderLookup.m_pData || m_HeaderLookup.m_Count == 0) return 0;
    size_t key_len = strlen(key);
    size_t lo = 0, hi = m_HeaderLookup.m_Count;
    while (lo != hi) {
        size_t mid = lo + (hi - lo) / 2;
        const HeaderLookup* e = &m_HeaderLookup.m_pData[mid];
        size_t cmp_len = (e->keylen <= key_len) ? e->keylen : key_len;
        int c = memcmp(key, e->key_ptr, cmp_len + 1);
        if (c < 0)
            hi = mid;
        else if (c > 0)
            lo = mid + 1;
        else
            return e;
    }
    return 0;
}

// GetString(id) -- binary @ 0x14d1dc (v1.6.1) int-ID overload.
const char* Mortar::StringTable::GetString(LocalizedString id) const {
    uint32_t idx = (uint32_t)(int32_t)id;
    if (!m_HeaderLookup.m_pData || idx >= m_HeaderLookup.m_Count) return kStringNotFound;
    const HeaderLookup* e = &m_HeaderLookup.m_pData[idx];
    if (!m_StringEntries.m_pData || e->str_idx >= m_StringEntries.m_Count) return kStringNotFound;
    const StringEntry* entry = &m_StringEntries.m_pData[e->str_idx];
    return (const char*)m_StringEntries.m_pData + entry->str_offset;
}

// GetString(key) -- binary @ 0x14d1f8 (v1.6.1) string-key overload.
const char* Mortar::StringTable::GetString(const char* key) const {
    if (!key) return kStringNotFound;
    const HeaderLookup* info = GetInfo(key);
    if (!info) return kStringNotFound;
    if (!m_StringEntries.m_pData || info->str_idx >= m_StringEntries.m_Count) return kStringNotFound;
    const StringEntry* entry = &m_StringEntries.m_pData[info->str_idx];
    return (const char*)m_StringEntries.m_pData + entry->str_offset;
}

// GetString(pre-resolved)
const char* Mortar::StringTable::GetString(const HeaderLookup* pre) const {
    if (!pre) return kStringNotFound;
    if (!m_StringEntries.m_pData || pre->str_idx >= m_StringEntries.m_Count) return kStringNotFound;
    const StringEntry* entry = &m_StringEntries.m_pData[pre->str_idx];
    return (const char*)m_StringEntries.m_pData + entry->str_offset;
}

// --- Static wrapper API ---

void Mortar::StringTable::Load(const char* dataDir, int languageFlag) {
    (void)dataDir;
    s_DefaultTable.~StringTable();
    new (&s_DefaultTable) StringTable();

    if (languageFlag < 0 || languageFlag >= kLanguageCount)
        languageFlag = 0;
    const char* lang = kLanguageSuffix[languageFlag];

    // Use relative paths — FileSystem_Direct prepends the data root.
    // The caller (Localisation::Load / game init) receives dataDir from
    // Game::data_dir which is also the FileSystem_Direct root.
    char lang_rel[512];
    snprintf(lang_rel, sizeof(lang_rel), "stringtables/translations_%s.str", lang);

    s_DefaultTable.LoadHeader("stringtables/translations_header.str");
    if (!s_DefaultTable.m_HeaderLookup.m_pData) {
        LOG_ERROR("STRINGTABLE/Load", "failed to load header: stringtables/translations_header.str");
        return;
    }

    s_DefaultTable.LoadLanguage(lang_rel);
    if (!s_DefaultTable.m_StringEntries.m_pData) {
        LOG_WARN("STRINGTABLE/Load", "failed to load language '%s', falling back to english_us", lang);
        s_DefaultTable.LoadLanguage("stringtables/translations_english_us.str");
        if (!s_DefaultTable.m_StringEntries.m_pData) {
            LOG_ERROR("STRINGTABLE/Load", "fallback english_us also failed");
            return;
        }
    }
}

void Mortar::StringTable::Unload() {
    s_DefaultTable.~StringTable();
    new (&s_DefaultTable) StringTable();
}

const char* Mortar::StringTable::GetStringS(LocalizedString id) {
    return s_DefaultTable.GetString(id);
}

const char* Mortar::StringTable::GetStringS(const char* key) {
    return s_DefaultTable.GetString(key);
}

bool Mortar::StringTable::IsLoaded() {
    return s_DefaultTable.m_StringEntries.m_pData != 0;
}

const HeaderLookup* Mortar::StringTable::GetInfoS(const char* key) {
    return s_DefaultTable.GetInfo(key);
}

// --- Free wrapper implementations ---

const char* Mortar::GETSTRING(LocalizedString id, int tableIdx) {
    (void)tableIdx;
    return Mortar::StringTable::GetStringS(id);
}

const char* Mortar::GETSTRING_STR(const char* key, int tableIdx) {
    (void)tableIdx;
    return Mortar::StringTable::GetStringS(key);
}

const char* Mortar::GETSTRING_CAST_0(LocalizedString id) {
    return Mortar::GETSTRING(id, 0);
}

const char* Mortar::GETSTRING_CAST_0_STR(const char* key) {
    return Mortar::GETSTRING_STR(key, 0);
}
