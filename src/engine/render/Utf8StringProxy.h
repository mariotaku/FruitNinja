#ifndef FN_ENGINE_RENDER_UTF8STRINGPROXY_H
#define FN_ENGINE_RENDER_UTF8STRINGPROXY_H

#include <cstdint>

// Mortar::Utf8StringProxy — 28-byte polymorphic base (vtable at +0x00).
// Binary: Ghidra struct "Mortar::Utf8StringProxy" == 28 bytes == 0x1C.
// Layout (from proxy copy-ctor @ 0x00160cbc, base ctor @ 0x001018bc / 0x001840c0,
//         Advance @ 0x00184128, Reset/_Init @ 0x001984a8):
//   +0x00  void**     vptr             (installed by base ctor 0x001840c0; GOT-indirect vtable)
//   +0x04  char*      m_Begin          (IMMUTABLE original string start; set once in ctor, NEVER written by Advance)
//   +0x08  uint32_t   m_NumChars       (total codepoint count, set by ctor walk)
//   +0x0C  char*      m_End            (one-past-last byte; port-side bounds guard)
//   +0x10  char*      m_PrevBegin      (start of most-recently-decoded codepoint; written by Advance: +0x10 = old +0x14)
//   +0x14  char*      m_NextScan       (live decode cursor; written by Advance via decode_next)
//   +0x18  uint32_t   m_CurrentCodepoint (decoded value; 0 = end-of-string; written by Advance)
//
// ASM-verified: 2026-06-12 binary @ 0x001984a8 (Reset) / 0x00184128 (Advance) (asm-inspector)
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

    const char* m_Begin;            // +0x04 field1_0x4 — IMMUTABLE original string start (set once in ctor, never by Advance)
    uint32_t    m_NumChars;         // +0x08 field_0x8  — total codepoint count
    const char* m_End;              // +0x0C field6_0xc — one-past-last byte (port-side bounds guard)
    const char* m_PrevBegin;        // +0x10 field7_0x10 — start of most-recently-decoded codepoint (written by Advance)
    const char* m_NextScan;         // +0x14 field8_0x14 — live decode cursor (written by Advance via decode_next)
    uint32_t    m_CurrentCodepoint; // +0x18 field9_0x18 — decoded codepoint value; 0 = end-of-string
};

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
static_assert(sizeof(Utf8StringProxy) == 0x1C, "Utf8StringProxy sizeof mismatch");
#endif

} // namespace Mortar

#endif // FN_ENGINE_RENDER_UTF8STRINGPROXY_H
