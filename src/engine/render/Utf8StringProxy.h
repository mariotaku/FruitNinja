#ifndef FN_ENGINE_RENDER_UTF8STRINGPROXY_H
#define FN_ENGINE_RENDER_UTF8STRINGPROXY_H

#include <cstdint>

// Mortar::Utf8StringProxy — 16-byte polymorphic string factory.
// Binary: v1.6.1 Mortar::Utf8StringProxy @ 0x0021eb98 (size 0x10).
// SEPARATE from Utf8StringIterator — not a base class of it.
//
// ASM-spec v1.6.1 Mortar::Utf8StringProxy @ 0x0021eb98: SEPARATE 16-byte polymorphic
// factory {vptr, +0x04 begin, +0x08 numChars, +0x0C end};
// vtable PTR__Utf8StringProxy_002cf368.
//
// Layout:
//   +0x00  void**     vptr       (installed by ctor; GOT-indirect vtable)
//   +0x04  char*      m_Begin    (string start; set in ctor)
//   +0x08  uint32_t   m_NumChars (total codepoint count; walked in ctor)
//   +0x0C  char*      m_End      (one-past-last byte)
//
// Binary ctors:
//   Proxy ctor (str)     @ 0x0021eb98
//   Proxy copy-ctor      @ 0x0021eafc
//   Proxy operator=      @ 0x0021eae0
//   Proxy c_str          @ 0x0021ec98
//   Proxy dtor           @ 0x0021eca0
//
// Port specific: GOT-indirect vtable install replaced by native C++ vptr.
// The Proxy is a transient factory; the iterator extracts begin from it
// then operates independently via the flat cursor/src/codepoint triple.

namespace Mortar {

class Utf8StringProxy {
public:
    // Binary @ 0x0021eb98
    explicit Utf8StringProxy(const char* str);
    Utf8StringProxy(const Utf8StringProxy& other);
    Utf8StringProxy& operator=(const Utf8StringProxy& other);
    virtual ~Utf8StringProxy();

    const char* c_str() const { return m_Begin; }

    const char* m_Begin;     // +0x04 string start
    uint32_t    m_NumChars;  // +0x08 total codepoint count
    const char* m_End;       // +0x0C one-past-last byte
};

#ifdef __bada__
static_assert(sizeof(Utf8StringProxy) == 0x10, "Utf8StringProxy sizeof mismatch");
#endif

} // namespace Mortar

#endif // FN_ENGINE_RENDER_UTF8STRINGPROXY_H
