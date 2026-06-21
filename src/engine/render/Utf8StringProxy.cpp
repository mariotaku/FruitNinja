#include "render/Utf8StringProxy.h"
#include "render/Utf8StringIterator.h"

namespace Mortar {

// Binary @ 0x0021eb98 — single-arg ctor: installs vtable, sets m_Begin=str,
// walks string to count codepoints (m_NumChars) and locate end (m_End).
// Port specific: GOT-indirect vtable install replaced by native C++ vptr.
Utf8StringProxy::Utf8StringProxy(const char* str)
    : m_Begin(str)
    , m_NumChars(0)
    , m_End(str)
{
    const char* scan = str;
    uint32_t count = 0;
    while (*scan) {
        utf8::decode_next_unicode_character(&scan);
        ++count;
    }
    m_NumChars = count;
    m_End = scan;
}

// Binary @ 0x0021eafc — copy-ctor: installs vtable, copies m_Begin/m_NumChars/m_End.
// Port specific: vtable install handled by native C++ vptr.
Utf8StringProxy::Utf8StringProxy(const Utf8StringProxy& other)
    : m_Begin(other.m_Begin)
    , m_NumChars(other.m_NumChars)
    , m_End(other.m_End)
{
}

// Binary @ 0x0021eae0 — operator=: copies m_Begin/m_NumChars/m_End.
Utf8StringProxy& Utf8StringProxy::operator=(const Utf8StringProxy& other) {
    m_Begin    = other.m_Begin;
    m_NumChars = other.m_NumChars;
    m_End      = other.m_End;
    return *this;
}

// Binary @ 0x0021eca0 — dtor: virtual, trivial.
Utf8StringProxy::~Utf8StringProxy() {}

} // namespace Mortar
