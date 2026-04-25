<!-- Analysed: 2026-04-25T14:30 -->

# Localisation / String Table System

## Overview

The Mortar Engine string table system provides key-based lookup of localised
strings.  At runtime two on-disk `.str` files are loaded: a shared header file
(`translations_header.str`) and one language-specific body file
(`translations_<lang>.str`).  A binary search over the sorted header entries
maps a key string to a per-language string index, then a direct offset-table
lookup in the body file returns the translated `const char*`.

Assets live in `FruitNinjaBada/Data/stringtables/`.

---

## Binary Functions

| Address    | Name                                 | Purpose |
|------------|--------------------------------------|---------|
| `0x0011fb20` | `StringTableUtilLoadStrings()`     | Loads table for slot 0 (primary); marks as loaded |
| `0x0011f9dc` | `StringTableUtilLoadStringsTable(int slot)` | Core loader: selects language string, builds header + body paths, calls LoadHeader + LoadLanguage |
| `0x0018a490` | `Mortar::StringTable::LoadHeader(char*)` | Opens file, calls `LoadHeader(File&)` |
| `0x0018a460` | `Mortar::StringTable::LoadHeader(File&)` | Reads FileHeader, calls `FileData<HeaderLookup>::Load` |
| `0x0018a41c` | `Mortar::StringTable::LoadLanguage(char*)` | Opens file, calls `LoadLanguage(File&)` |
| `0x0018a3ec` | `Mortar::StringTable::LoadLanguage(File&)` | Reads FileHeader, calls `FileData<StringEntry>::Load` |
| `0x0018a2cc` | `Mortar::StringTable::GetInfo(char*)` | **Key lookup** — binary search on sorted HeaderLookup[], returns entry ptr or NULL |
| `0x0011fec8` | `Mortar::StringTable::GetString(HeaderLookup*)` | Resolves HeaderLookup → StringEntry → `char*` |
| `0x0011fef0` | `Mortar::StringTable::GetString(char*)` | Calls GetInfo then GetString(HeaderLookup*) |
| `0x0011fb40` | `GETSTRING_STR(char* key, int slot)` | Public entrypoint; accesses `g_GameData`-relative StringTable at slot; returns key on miss |
| `0x00109ec0` | `GETSTRING_CAST_0_STR(char* key)` | Wraps `GETSTRING_STR(key, 0)` |
| `0x0011f940` | `StringTableUtilLoaded()`           | Returns `loaded` flag byte |
| `0x0011f9b8` | `StringTableUtilUnload()`           | Unloads slot 0, clears flag |

**Call site**: `InitialiseData` (`0x0010b67e`) calls:
```
StringTableUtilInit();
StringTableUtilLoadStrings();   // loads slot 0 with current language
```
This is step 1 of `InitialiseData`, which itself is called from `GameInitialise`.

---

## Language Selection

`StringTableUtilLoadStringsTable(int slot)` reads the language enum from
`g_GameData + 3` (the `languageFlag` byte).  A `switch` on values 1–13
selects the language sub-string; the default (0 or 14) falls through to
`english_us`.

| Enum value | Language file suffix |
|-----------|----------------------|
| 0 (default / ≥14) | `english_us` |
| 1  | `german` |
| 2  | `dutch` |
| 3  | `french` |
| 4  | `spanish` |
| 5  | `italian` |
| 6  | `swedish` |
| 7  | `danish` |
| 8  | `norwegian` |
| 9  | `finnish` |
| 10 | `korean` |
| 11 | `japanese` |
| 12 | `english_uk` |
| 13 | `chinese` |

When `slot == 0` the header path uses the base name `"translations"`:
```
header path:  "stringtables/translations_header.str"
body path:    "stringtables/translations_<lang>.str"
```
If `LoadLanguage` fails for the chosen language, it falls back to `english_us`.

---

## `.str` File Format

Both the header file and language body files share the same outer wrapper:

```
Outer wrapper (FileHeader) — 68 bytes:
  [0x00]  uint32  magic       = 1
  [0x04]  uint8[64] token     = shared 64-byte GUID (identical across all language files)
```
`FileHeader::Check` (`0x0018a3b4`) validates `magic == 1` and accepts the token
if the in-memory token buffer is still all-zeroes (first load) or if the token
matches an already-loaded value.

### `translations_header.str` — `FileData<HeaderLookup>`

```
[0x44]  uint32  blob_byte_size   = total byte count of the FileData payload
[0x48]  uint32  count            = 961  (number of lookup entries)
[0x4c]  HeaderLookup[961]        (each 40 bytes, see below)
[0x9674] char[]  key_blob        (null-terminated ASCII key strings, packed)
```

**HeaderLookup entry — 40 bytes (10 × uint32):**

| Offset | Type   | Name        | Notes |
|--------|--------|-------------|-------|
| +0x00  | uint32 | `key_offset` | byte offset from `key_blob_off` (= 0x9674) to the null-terminated key string |
| +0x04  | uint32 | *unknown*   | same value as `keylen` in all observed entries |
| +0x08  | uint32 | `keylen`    | `strlen` of the key (no null) — used by `GetInfo` binary search |
| +0x0c  | uint32 | *unknown*   | constant 0x3c05 in all observed entries |
| +0x10  | uint32 | *unknown*   | |
| +0x14  | uint32 | *unknown*   | |
| +0x18  | uint32 | *unknown*   | |
| +0x1c  | uint32 | *unknown*   | |
| +0x20  | uint32 | *unknown*   | |
| +0x24  | uint32 | `str_idx`   | index into `StringEntry[]` in the language body file |

Entries are sorted **ascending by key string** (case-sensitive byte order),
enabling binary search.

### `translations_<lang>.str` — `FileData<StringEntry>`

```
[0x44]  uint32  blob_byte_size   = total byte count of the FileData payload
[0x48]  uint32  count            = 961  (same count as header)
[0x4c]  StringEntry[961]         (each 12 bytes, see below)
[0x2d58] char[]  str_blob        (null-terminated UTF-8 strings, packed)
```

**StringEntry — 12 bytes (3 × uint32):**

| Offset | Type   | Name         | Notes |
|--------|--------|--------------|-------|
| +0x00  | uint32 | `str_offset` | byte offset from `str_blob_off` (= 0x2d58) to the null-terminated string |
| +0x04  | uint32 | *unknown*    | equals `strlen` of the translated string in all observed entries |
| +0x08  | uint32 | *unknown*    | same value as field +0x04 |

The string blob contains null-terminated UTF-8 strings.  Languages with
multi-byte characters (Japanese, Korean, Chinese) use UTF-8 encoding.

### Key → String Lookup Summary

```
1. Binary-search HeaderLookup[] by key string
      (compare up to min(entry.keylen, query_len)+1 bytes with memcmp)
2. entry.str_idx  → index into StringEntry[]
3. StringEntry[str_idx].str_offset + str_blob_off  → const char*
4. If binary search finds no match: return the key itself (pass-through)
```

Confirmed working examples (English):
```
"GEN_OK"         -> "OK"
"GEN_YES"        -> "Yes"
"GEN_NO"         -> "No"
"DOJO_TEXT_05"   -> "DISCO BLADE"
"ACHIEVEMENT_00" -> "FRUIT NINJA"
```

---

## In-Memory `StringTable` Struct Layout

```
StringTable (0x50 bytes):
  [+0x00] uint8[64]                   token          (64-byte GUID, filled by FileHeader::Check)
  [+0x40] FileData<HeaderLookup>      header         {ptr: HeaderLookup*, count: uint32}
  [+0x48] FileData<StringEntry>       language       {ptr: StringEntry*, count: uint32}
```

`StringTable` objects are embedded in `g_GameData` starting at offset
`+0x5b4`.  `GETSTRING_STR(key, slot)` accesses
`*(g_GameData) + slot * 0x50 + 0x5b4`.  Currently only slot 0 is used.

---

## `GetInfo` Binary Search Pseudocode

```c
// Mortar::StringTable::GetInfo(char* key) — 0x0018a2cc
const HeaderLookup* GetInfo(const StringTable* st, const char* key) {
    size_t key_len = strlen(key);
    size_t lo = 0, hi = st->header.count;
    while (lo != hi) {
        size_t mid = lo + (hi - lo) / 2;
        const HeaderLookup* e = &st->header.ptr[mid];
        size_t cmp_len = (e->keylen <= key_len) ? e->keylen : key_len;
        int c = memcmp(key, e->key_ptr, cmp_len + 1);
        if (c < 0)
            hi = mid;
        else if (c > 0)
            lo = mid + 1;
        else
            return e;
    }
    return NULL;
}
```

After loading, `e->key_ptr` is a live heap pointer (populated by
`FileData<HeaderLookup>::Load`); in the file it is a byte offset from
`key_blob_off`.

---

## `GETSTRING_STR` Pseudocode

```c
// GETSTRING_STR(char* key, int slot) — 0x0011fb40
const char* GETSTRING_STR(const char* key, int slot) {
    StringTable* st = (StringTable*)(g_GameData + slot * 0x50 + 0x5b4);
    const HeaderLookup* info = StringTable::GetInfo(st, key);
    if (info == NULL)
        return key;         // pass-through on miss
    return StringTable::GetString(st, info);   // -> StringEntry -> char*
}
```

---

## Port Implementation Spec

### Files to create

```
src/engine/util/Localisation.h
src/engine/util/Localisation.cpp
```

### Suggested API

```cpp
// Localisation.h
class Localisation {
public:
    // Load strings from the given data directory (e.g. "Data/").
    // Selects language based on languageFlag (0 = english_us, 1..13 = others).
    // Falls back to english_us if the language file fails to open.
    static void Load(const char* dataDir, int languageFlag);

    // Release all loaded data.
    static void Unload();

    // Look up key, return translated string or key itself on miss.
    static const char* Get(const char* key);

    // Returns true if Load() has been called and succeeded.
    static bool IsLoaded();
};
```

### Loader pseudocode

```cpp
void Localisation::Load(const char* dataDir, int languageFlag) {
    // Map languageFlag to file suffix (see Language Selection table above)
    const char* lang = language_suffix[languageFlag];  // "english_us", "german", etc.

    // Build paths
    char hdr_path[260], lang_path[260];
    snprintf(hdr_path,  sizeof(hdr_path),  "%sstringtables/translations_header.str", dataDir);
    snprintf(lang_path, sizeof(lang_path), "%sstringtables/translations_%s.str", dataDir, lang);

    // Load header file
    if (!s_table.LoadHeader(hdr_path)) return;

    // Load language body; fall back to english_us on failure
    if (!s_table.LoadLanguage(lang_path)) {
        snprintf(lang_path, sizeof(lang_path), "%sstringtables/translations_english_us.str", dataDir);
        s_table.LoadLanguage(lang_path);
    }
    s_loaded = true;
}
```

### Lookup pseudocode

```cpp
const char* Localisation::Get(const char* key) {
    if (!s_loaded) return key;
    const HeaderLookup* info = s_table.GetInfo(key);
    if (!info) return key;
    return s_table.GetString(info);
}
```

### StringTable internal implementation

The port's `StringTable` struct needs:

```cpp
struct HeaderLookup {
    const char* key_ptr;    // pointer into key_blob (set during load)
    uint32_t    unknown1;
    uint32_t    keylen;
    uint32_t    unknown2[6];
    uint32_t    str_idx;    // at +0x24
};  // 40 bytes

struct StringEntry {
    uint32_t    str_offset;
    uint32_t    strlen_cached;
    uint32_t    strlen_cached2;
};  // 12 bytes

struct StringTable {
    uint8_t      token[64];
    // After load, these hold heap-allocated arrays + counts
    HeaderLookup* header_entries;   // count=961, sorted by key
    uint32_t      header_count;
    const char*  key_blob;          // heap copy of key string area
    StringEntry*  lang_entries;     // count=961
    uint32_t      lang_count;
    const char*  str_blob;          // heap copy of translated string area
};
```

File offsets translate directly to runtime pointers during `Load`.

### Wire into `GETSTRING_CAST_0_STR`

In `src/game/ItemParseUtil.h`, replace the stub:

```cpp
// Was: return key;
inline const char* GETSTRING_CAST_0_STR(const char* key) {
    return Localisation::Get(key);
}
```

Also update `GETSTRING_STR` (if it has a separate definition) to forward to
`Localisation::Get(key)` regardless of slot, since only slot 0 is used.

### Wire-up in `InitialiseData`

In `src/game/` `InitialiseData` (after `StringTableUtilInit` / before data
loaders):

```cpp
Localisation::Load("Data/", g_GameData->languageFlag);
```

---

## Known Gaps / Not Blocked

- **Fields +0x04, +0x0c..+0x20 of HeaderLookup and +0x04/+0x08 of StringEntry**
  are parsed from the file but unused by the lookup path (`GetInfo` and
  `GetString` only use `key_ptr`, `keylen`, and `str_idx`/`str_offset`).
  They can be stored as opaque uint32 or skipped entirely in the port.
- **No encryption or compression** — files are plaintext binary, no
  Bada-specific encoding beyond UTF-8.
- **`StringTableUtilInit`** is a no-op stub in the binary (`bx lr` only).
- **Multiple slots** (`slot > 0`): the binary supports up to
  `(g_GameData_size - 0x5b4) / 0x50` slots but only slot 0 is ever loaded.
  The port can hard-code slot 0.
