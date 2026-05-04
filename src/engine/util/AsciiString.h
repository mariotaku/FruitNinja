#ifndef FN_ENGINE_UTIL_ASCII_STRING_H
#define FN_ENGINE_UTIL_ASCII_STRING_H

// Analysed: 2026-05-04T00:00

#include "util/StringHash.h"
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>

namespace Mortar {

// Mortar::AsciiString -- binary @ 0x00183c54 ctors / @ 0x00183b18 Resize.
// 40-byte SSO body (inline_buf for strings <= 32 chars; heap-allocated for longer).
// Inherits Mortar::MicroBuffer<char, 32> -- for port simplicity we inline the
// body here.
// DIFFERS: port doesn't model MicroBuffer as a separate template; equivalent inlined.
class AsciiString {
public:
    AsciiString();
    AsciiString(const char* s);
    AsciiString(const char* s, unsigned long len);
    AsciiString(const AsciiString& other);
    AsciiString(const std::string& s);  // Port-specific bridge for ResourceLoader.cpp etc.
    ~AsciiString();

    AsciiString& operator=(const AsciiString& other);
    AsciiString& operator=(const char* s);

    const char*   c_str() const;
    unsigned long Length() const { return m_size; }
    bool          Empty() const  { return m_size == 0; }

    // Port bridge: binary uses c_str(); call sites in port use CStr() from old wrapper.
    const char* CStr() const { return c_str(); }
    // Port bridge: binary uses Empty(); old call sites use IsEmpty().
    bool IsEmpty() const { return Empty(); }

    // Binary @ 0x0018397c -- lazy StringHash; cleared by mutators.
    unsigned int Hash() const;

    // Binary @ 0x001022b8 (PLT thunk) -- length-first, hash-second, memcmp-third.
    // NOT lexicographic. Used by operator< for std::map<AsciiString, T>.
    int  Compare (const AsciiString& other) const;
    // Binary @ 0x00183a40 -- case-insensitive variant.
    int  CompareI(const AsciiString& other) const;

    void Append(const AsciiString& other);
    void Append(char c);
    void Resize(unsigned long newLen);

    // For std::map<AsciiString, T> binary-faithful ordering.
    bool operator<(const AsciiString& other) const { return Compare(other) < 0; }
    bool operator==(const AsciiString& other) const;
    bool operator!=(const AsciiString& other) const { return !(*this == other); }

private:
    void        SetFromCStr(const char* s, unsigned long len);
    bool        IsHeap() const { return m_size > 32; }
    char*       Buffer();
    const char* Buffer() const;

    uint32_t m_size;                // +0x00
    union {
        char     m_inline_buf[32];  // +0x04..+0x23 (inline mode; strings <= 32 chars)
        struct {
            char*    m_heap;        // +0x04 (heap mode)
            uint32_t m_capacity;    // +0x08 (heap mode)
        } m_h;
    };
    mutable uint32_t m_hashCache;   // +0x24 (lazy; 0 = unset; cleared by every mutator)
};

#ifdef __bada__
static_assert(sizeof(AsciiString) == 40, "AsciiString must be 40 bytes (Bada SSO layout)");
static_assert(offsetof(AsciiString, m_size)      == 0x00, "AsciiString::m_size offset");
static_assert(offsetof(AsciiString, m_hashCache) == 0x24, "AsciiString::m_hashCache offset");
#endif

}  // namespace Mortar

#endif  // FN_ENGINE_UTIL_ASCII_STRING_H
