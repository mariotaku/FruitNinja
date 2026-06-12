#ifndef FN_ENGINE_RENDER_UTF8STRINGITERATOR_H
#define FN_ENGINE_RENDER_UTF8STRINGITERATOR_H

#include "render/Utf8StringProxy.h"
#include <cstdint>

// Mortar::Utf8StringIterator — derives from Mortar::Utf8StringProxy at offset 0.
// sizeof == 0x1C (28) — entirely the base Utf8StringProxy sub-object; no own data members.
//
// Binary record (Ghidra struct Mortar::Utf8StringIterator == 28 bytes):
//   Offset 0: base Utf8StringProxy (vptr @ +0x00 through field9_0x18 @ +0x18)
//   No additional members beyond the base.
//
// POLYMORPHIC: vtable installed by Utf8StringProxy base ctor (0x001840c0, GOT-indirect).
//   Utf8StringIterator ctor (0x0012fe00) delegates to proxy base ctor (GOT thunk
//   0x001018bc) then calls _Init (0x000f8514). Does NOT write its own vtable.
//
// The base fields serve as iterator cursor state (binary-verified @ Advance 0x00184128 / Reset 0x001984a8):
//   m_Begin            (+0x04) — IMMUTABLE original string start; set once in ctor, NEVER written by Advance
//   m_NumChars         (+0x08) — total codepoint count (set by ctor walk)
//   m_End              (+0x0C) — one-past-last byte of input string
//   m_PrevBegin        (+0x10) — start of most-recently-decoded codepoint (written by Advance)
//   m_NextScan         (+0x14) — live decode cursor (written by Advance via decode_next)
//   m_CurrentCodepoint (+0x18) — decoded codepoint value; 0 = end-of-string

namespace Mortar {

namespace utf8 {
    // Standard UTF-8 1-6 byte decoder. Advances *cursor past the decoded bytes.
    // Returns 0 when *cursor points at '\0'; returns 0xFFFD on malformed input.
    uint32_t decode_next_unicode_character(const char** cursor);
}

class Utf8StringIterator : public Utf8StringProxy {
public:
    // Binary @ 0x0012fe00 — builds temp Utf8StringProxy(str), calls base ctor, then _Init.
    explicit Utf8StringIterator(const char* str);
    // Binary @ 0x00160cbc — base proxy copy-ctor + copies cursor fields.
    Utf8StringIterator(const Utf8StringIterator& other);

    bool IsEmpty() const { return m_CurrentCodepoint == 0; }
    void Advance(int n);
    void operator++(int) { Advance(1); }
    Utf8StringIterator operator+(int n) const;

    // Binary @ 0x001984a8 -- Reset(): rewind the iterator to the immutable string start
    // (m_Begin, +0x04) so it can be walked a second time. Used by BakedString::Bake
    // between pass 1 and pass 2. Resets m_NextScan to m_Begin and re-decodes the
    // first codepoint via Advance(1).
    void Reset();

    // No own data members — sizeof == sizeof(Utf8StringProxy) == 0x1C.
};

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
static_assert(sizeof(Utf8StringIterator) == 0x1C, "Utf8StringIterator sizeof mismatch");
#endif

}  // namespace Mortar
#endif // FN_ENGINE_RENDER_UTF8STRINGITERATOR_H
