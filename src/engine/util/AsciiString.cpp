// v1.6.1 AsciiString @0x0021e684 ctor / @0x0021e454 Resize / @0x0021e594 Set(s,len)
// m_size = strlen+1 (byte count including null terminator), matching the binary.
// Inline when m_size <= 32 (strlen <= 31); heap when m_size > 32 (strlen >= 32).
// MicroBuffer threshold: v1.6.1 MicroBuffer::operator[] @0x00252578 `cmp r3,#0x20`;
// MicroBuffer::Resize @0x0021e6ec.

#include "util/AsciiString.h"
#include <cctype>
#include <cstdlib>
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
    // DIFFERS: CompareI body inferred (tolower loop); not byte-verified vs binary @ 0x00183a40.
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
