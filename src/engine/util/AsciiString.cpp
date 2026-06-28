// v1.6.1 AsciiString @0x0021e684 ctor / @0x0021e454 Resize / @0x0021e594 Set(s,len)
// m_size = strlen+1 (byte count including null terminator), matching the binary.
// Inline when m_size <= 32 (strlen <= 31); heap when m_size > 32 (strlen >= 32).
// MicroBuffer threshold: v1.6.1 MicroBuffer::operator[] @0x00252578 `cmp r3,#0x20`;
// MicroBuffer::Resize @0x0021e6ec.

#include "util/AsciiString.h"
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <new>

namespace Mortar {

AsciiString::AsciiString()
    : m_size(0), m_hashCache(0)
{
    // m_size==0: default-ctor transient. Empty() and c_str() treat this as empty.
}

AsciiString::AsciiString(const char* s)
    : m_size(0), m_hashCache(0)
{
    if (s) {
        unsigned long len = (unsigned long)strlen(s);
        SetFromCStr(s, len);
    }
}

// v1.6.1 AsciiString::Set @0x0021e594 (s, len variant).
AsciiString::AsciiString(const char* s, unsigned long len)
    : m_size(0), m_hashCache(0)
{
    SetFromCStr(s, len);
}

AsciiString::AsciiString(const AsciiString& other)
    : m_size(0), m_hashCache(0)
{
    // Pass strlen = other.Length() (= other.m_size - 1 when non-empty, 0 when empty).
    SetFromCStr(other.c_str(), other.Length());
}

// Port-specific bridge: ResourceLoader.cpp constructs AsciiString from std::string.
AsciiString::AsciiString(const std::string& s)
    : m_size(0), m_hashCache(0)
{
    SetFromCStr(s.c_str(), (unsigned long)s.size());
}

AsciiString::~AsciiString()
{
    if (IsHeap()) {
        delete[] m_h.m_heap;
    }
}

AsciiString& AsciiString::operator=(const AsciiString& other)
{
    if (this != &other) {
        m_hashCache = 0;
        // Pass strlen, not m_size.
        SetFromCStr(other.c_str(), other.Length());
    }
    return *this;
}

AsciiString& AsciiString::operator=(const char* s)
{
    m_hashCache = 0;
    if (s) {
        unsigned long len = (unsigned long)strlen(s);
        SetFromCStr(s, len);
    } else {
        Resize(0);
    }
    return *this;
}

char* AsciiString::Buffer()
{
    return IsHeap() ? m_h.m_heap : m_inline_buf;
}

const char* AsciiString::Buffer() const
{
    return IsHeap() ? m_h.m_heap : m_inline_buf;
}

const char* AsciiString::c_str() const
{
    // m_size==0 (default ctor) or m_size==1 (empty string "\0") both return "".
    if (m_size <= 1) return "";
    return Buffer();
}

// Binary @ 0x0018397c -- lazy hash; cleared by every mutator.
unsigned int AsciiString::Hash() const
{
    // m_size > 1 means the string has at least one character.
    if (m_hashCache == 0 && m_size > 1) {
        m_hashCache = StringHash(c_str());
    }
    return m_hashCache;
}

// Binary @ 0x001022b8 (PLT thunk) --
// length-first, hash-second, memcmp-third. NOT lexicographic.
int AsciiString::Compare(const AsciiString& other) const
{
    if (m_size != other.m_size) {
        return (m_size < other.m_size) ? -1 : 1;
    }
    unsigned int ha = Hash();
    unsigned int hb = other.Hash();
    if (ha != hb) {
        return (ha < hb) ? -1 : 1;
    }
    // Compare strlen bytes (m_size-1 when m_size>0; safe since lengths are equal).
    unsigned long slen = Length();
    return memcmp(c_str(), other.c_str(), slen);
}

// Binary @ 0x00183a40 -- case-insensitive variant; same shape as Compare.
int AsciiString::CompareI(const AsciiString& other) const
{
    if (m_size != other.m_size) {
        return (m_size < other.m_size) ? -1 : 1;
    }
    // DIFFERS: CompareI body inferred (tolower loop); not byte-verified vs v1.6.1 binary @ 0x00183a40.
    //   asm-verify has not flagged divergence as of R4 W4.
    const char* a = c_str();
    const char* b = other.c_str();
    unsigned long slen = Length();
    for (unsigned long i = 0; i < slen; i++) {
        int ca = tolower((unsigned char)a[i]);
        int cb = tolower((unsigned char)b[i]);
        if (ca != cb) {
            return (ca < cb) ? -1 : 1;
        }
    }
    return 0;
}

// v1.6.1 AsciiString::Set @0x0021e5e4 (from AsciiString).
void AsciiString::Set(const AsciiString& other)
{
    *this = other;
}

// v1.6.1 AsciiString::Set @0x0021e5e4 (from char*).
void AsciiString::Set(const char* s)
{
    *this = s;
}

// v1.6.1 AsciiString::Set @0x0021e594 (from char*, len).
void AsciiString::Set(const char* s, unsigned long len)
{
    m_hashCache = 0;
    SetFromCStr(s, len);
}

// Binary @ TBD -- equality with caller-precomputed hash: length, then hash, then memcmp.
bool AsciiString::Equals(const char* s, unsigned int hash, unsigned long len) const
{
    // len is the caller's strlen; compare against our strlen = Length().
    if (Length() != len) return false;
    if (Hash() != hash) return false;
    return memcmp(c_str(), s, len) == 0;
}

// Binary @ TBD -- case-insensitive variant of Equals.
bool AsciiString::EqualsI(const char* s, unsigned int hash, unsigned long len) const
{
    if (Length() != len) return false;
    if (Hash() != hash) return false;
    const char* a = c_str();
    for (unsigned long i = 0; i < len; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)s[i])) return false;
    }
    return true;
}

// Binary @ TBD -- raw-pointer accessor; equivalent to c_str() but returns internal buffer address.
const char* AsciiString::_GetPtr() const
{
    return Buffer();
}

bool AsciiString::operator==(const AsciiString& other) const
{
    if (m_size != other.m_size) return false;
    if (Hash() != other.Hash()) return false;
    unsigned long slen = Length();
    return memcmp(c_str(), other.c_str(), slen) == 0;
}

// v1.6.1 AsciiString::Resize @0x0021e454 / MicroBuffer::Resize @0x0021e6ec.
// newLen is the desired strlen. After Resize, m_size == newLen+1.
void AsciiString::Resize(unsigned long newLen)
{
    m_hashCache = 0;

    // Compare desired strlen with current strlen.
    if (newLen == Length()) return;

    bool wasHeap = IsHeap();
    // Heap when strlen >= 32, i.e. byteCount = strlen+1 > 32.
    bool willHeap = (newLen >= 32);

    if (willHeap && !wasHeap) {
        // inline -> heap
        unsigned long byteCount = newLen + 1;
        char* buf = new char[byteCount];
        unsigned long copyLen = (newLen < Length()) ? newLen : Length();
        memcpy(buf, m_inline_buf, copyLen);
        buf[newLen] = '\0';
        m_h.m_heap = buf;
        m_h.m_capacity = byteCount;
        m_size = byteCount;
    } else if (!willHeap && wasHeap) {
        // heap -> inline
        char* old = m_h.m_heap;
        unsigned long oldLen = Length();
        unsigned long copyLen = (newLen < oldLen) ? newLen : oldLen;
        memcpy(m_inline_buf, old, copyLen);
        m_inline_buf[newLen] = '\0';
        delete[] old;
        m_size = newLen + 1;
    } else if (willHeap && wasHeap) {
        unsigned long byteCount = newLen + 1;
        if (byteCount > m_h.m_capacity) {
            char* buf = new char[byteCount];
            unsigned long oldLen = Length();
            unsigned long copyLen = (newLen < oldLen) ? newLen : oldLen;
            memcpy(buf, m_h.m_heap, copyLen);
            buf[newLen] = '\0';
            delete[] m_h.m_heap;
            m_h.m_heap = buf;
            m_h.m_capacity = byteCount;
        } else {
            m_h.m_heap[newLen] = '\0';
        }
        m_size = byteCount;
    } else {
        // inline -> inline
        m_inline_buf[newLen] = '\0';
        m_size = newLen + 1;
    }
}

void AsciiString::Append(const AsciiString& other)
{
    if (other.Empty()) return;
    unsigned long oldLen = Length();
    unsigned long addLen = other.Length();
    Resize(oldLen + addLen);
    memcpy(Buffer() + oldLen, other.c_str(), addLen);
    Buffer()[oldLen + addLen] = '\0';
    m_hashCache = 0;
}

void AsciiString::Append(char c)
{
    unsigned long oldLen = Length();
    Resize(oldLen + 1);
    Buffer()[oldLen] = c;
    Buffer()[oldLen + 1] = '\0';
    m_hashCache = 0;
}

// v1.6.1 AsciiString ctor @0x0021e684 / Set(s,len) @0x0021e594.
// len is strlen (NOT including null). Stores m_size = len+1.
// Inline when len <= 31 (m_size <= 32); heap when len >= 32 (m_size > 32).
void AsciiString::SetFromCStr(const char* s, unsigned long len)
{
    m_hashCache = 0;

    bool wasHeap = IsHeap();
    // v1.6.1 MicroBuffer::Resize @0x0021e6ec: heap when byteCount (=len+1) > 32.
    bool willHeap = (len >= 32);

    if (willHeap) {
        unsigned long byteCount = len + 1;
        if (wasHeap) {
            if (byteCount > m_h.m_capacity) {
                delete[] m_h.m_heap;
                m_h.m_heap = new char[byteCount];
                m_h.m_capacity = byteCount;
            }
        } else {
            m_h.m_heap = new char[byteCount];
            m_h.m_capacity = byteCount;
        }
        if (s && len > 0) {
            memcpy(m_h.m_heap, s, len);
        }
        m_h.m_heap[len] = '\0';
    } else {
        if (wasHeap) {
            delete[] m_h.m_heap;
        }
        if (s && len > 0) {
            memcpy(m_inline_buf, s, len);
        }
        // len <= 31 always lands inside the 32-byte inline buffer.
        m_inline_buf[len] = '\0';
    }
    // Store strlen+1 as the binary does.
    m_size = len + 1;
}

// Binary @ TBD -- returns true iff name == "..".
bool IsParentFolderToken(const AsciiString& name)
{
    if (name.Length() != 2) return false;
    return memcmp(name.c_str(), "..", 2) == 0;
}

// Binary @ TBD -- returns true iff name == ".".
bool IsThisFolderToken(const AsciiString& name)
{
    if (name.Length() != 1) return false;
    return name.c_str()[0] == '.';
}

}  // namespace Mortar

// ============================================================================
// GROUP A -- string utility free functions at global scope.
// Binary addresses are all v1.6.1. No namespace prefix in mangling.
// ============================================================================

// ASM-spec v1.6.1 MakeUpperCase @0x0014f18c
// In-place ASCII a-z -> A-Z only (NOT libc). NULL-safe entry; re-checks ptr each iter.
void MakeUpperCase(char* s) {
    while (s) {
        char c = *s;
        if (c == '\0') return;
        if ((unsigned char)(c - 'a') < 26u) *s = c - 0x20;
        ++s;
    }
}

// ASM-spec v1.6.1 MakeLowerCase @0x0014f1bc
// In-place ASCII A-Z -> a-z only (NOT libc). NULL-safe entry.
void MakeLowerCase(char* s) {
    while (s) {
        char c = *s;
        if (c == '\0') return;
        if ((unsigned char)(c - 'A') < 26u) *s = c + 0x20;
        ++s;
    }
}

// ASM-spec v1.6.1 StringToLower @0x00253508
// In-place, libc tolower (locale-aware). NULL-check once up front.
void StringToLower(char* s) {
    if (!s) return;
    for (; *s; ++s) *s = (char)tolower((unsigned char)*s);
}

// ASM-spec v1.6.1 StringToUpper @0x00253530
// In-place, libc toupper (locale-aware). NULL-check once up front.
void StringToUpper(char* s) {
    if (!s) return;
    for (; *s; ++s) *s = (char)toupper((unsigned char)*s);
}

// ASM-spec v1.6.1 FindSubstring @0x00253164
// DEGENERATE -- binary is broken: only ever loads needle[0], never indexes deeper.
// Net effect: always returns 0xFFFFFFFF in every case. Ported faithfully for asm-verify.
// DIFFERS-NOTE: binary FindSubstring @0x00253164 is degenerate -- always returns
// 0xFFFFFFFF (not a real substring search); ported as-is for asm-verify fidelity.
uint32_t FindSubstring(const char* haystack, const char* needle) {
    uint16_t nlen = (uint16_t)strlen(needle);
    uint16_t i = 0;
    while (haystack[i] != '\0') {
        int flag = 0;
        if (haystack[i] == needle[0]) {
            if (nlen == 0) return i;
            flag = 1;
        }
        if (haystack[i + flag] == '\0') return 0xFFFFFFFFu;
        i = (uint16_t)(i + 1);
    }
    return 0xFFFFFFFFu;
}

// ASM-spec v1.6.1 StringFindLastIndex @0x00253558
// Last index of ch in s; -1 if absent or s NULL. Null terminator matches if ch=='\0'.
int StringFindLastIndex(const char* s, char ch) {
    if (!s) return -1;
    int last = -1;
    for (int i = 0; ; ++i) {
        if (s[i] == ch) last = i;
        if (s[i] == '\0') break;
    }
    return last;
}

// ASM-spec v1.6.1 StartsWithWord @0x0014f1ec
// Plain prefix test (no trailing word-boundary check despite the name). NULL-safe.
bool StartsWithWord(const char* str, const char* word) {
    if (!str || !word) return false;
    size_t ls = strlen(str), lw = strlen(word);
    if (lw > ls) return false;
    for (int i = 0; ; ++i) {
        if (word[i] == '\0') return true;
        if (word[i] != str[i]) return false;
    }
}

// ASM-spec v1.6.1 IsStringInDelimitedList @0x002535a8
// True iff word appears as a complete delim-bounded token in list.
bool IsStringInDelimitedList(const char* list, const char* word, char delim) {
    size_t wlen = strlen(word);
    while (*list != '\0') {
        if (*word == *list) {
            const char* p = list;
            size_t k = 0;
            for (; k != wlen; ++k, ++p) {
                if (*p == '\0' || word[k] == '\0' || *p != word[k]) break;
            }
            if (k == wlen) {
                char after = p[0];
                if (after == '\0' || after == delim) return true;
            }
        }
        while (*list != '\0' && *list != delim) ++list;
        if (*list == delim) ++list;
    }
    return false;
}

// ASM-spec v1.6.1 ParseFloats @0x0014f6cc
// Parse up to count comma-separated floats into out. Exhausted slots replicate previous.
void ParseFloats(const char* s, float* out, int count) {
    if (!s) return;
    if (!out || *s == '\0') return;
    for (int i = 0; i < count; ++i, ++out) {
        if (*s == '\0') {
            *out = out[-1];
        } else {
            *out = (float)atof(s);
            while (*s != ',' && *s != '\0') ++s;
            if (*s != '\0') ++s;
        }
    }
}

// ASM-spec v1.6.1 CombineFilePaths @0x002536d0
// Join a + b into out, normalising all separators to '/'. No leading slash.
// 4th bool param unused in v1.6.1 (present for ABI fidelity).
void CombineFilePaths(const char* a, const char* b, char* out, bool /*unused*/) {
    bool secondPath = false;
    int outLen = 0;
    const char* p = a;
    for (;;) {
        while (*p == '\\' || *p == '/') ++p;
        const char* seg = p;
        const char* scan = p;
        if (*p == '.' && p[1] == '.') scan = p + 2;
        else if (*p == '.') scan = p + 1;
        const char* end;
        for (;;) {
            end = scan;
            char c = *end;
            if (c == '\\' || c == '\0' || c == '/') break;
            ++scan;
        }
        size_t n = (size_t)(end - seg);
        if (n != 0) {
            if (outLen != 0) out[outLen++] = '/';
            memcpy(out + outLen, seg, n);
            outLen += (int)n;
        }
        p = end;
        if (*end == '\0') {
            if (secondPath) { out[outLen] = '\0'; return; }
            secondPath = true;
            p = b;
        }
    }
}

// ASM-spec v1.6.1 WildCardFit @0x002531dc
// Case-insensitive glob matcher (Kirk Krauss algorithm + [set] extension).
// Returns 1 = match, 0 = no match. Metacharacters: ? * [...] [!...] [a-z].
// Note: [...] set comparisons are NOT tolower'd; only literal char matching is CI.
// asterisk() is declared in AsciiString.h (already included); the mutual recursion
// WildCardFit->asterisk->WildCardFit resolves without a local forward decl.
int WildCardFit(char* wildcard, char* test) {
    char* wc = wildcard;
    char* ts = test;
    unsigned m = 1;
    for (;;) {
        bool matchState = (m == 1);
        unsigned cw = (unsigned char)*wc;
        if (cw == 0 || !matchState) {
            while ((unsigned char)*wc == '*' && matchState) {
                ++wc;
                cw = (unsigned char)*wc;
                if (cw != '*') break;
            }
            int res = 0;
            if (m == 1 && *ts == '\0') {
                res = 1 - (int)cw;
                if (cw > 1) res = 0;
            }
            return res;
        }
        if ((unsigned char)*ts == 0) {
            m = 1; matchState = true;
            while ((unsigned char)*wc == '*') ++wc;
            cw = (unsigned char)*wc;
            int res = 0;
            if (m == 1 && *ts == '\0') {
                res = 1 - (int)cw;
                if (cw > 1) res = 0;
            }
            return res;
        }
        if (cw == '?') {
            m = 1; ++ts;
        } else if (cw == '[') {
            char* setStart = wc + 1;
            m = 0;
            bool active = true;
            char* s = wc + 1;
            if ((unsigned char)*setStart == '!') s = wc + 2;
            for (;;) {
                wc = s;
                unsigned cc = (unsigned char)*wc;
                bool cont = active;
                if (cc != ']') cont = true;
                if (!cont) break;
                if (m == 0) {
                    if (cc == '-') {
                        unsigned hi = (unsigned char)wc[1];
                        if ((unsigned char)wc[-1] < hi) {
                            active = !active;
                            if (hi == ']') active = false;
                            if (active) {
                                if ((unsigned char)wc[-1] <= (unsigned char)*ts &&
                                    (unsigned char)*ts <= hi) {
                                    m = 1; ++wc;
                                }
                                goto setNext;
                            }
                        }
                    }
                    m = ((unsigned char)*ts == cc) ? 1u : 0u;
                } else {
                    m = 1;
                }
            setNext:
                active = false; s = wc + 1;
            }
            if ((unsigned char)*setStart == '!') m = 1 - m;
            if (m == 1) ++ts;
        } else if (cw == '*') {
            m = asterisk(&wc, &ts);
            --wc;
        } else {
            int ca = tolower((unsigned char)cw);
            int cb = tolower((unsigned char)*ts);
            ++ts;
            m = (ca == cb) ? 1u : 0u;
        }
        ++wc;
    }
}

// ASM-spec v1.6.1 asterisk @0x002533e0
// '*' backtracking helper; cursors advanced via pointer. Mutually recursive with WildCardFit.
// Binary mangling: _Z8asteriskPPcS0_ (global scope, char**).
unsigned asterisk(char** wcp, char** tsp) {
    char* wc = *wcp;
    char* ts = *tsp;
    for (;;) {
        ++wc;
        if (*ts == '\0') { *wcp = wc; break; }
        char c = *wc;
        if (c != '?' && c != '*') break;
        if (c == '?') ++ts;
    }
    *wcp = wc;
    unsigned last;
    for (;;) {
        last = (unsigned char)*wc;
        if (last != '*') break;
        ++wc;
    }
    *wcp = wc;
    if (*ts == '\0') {
        unsigned r = 1 - last;
        if (last > 1) r = 0;
        *tsp = ts;
        return r;
    }
    if (WildCardFit(wc, ts) == 0) {
        do {
            ++ts;
            for (;;) {
                unsigned cw2 = (unsigned char)*wc;
                unsigned ct2 = (unsigned char)*ts;
                if (cw2 == ct2 || cw2 == '[') break;
                if (ct2 == 0) { *tsp = ts; return last; }
                ++ts;
            }
            if (*ts == '\0') { *tsp = ts; return 1; }
            last = (unsigned)WildCardFit(wc, ts);
        } while (((last ^ 1) & 0xff) != 0);
    }
    last = 1;
    if (*ts == '\0' && *wc == '\0') last = 1;
    *tsp = ts;
    return last;
}
