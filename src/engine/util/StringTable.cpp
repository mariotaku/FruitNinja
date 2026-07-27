// Mortar Engine string table loader and lookup.
// See docs/engine/localisation.md for full format documentation.
//
// Binary refs (v1.6.1):
//   StringTableUtilLoadStrings         0x0014cccc
//   StringTableUtilLoadStringsTable    0x0014ca5c
//   Mortar::StringTable::LoadHeader(char*)     0x0022d800
//   Mortar::StringTable::LoadHeader(File&)     0x0022d7b0
//   Mortar::StringTable::LoadLanguage(char*)   0x0022d74c
//   Mortar::StringTable::LoadLanguage(File&)   0x0022d6fc
//   Mortar::StringTable::GetInfo(ulong)        0x14d1a4  (unported overload)
//   Mortar::StringTable::GetInfo(char*)        0x22d630
//   Mortar::StringTable::GetString(ulong)      0x14d1dc
//   Mortar::StringTable::GetString(HeaderLookup*)  0x14d1c0
//   Mortar::StringTable::GetString(char*)      0x14d1f8
//   Mortar::StringTable::Clear                 0x22d6b4
//   Mortar::StringTable::FileHeader::Check     0x22d598
//   GETSTRING                          0x14c9a0
//   GETSTRING_STR                      0x0014ccf8
//   GETSTRING_CAST_0                   0x0011eb9c
//   GETSTRING_CAST_0_STR               0x001195f4

#include "StringTable.h"
#include "asset/File.h"
#include "debug/Logger.h"
#include "game/GameWork.h"
#include "util/Endian.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// --- Constructors / Destructor ---

Mortar::StringTable::StringTable() {
    memset(m_HeaderBuffer, 0, sizeof(m_HeaderBuffer));
    // FileData members default-constructed (zeroed)
}

Mortar::StringTable::~StringTable() {
    // Dtor @ 0x0022d6dc — destroys two FileData members (frees heaps) in reverse.
    free(m_StringEntries.m_pData);
    m_StringEntries.m_pData  = 0;
    m_StringEntries.m_Count  = 0;
    free(m_HeaderLookup.m_pData);
    m_HeaderLookup.m_pData   = 0;
    m_HeaderLookup.m_Count   = 0;
}

// ASM-spec v1.6.1 Mortar::StringTable::Clear @ 0x0022d6b4 (called by StringTableUtilUnloadTable @0x14c9f8).
// Defunct: game_work.m_StringTable is a uint8_t[0x50] placeholder, not a real array.
// In the binary, Clear frees the two FileData allocations and zeroes all fields.
// Port: no-op stub because the placeholder slot never holds live allocations.
void Mortar::StringTable::Clear() {}

// ASM-spec v1.6.1 StringTableUtilInit @0x14c980: empty body.
void StringTableUtilInit() {}

// ASM-spec v1.6.1 StringTableUtilUnloadTable @0x14c9f8
// Binary: Mortar::StringTable::Clear(&game_work.m_StringTable[idx]).
// Port: no-op — game_work.m_StringTable is a placeholder uint8_t[0x50] with no live allocs.
void StringTableUtilUnloadTable(int /*idx*/) {
    // no-op: placeholder slot holds no live allocations; Clear() has nothing to free.
}

// Default instance used by the port's static wrappers
static Mortar::StringTable s_DefaultTable;
// v1.6.1 tables_loaded @0x00314244: set unconditionally by StringTableUtilLoadStrings().
static bool s_tables_loaded = false;

// ASM-spec v1.6.1 StringTableUtilLoadStringsTable @0x0014ca5c: switch on bM_LangId (+0x03).
static const char* const kLanguageSuffix[] = {
    "english_us",          // 0x00 = 0 (default)
    "english_uk",          // 0x01 = 1
    "french",              // 0x02 = 2
    "spanish",             // 0x03 = 3
    "german",              // 0x04 = 4
    "italian",             // 0x05 = 5
    "dutch",               // 0x06 = 6
    "swedish",             // 0x07 = 7
    "danish",              // 0x08 = 8
    "norwegian",           // 0x09 = 9
    "finnish",             // 0x0a = 10
    "korean",              // 0x0b = 11
    "japanese",            // 0x0c = 12
    "chinese",             // 0x0d = 13
    "traditional chinese", // 0x0e = 14
    "latin spanish",       // 0x0f = 15
    "polish",              // 0x10 = 16
    "portuguese (pt)",     // 0x11 = 17
    "portuguese (br)",     // 0x12 = 18
    "russian",             // 0x13 = 19
    "arabic",              // 0x14 = 20
    "fake debug language", // 0x15 = 21
};
static const int kLanguageCount = 22;

// Miss fallback -- binary returns literal "STRING NOT FOUND" on all miss paths.
static const char* const kStringNotFound = "STRING NOT FOUND";

// --- File format constants ---
// (see docs/engine/localisation.md for full layout)
static const uint32_t kHeaderEntrySize  = 40;  // sizeof(HeaderLookup) in file
static const uint32_t kLangEntrySize    = 12;  // sizeof(StringEntry) in file

// --- Instance methods ---

// CheckHeader -- mirrors Mortar::StringTableData::FileHeader::Check @ 0x0022d598.
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

// File& overload -- binary @ 0x0022d7b0. Reads from an already-opened file
// (positioned at offset 0, immediately after Open).
bool Mortar::StringTable::LoadHeader(Mortar::File& file) {

    // Read FileHeader: magic(4) + token[64] + blob_byte_size(4) + count(4) = 76 bytes.
    uint8_t hdr[76];
    if (!file.Read(hdr, 76)) return false;

    uint32_t magic;
    memcpy(&magic, hdr, 4);
#if defined(FN_BIG_ENDIAN)
    magic = Endian::fnByteSwap32(magic);
#endif
    if (!CheckHeader(magic, hdr + 4)) return false;

    uint32_t blob_byte_size, count;
    memcpy(&blob_byte_size, hdr + 0x44, 4);
    memcpy(&count, hdr + 0x48, 4);
#if defined(FN_BIG_ENDIAN)
    blob_byte_size = Endian::fnByteSwap32(blob_byte_size);
    count          = Endian::fnByteSwap32(count);
#endif
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
    size_t final_size = count * sizeof(StringTableData::HeaderLookup) + key_blob_size;
    uint8_t* buf = (uint8_t*)malloc(final_size);
    if (!buf) { free(raw); return false; }

    StringTableData::HeaderLookup* entries = (StringTableData::HeaderLookup*)buf;
    char* key_blob = (char*)(buf + count * sizeof(StringTableData::HeaderLookup));

    // Copy key blob (raw bytes -- no swap, string data is endian-neutral).
    memcpy(key_blob, raw + count * kHeaderEntrySize, key_blob_size);

    // Copy + fixup entries: each file entry is 10 x uint32_t, first is key_offset.
    // Port specific: FN_BIG_ENDIAN byteswaps each on-disk uint32_t (still little-endian
    // per the on-disk format) before use -- the file layout itself is unchanged.
    uint32_t* raw_entries = (uint32_t*)raw;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t* src = raw_entries + i * 10;
#if defined(FN_BIG_ENDIAN)
        uint32_t key_offset = Endian::fnByteSwap32(src[0]);
        entries[i].key_ptr  = key_blob + key_offset;
        entries[i].unknown1 = Endian::fnByteSwap32(src[1]);
        entries[i].keylen   = Endian::fnByteSwap32(src[2]);
        entries[i].unknown2 = Endian::fnByteSwap32(src[3]);
        entries[i].unknown3 = Endian::fnByteSwap32(src[4]);
        entries[i].unknown4 = Endian::fnByteSwap32(src[5]);
        entries[i].unknown5 = Endian::fnByteSwap32(src[6]);
        entries[i].unknown6 = Endian::fnByteSwap32(src[7]);
        entries[i].unknown7 = Endian::fnByteSwap32(src[8]);
        entries[i].str_idx  = Endian::fnByteSwap32(src[9]);
#else
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
#endif
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

// File& overload -- binary @ 0x0022d6fc. Reads from an already-opened file.
bool Mortar::StringTable::LoadLanguage(Mortar::File& file) {

    // Read FileHeader: magic(4) + token[64] + blob_byte_size(4) + count(4) = 76 bytes.
    uint8_t hdr[76];
    if (!file.Read(hdr, 76)) return false;

    uint32_t magic;
    memcpy(&magic, hdr, 4);
#if defined(FN_BIG_ENDIAN)
    magic = Endian::fnByteSwap32(magic);
#endif
    if (!CheckHeader(magic, hdr + 4)) return false;

    uint32_t blob_byte_size, count;
    memcpy(&blob_byte_size, hdr + 0x44, 4);
    memcpy(&count, hdr + 0x48, 4);
#if defined(FN_BIG_ENDIAN)
    blob_byte_size = Endian::fnByteSwap32(blob_byte_size);
    count          = Endian::fnByteSwap32(count);
#endif
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
    // Port specific: FN_BIG_ENDIAN byteswaps each on-disk uint32_t field (str_offset,
    // strlen_cached, strlen_cached2 -- all still little-endian on disk) before use.
    StringTableData::StringEntry* entries = (StringTableData::StringEntry*)alloc;
    for (uint32_t i = 0; i < count; i++) {
#if defined(FN_BIG_ENDIAN)
        entries[i].str_offset     = Endian::fnByteSwap32(entries[i].str_offset);
        entries[i].strlen_cached  = Endian::fnByteSwap32(entries[i].strlen_cached);
        entries[i].strlen_cached2 = Endian::fnByteSwap32(entries[i].strlen_cached2);
#endif
        entries[i].str_offset += count * kLangEntrySize;
    }

    free(m_StringEntries.m_pData);
    m_StringEntries.m_pData = entries;
    m_StringEntries.m_Count = count;
    return true;
}

// GetInfo(char*) -- binary search @ 0x22d630 (v1.6.1).
const Mortar::StringTableData::HeaderLookup* Mortar::StringTable::GetInfo(const char* key) const {
    if (!m_HeaderLookup.m_pData || m_HeaderLookup.m_Count == 0) return 0;
    size_t key_len = strlen(key);
    size_t lo = 0, hi = m_HeaderLookup.m_Count;
    while (lo != hi) {
        size_t mid = lo + (hi - lo) / 2;
        const StringTableData::HeaderLookup* e = &m_HeaderLookup.m_pData[mid];
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

// GetString(id) -- binary @ 0x14d1dc (v1.6.1) ulong-ID overload.
const char* Mortar::StringTable::GetString(unsigned long id) const {
    uint32_t idx = (uint32_t)id;
    if (!m_HeaderLookup.m_pData || idx >= m_HeaderLookup.m_Count) return kStringNotFound;
    const StringTableData::HeaderLookup* e = &m_HeaderLookup.m_pData[idx];
    if (!m_StringEntries.m_pData || e->str_idx >= m_StringEntries.m_Count) return kStringNotFound;
    const StringTableData::StringEntry* entry = &m_StringEntries.m_pData[e->str_idx];
    return (const char*)m_StringEntries.m_pData + entry->str_offset;
}

// GetString(key) -- binary @ 0x14d1f8 (v1.6.1) string-key overload.
const char* Mortar::StringTable::GetString(const char* key) const {
    if (!key) return kStringNotFound;
    const StringTableData::HeaderLookup* info = GetInfo(key);
    if (!info) return kStringNotFound;
    if (!m_StringEntries.m_pData || info->str_idx >= m_StringEntries.m_Count) return kStringNotFound;
    const StringTableData::StringEntry* entry = &m_StringEntries.m_pData[info->str_idx];
    return (const char*)m_StringEntries.m_pData + entry->str_offset;
}

// GetString(pre-resolved) -- binary @ 0x14d1c0.
const char* Mortar::StringTable::GetString(const StringTableData::HeaderLookup* pre) const {
    if (!pre) return kStringNotFound;
    if (!m_StringEntries.m_pData || pre->str_idx >= m_StringEntries.m_Count) return kStringNotFound;
    const StringTableData::StringEntry* entry = &m_StringEntries.m_pData[pre->str_idx];
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

const Mortar::StringTableData::HeaderLookup* Mortar::StringTable::GetInfoS(const char* key) {
    return s_DefaultTable.GetInfo(key);
}

int Mortar::StringTable::LanguageFlagFromName(const char* name) {
    if (!name) return -1;
    // Convert name to lowercase for case-insensitive match.
    char lower[32];
    int i = 0;
    while (name[i] && i < 31) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        lower[i] = c;
        ++i;
    }
    lower[i] = '\0';
    for (int j = 0; j < kLanguageCount; ++j) {
        if (strcmp(lower, kLanguageSuffix[j]) == 0)
            return j;
    }
    return -1;
}

// --- Free wrapper implementations ---

const char* GETSTRING(LocalizedString id, int tableIdx) {
    (void)tableIdx;
    return Mortar::StringTable::GetStringS(id);
}

const char* GETSTRING_STR(const char* key, int tableIdx) {
    (void)tableIdx;
    return Mortar::StringTable::GetStringS(key);
}

const char* GETSTRING_CAST_0(LocalizedString id) {
    return GETSTRING(id, 0);
}

const char* GETSTRING_CAST_0_STR(const char* key) {
    return GETSTRING_STR(key, 0);
}

// ASM-spec v1.6.1 StringTableUtilLoaded @0x0014c984
bool StringTableUtilLoaded() {
    return s_tables_loaded;
}

// ASM-spec v1.6.1 StringTableUtilUnload @0x0014ca24
void StringTableUtilUnload() {
    if (!s_tables_loaded) return;
    StringTableUtilUnloadTable(0);
    s_tables_loaded = false;
}

// ASM-spec v1.6.1 StringTableUtilLoadStringsTable @0x0014ca5c
// game_work.languageFlag indexes kLanguageSuffix[]; slot!=0 is never reached in v1.6.1.
bool StringTableUtilLoadStringsTable(int slot) {
    if (s_tables_loaded)
        StringTableUtilUnloadTable(slot);

    int flag = (int)game_work.languageFlag;
    if (flag >= kLanguageCount) flag = 0;
    const char* lang = kLanguageSuffix[flag];

    const char* base = (slot == 0) ? "translations" : 0;

    char buf[256];
    snprintf(buf, sizeof(buf), "stringtables/%s_header.str", base);
    s_DefaultTable.LoadHeader(buf);

    snprintf(buf, sizeof(buf), "stringtables/%s_%s.str", base, lang);
    bool ok = s_DefaultTable.LoadLanguage(buf);
    if (!ok) {
        snprintf(buf, sizeof(buf), "stringtables/%s_english_us.str", base);
        ok = s_DefaultTable.LoadLanguage(buf);
    }
    return ok;
}

// ASM-spec v1.6.1 StringTableUtilLoadStrings @0x0014cccc
// s_tables_loaded is set UNCONDITIONALLY, even when load fails (binary behaviour).
bool StringTableUtilLoadStrings() {
    bool ok = StringTableUtilLoadStringsTable(0);
    s_tables_loaded = true;
    return ok;
}
