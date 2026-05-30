#ifndef FN_ENGINE_RENDER_UTF8STRINGPROXY_H
#define FN_ENGINE_RENDER_UTF8STRINGPROXY_H

#include <cstdint>

// Mortar::Utf8StringProxy — 28-byte polymorphic base (vtable at +0x00).
// Binary: Ghidra struct "Mortar::Utf8StringProxy" == 28 bytes == 0x1C.
// Layout (from proxy copy-ctor @ 0x00160cbc and base ctor @ 0x001018bc / 0x001840c0):
//   +0x00  void**     vptr             (installed by base ctor 0x001840c0; GOT-indirect vtable)
//   +0x04  char*      field1_0x4       (current/string ptr; in iterator = m_PrevBegin)
//   +0x08  uint32_t   field_0x8        (in iterator = m_CurrentCodepoint)
//   +0x0C  uint32_t   field6_0xc       (in iterator = m_NumChars)
//   +0x10  void*      field7_0x10      (in iterator = m_NextScan cursor)
//   +0x14  void*      field8_0x14      (in iterator = m_End pointer)
//   +0x18  uint32_t   field9_0x18      (in iterator = aux / unused)
//
// POLYMORPHIC: vtable installed by base ctor; vtable VA unresolvable statically
// (GOT-indirect computation in 0x001840c0).
// Utf8StringIterator inherits this class at offset 0 and adds NO data members.
//
// Port-side naming: Utf8StringIterator exposes semantic accessors mapping these
// base fields to iterator cursor concepts. The field names here are aliased
// publicly via Utf8StringIterator for backward compatibility with Font.cpp callers.

namespace Mortar {

class Utf8StringProxy {
public:
    // Binary @ 0x001018bc (GOT thunk -> 0x001840c0)
    explicit Utf8StringProxy(const char* str);
    Utf8StringProxy(const Utf8StringProxy& other);
    virtual ~Utf8StringProxy();

    const char* m_PrevBegin;        // +0x04 field1_0x4 — start of current codepoint
    uint32_t    m_CurrentCodepoint; // +0x08 field_0x8  — 0 = end-of-string
    uint32_t    m_NumChars;         // +0x0C field6_0xc  — total codepoint count
    const char* m_NextScan;         // +0x10 field7_0x10 — decode cursor
    const char* m_End;              // +0x14 field8_0x14 — one-past-last byte
    uint32_t    m_field9_0x18;      // +0x18 field9_0x18 — auxiliary
};

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
static_assert(sizeof(Utf8StringProxy) == 0x1C, "Utf8StringProxy sizeof mismatch");
#endif

} // namespace Mortar

#endif // FN_ENGINE_RENDER_UTF8STRINGPROXY_H
