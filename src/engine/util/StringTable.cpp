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
#include "debug/Logger.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

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

// --- Port-only static state ---

#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
bool   Mortar::StringTable::s_loaded   = false;
char*  Mortar::StringTable::s_key_blob = 0;
char*  Mortar::StringTable::s_str_blob = 0;
#endif

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
static const uint32_t kCountField       = 72;  // file offset of count
static const uint32_t kEntriesStart     = 76;  // file offset of first entry

// Helper: read entire file into heap buffer. Caller must free().
static char* ReadFile(const char* path, size_t* out_size) {
    FILE* fp = fopen(path, "rb");
    if (!fp) {
        std::string ci = Mortar::ResolvePathCI(path);
        if (!ci.empty()) fp = fopen(ci.c_str(), "rb");
    }
    if (!fp) return 0;
    fseek(fp, 0, SEEK_END);
    size_t sz = (size_t)ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char* buf = (char*)malloc(sz + 1);
    if (!buf) { fclose(fp); return 0; }
    size_t n = fread(buf, 1, sz, fp);
    fclose(fp);
    buf[n] = '\0';
    if (out_size) *out_size = n;
    return buf;
}

// --- Instance methods ---

// LoadHeader -- reads translations_header.str into m_HeaderBuffer/m_HeaderLookup.
// TODO: 0x0018a490 -- full instance method: reads 64-byte GUID into m_HeaderBuffer,
//   then populates m_HeaderLookup.m_pData / m_HeaderLookup.m_Count from header entries.
void Mortar::StringTable::LoadHeader(const char* path) {
    size_t size = 0;
    char* data = ReadFile(path, &size);
    if (!data) return;

    if (size < kEntriesStart + 4) { free(data); return; }

    uint32_t magic = 0;
    memcpy(&magic, data, 4);
    if (magic != 1) { free(data); return; }

    // Copy 64-byte GUID header into m_HeaderBuffer (offset +0x04 in file)
    size_t copyLen = size > 68 ? 64 : (size > 4 ? size - 4 : 0);
    if (copyLen > 64) copyLen = 64;
    memcpy(m_HeaderBuffer, data + 4, copyLen);

    uint32_t count = 0;
    memcpy(&count, data + kCountField, 4);
    if (count == 0) { free(data); return; }

    uint32_t key_blob_off = kEntriesStart + count * kHeaderEntrySize;
    if (key_blob_off >= (uint32_t)size) { free(data); return; }
    uint32_t key_blob_size = (uint32_t)size - key_blob_off;

    HeaderLookup* entries = (HeaderLookup*)malloc(count * sizeof(HeaderLookup));
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    char* key_blob = (char*)malloc(key_blob_size + 1);
    if (!entries || !key_blob) {
        free(entries); free(key_blob); free(data); return;
    }
    memcpy(key_blob, data + key_blob_off, key_blob_size);
    key_blob[key_blob_size] = '\0';
    // Store key_blob in the port-only static (not in instance; binary stores ptr into mmap)
    free(s_key_blob);
    s_key_blob = key_blob;
#else
    if (!entries) { free(data); return; }
    // On cross-build, key_blob is external; port-only s_key_blob not available.
    // TODO: 0x0018a490 -- binary embeds key_blob ptr within the file-mapped block.
    char* key_blob = (char*)(data + key_blob_off); // temporary; freed below
#endif

    for (uint32_t i = 0; i < count; i++) {
        const char* src = data + kEntriesStart + i * kHeaderEntrySize;
        uint32_t raw[10];
        memcpy(raw, src, 40);

        HeaderLookup* e = &entries[i];
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
        e->key_ptr   = s_key_blob + raw[0];
#else
        e->key_ptr   = key_blob + raw[0];
#endif
        e->unknown1  = raw[1];
        e->keylen    = raw[2];
        e->unknown2  = raw[3];
        e->unknown3  = raw[4];
        e->unknown4  = raw[5];
        e->unknown5  = raw[6];
        e->unknown6  = raw[7];
        e->unknown7  = raw[8];
        e->str_idx   = raw[9];
    }

    free(m_HeaderLookup.m_pData);
    m_HeaderLookup.m_pData  = entries;
    m_HeaderLookup.m_Count  = count;

    free(data);
}

// LoadLanguage -- reads translations_<lang>.str into m_StringEntries.
// TODO: 0x0018a41c -- full instance method: populates m_StringEntries.m_pData / m_Count.
void Mortar::StringTable::LoadLanguage(const char* path) {
    size_t size = 0;
    char* data = ReadFile(path, &size);
    if (!data) return;

    if (size < kEntriesStart + 4) { free(data); return; }

    uint32_t magic = 0;
    memcpy(&magic, data, 4);
    if (magic != 1) { free(data); return; }

    uint32_t count = 0;
    memcpy(&count, data + kCountField, 4);
    if (count == 0) { free(data); return; }

    uint32_t str_blob_off = kEntriesStart + count * kLangEntrySize;
    if (str_blob_off >= (uint32_t)size) { free(data); return; }
    uint32_t str_blob_size = (uint32_t)size - str_blob_off;

    StringEntry* entries = (StringEntry*)malloc(count * sizeof(StringEntry));
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    char* str_blob = (char*)malloc(str_blob_size + 1);
    if (!entries || !str_blob) {
        free(entries); free(str_blob); free(data); return;
    }
    memcpy(str_blob, data + str_blob_off, str_blob_size);
    str_blob[str_blob_size] = '\0';
    free(s_str_blob);
    s_str_blob = str_blob;
#else
    if (!entries) { free(data); return; }
#endif

    for (uint32_t i = 0; i < count; i++) {
        const char* src = data + kEntriesStart + i * kLangEntrySize;
        uint32_t raw[3];
        memcpy(raw, src, 12);
        StringEntry* e   = &entries[i];
        e->str_offset     = raw[0];
        e->strlen_cached  = raw[1];
        e->strlen_cached2 = raw[2];
    }

    free(m_StringEntries.m_pData);
    m_StringEntries.m_pData  = entries;
    m_StringEntries.m_Count  = count;

    free(data);
}

// GetInfo -- binary search @ 0x0018a2cc.
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

// GetString(id) -- binary @ 0x0011fec8 int-ID overload.
const char* Mortar::StringTable::GetString(LocalizedString id) const {
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    if (!s_loaded || !s_str_blob) return kStringNotFound;
    uint32_t idx = (uint32_t)(int32_t)id;
    if (!m_HeaderLookup.m_pData || idx >= m_HeaderLookup.m_Count) return kStringNotFound;
    const HeaderLookup* e = &m_HeaderLookup.m_pData[idx];
    if (!m_StringEntries.m_pData || e->str_idx >= m_StringEntries.m_Count) return kStringNotFound;
    const StringEntry* entry = &m_StringEntries.m_pData[e->str_idx];
    return s_str_blob + entry->str_offset;
#else
    return kStringNotFound;
#endif
}

// GetString(key) -- binary @ 0x0011fec8 string-key overload.
const char* Mortar::StringTable::GetString(const char* key) const {
    if (!key) return kStringNotFound;
    const HeaderLookup* info = GetInfo(key);
    if (!info) return kStringNotFound;
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    if (!m_StringEntries.m_pData || info->str_idx >= m_StringEntries.m_Count) return kStringNotFound;
    const StringEntry* entry = &m_StringEntries.m_pData[info->str_idx];
    return s_str_blob + entry->str_offset;
#else
    return kStringNotFound;
#endif
}

// GetString(pre-resolved)
const char* Mortar::StringTable::GetString(const HeaderLookup* pre) const {
    if (!pre) return kStringNotFound;
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    if (!m_StringEntries.m_pData || pre->str_idx >= m_StringEntries.m_Count) return kStringNotFound;
    const StringEntry* entry = &m_StringEntries.m_pData[pre->str_idx];
    return s_str_blob + entry->str_offset;
#else
    return kStringNotFound;
#endif
}

// --- Static wrapper API ---

void Mortar::StringTable::Load(const char* dataDir, int languageFlag) {
    s_DefaultTable.~StringTable();
    new (&s_DefaultTable) StringTable();

    if (languageFlag < 0 || languageFlag >= kLanguageCount)
        languageFlag = 0;
    const char* lang = kLanguageSuffix[languageFlag];

    char hdr_path[512], lang_path[512];
    snprintf(hdr_path,  sizeof(hdr_path),  "%s/stringtables/translations_header.str", dataDir);
    snprintf(lang_path, sizeof(lang_path), "%s/stringtables/translations_%s.str",     dataDir, lang);

    s_DefaultTable.LoadHeader(hdr_path);
    if (!s_DefaultTable.m_HeaderLookup.m_pData) {
        LOG_ERROR("STRINGTABLE/Load", "failed to load header: %s", hdr_path);
        return;
    }

    s_DefaultTable.LoadLanguage(lang_path);
    if (!s_DefaultTable.m_StringEntries.m_pData) {
        LOG_WARN("STRINGTABLE/Load", "failed to load language '%s', falling back to english_us", lang);
        snprintf(lang_path, sizeof(lang_path),
                 "%s/stringtables/translations_english_us.str", dataDir);
        s_DefaultTable.LoadLanguage(lang_path);
        if (!s_DefaultTable.m_StringEntries.m_pData) {
            LOG_ERROR("STRINGTABLE/Load", "fallback english_us also failed");
            return;
        }
    }

#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    s_loaded = true;
#endif
}

void Mortar::StringTable::Unload() {
    s_DefaultTable.~StringTable();
    new (&s_DefaultTable) StringTable();
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    free(s_key_blob); s_key_blob = 0;
    free(s_str_blob); s_str_blob = 0;
    s_loaded = false;
#endif
}

const char* Mortar::StringTable::GetStringS(LocalizedString id) {
    return s_DefaultTable.GetString(id);
}

const char* Mortar::StringTable::GetStringS(const char* key) {
    return s_DefaultTable.GetString(key);
}

bool Mortar::StringTable::IsLoaded() {
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    return s_loaded;
#else
    return false;
#endif
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
