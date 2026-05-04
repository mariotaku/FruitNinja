// Analysed: 2026-05-04T00:00

#include "util/AsciiString.h"
#include <cctype>
#include <cstdlib>
#include <new>

namespace Mortar {

// Binary @ 0x00183b18 Resize (body documented in RE audit).
// SSO threshold: heap-allocate when m_size > 32.

AsciiString::AsciiString()
    : m_size(0), m_hashCache(0)
{
    // inline_buf left uninitialised; c_str() returns "" when m_size==0.
}

AsciiString::AsciiString(const char* s)
    : m_size(0), m_hashCache(0)
{
    if (s) {
        unsigned long len = (unsigned long)strlen(s);
        SetFromCStr(s, len);
    }
}

AsciiString::AsciiString(const char* s, unsigned long len)
    : m_size(0), m_hashCache(0)
{
    SetFromCStr(s, len);
}

AsciiString::AsciiString(const AsciiString& other)
    : m_size(0), m_hashCache(0)
{
    SetFromCStr(other.c_str(), other.m_size);
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
        SetFromCStr(other.c_str(), other.m_size);
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
    if (m_size == 0) return "";
    return Buffer();
}

// Binary @ 0x0018397c -- lazy hash; cleared by every mutator.
unsigned int AsciiString::Hash() const
{
    if (m_hashCache == 0 && m_size != 0) {
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
    return memcmp(c_str(), other.c_str(), m_size);
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
    for (unsigned long i = 0; i < m_size; i++) {
        int ca = tolower((unsigned char)a[i]);
        int cb = tolower((unsigned char)b[i]);
        if (ca != cb) {
            return (ca < cb) ? -1 : 1;
        }
    }
    return 0;
}

bool AsciiString::operator==(const AsciiString& other) const
{
    if (m_size != other.m_size) return false;
    if (Hash() != other.Hash()) return false;
    return memcmp(c_str(), other.c_str(), m_size) == 0;
}

// Binary @ 0x00183b18 Resize.
// Handles inline<->heap transitions; clears hash cache.
void AsciiString::Resize(unsigned long newLen)
{
    m_hashCache = 0;

    if (newLen == m_size) return;

    bool wasHeap = IsHeap();
    bool willHeap = (newLen > 32);

    if (willHeap && !wasHeap) {
        // inline -> heap
        char* buf = new char[newLen + 1];
        unsigned long copyLen = (newLen < m_size) ? newLen : m_size;
        memcpy(buf, m_inline_buf, copyLen);
        buf[newLen] = '\0';
        m_h.m_heap = buf;
        m_h.m_capacity = newLen;
        m_size = newLen;
    } else if (!willHeap && wasHeap) {
        // heap -> inline
        char* old = m_h.m_heap;
        unsigned long copyLen = (newLen < m_size) ? newLen : m_size;
        memcpy(m_inline_buf, old, copyLen);
        m_inline_buf[newLen] = '\0';
        delete[] old;
        m_size = newLen;
    } else if (willHeap && wasHeap) {
        if (newLen > m_h.m_capacity) {
            char* buf = new char[newLen + 1];
            unsigned long copyLen = (newLen < m_size) ? newLen : m_size;
            memcpy(buf, m_h.m_heap, copyLen);
            buf[newLen] = '\0';
            delete[] m_h.m_heap;
            m_h.m_heap = buf;
            m_h.m_capacity = newLen;
        }
        m_size = newLen;
    } else {
        // inline -> inline
        m_size = newLen;
    }
}

void AsciiString::Append(const AsciiString& other)
{
    if (other.m_size == 0) return;
    unsigned long oldSize = m_size;
    Resize(m_size + other.m_size);
    memcpy(Buffer() + oldSize, other.c_str(), other.m_size);
    Buffer()[m_size] = '\0';
    m_hashCache = 0;
}

void AsciiString::Append(char c)
{
    unsigned long oldSize = m_size;
    Resize(m_size + 1);
    Buffer()[oldSize] = c;
    Buffer()[m_size] = '\0';
    m_hashCache = 0;
}

void AsciiString::SetFromCStr(const char* s, unsigned long len)
{
    m_hashCache = 0;

    bool wasHeap = IsHeap();
    bool willHeap = (len > 32);

    if (willHeap) {
        if (wasHeap) {
            if (len > m_h.m_capacity) {
                delete[] m_h.m_heap;
                m_h.m_heap = new char[len + 1];
                m_h.m_capacity = len;
            }
        } else {
            m_h.m_heap = new char[len + 1];
            m_h.m_capacity = len;
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
        m_inline_buf[len] = '\0';
    }
    m_size = len;
}

}  // namespace Mortar
