#ifndef FN_ENGINE_UTIL_ASCII_STRING_H
#define FN_ENGINE_UTIL_ASCII_STRING_H

#include "util/StringHash.h"
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>

namespace Mortar {

// Mortar::AsciiString -- v1.6.1 AsciiString @0x0021e684 ctor / @0x0021e454 Resize.
// 40-byte SSO body. m_size stores strlen+1 (byte count INCLUDING null terminator),
// matching the binary. Inline when m_size <= 32 (i.e. strlen <= 31); heap when
// m_size > 32 (i.e. strlen >= 32). Default ctor leaves m_size=0 (transient;
// Empty() treats m_size<=1 as empty). Empty string: m_size==1, buf[0]=='\0'.
// MicroBuffer<char,32> threshold: v1.6.1 MicroBuffer::operator[] @0x00252578 cmp r3,#0x20;
// MicroBuffer::Resize @0x0021e6ec.
// Port-specific: port doesn't model MicroBuffer as a separate template; equivalent inlined.
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
    // Returns strlen (m_size-1); 0 when m_size==0 (default-ctor transient).
    unsigned long Length() const { return m_size ? m_size - 1 : 0; }
    bool          Empty() const  { return m_size <= 1; }

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

    // Binary @ v1.6.1 AsciiString::Set @0x0021e5e4 (from char*) / @0x0021e594 (from char*,len).
    void Set(const AsciiString& other);
    void Set(const char* s);
    void Set(const char* s, unsigned long len);

    // Binary @ TBD -- equality test with caller-precomputed hash (length, then hash, then memcmp).
    bool Equals (const char* s, unsigned int hash, unsigned long len) const;
    // Binary @ TBD -- case-insensitive variant of Equals.
    bool EqualsI(const char* s, unsigned int hash, unsigned long len) const;

    // Binary @ TBD -- internal raw-pointer accessor; returns active buffer including empty-string canonical.
    const char* _GetPtr() const;

    void Append(const AsciiString& other);
    void Append(char c);
    // Resize to newLen characters (strlen). m_size becomes newLen+1.
    void Resize(unsigned long newLen);

    // For std::map<AsciiString, T> binary-faithful ordering.
    bool operator<(const AsciiString& other) const { return Compare(other) < 0; }
    bool operator==(const AsciiString& other) const;
    bool operator!=(const AsciiString& other) const { return !(*this == other); }

private:
    void        SetFromCStr(const char* s, unsigned long len);
    // m_size == strlen+1; heap when strlen >= 32 means m_size > 32.
    // v1.6.1 MicroBuffer::Resize @0x0021e6ec.
    bool        IsHeap() const { return m_size > 32; }
    char*       Buffer();
    const char* Buffer() const;

    uint32_t m_size;                // +0x00  (strlen+1; 0=default-ctor transient; 1=empty)
    union {
        char     m_inline_buf[32];  // +0x04..+0x23 (inline: strlen<=31, m_size<=32)
        struct {
            char*    m_heap;        // +0x04 (heap mode)
            uint32_t m_capacity;    // +0x08 (heap mode; capacity = strlen+1)
        } m_h;
    };
    mutable uint32_t m_hashCache;   // +0x24 (lazy; 0 = unset; cleared by every mutator)

#ifdef __bada__
    friend struct AsciiStringLayoutAssert;
#endif
};

#ifdef __bada__
// Friend probe: GCC 4.4.1 is strict about offsetof on private members from
// namespace-scope static_assert; MSVC allows it. The friend struct gives the
// asserts access without exposing the fields publicly.
struct AsciiStringLayoutAssert {
    static_assert(sizeof(AsciiString) == 40, "AsciiString must be 40 bytes (Bada SSO layout)");
    static_assert(offsetof(AsciiString, m_size)      == 0x00, "AsciiString::m_size offset");
    static_assert(offsetof(AsciiString, m_hashCache) == 0x24, "AsciiString::m_hashCache offset");
};
#endif

// Binary @ TBD -- returns true iff name == "..".
bool IsParentFolderToken(const AsciiString& name);

// Binary @ TBD -- returns true iff name == ".".
bool IsThisFolderToken(const AsciiString& name);

}  // namespace Mortar

#endif  // FN_ENGINE_UTIL_ASCII_STRING_H
