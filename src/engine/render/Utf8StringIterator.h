#ifndef FN_ENGINE_RENDER_UTF8STRINGITERATOR_H
#define FN_ENGINE_RENDER_UTF8STRINGITERATOR_H

#include "render/Utf8StringProxy.h"
#include <cstdint>

// Mortar::Utf8StringIterator — NON-polymorphic flat iterator, sizeof 0x0C.
// Binary: v1.6.1 Mortar::Utf8StringIterator @ 0x0021ebcc (flat, no vtable).
//
// ASM-spec v1.6.1 Mortar::Utf8StringIterator::Advance @ 0x0021ebcc:
// NON-polymorphic flat {+0x00 char* cursor, +0x04 char* src, +0x08 u32 codepoint};
// if(cursor==0)return; loop: if(*cursor=='\0'){codepoint=0;return;}
// src=cursor; codepoint=decode_next(&src); cursor=src.
//
// The binary iterator does NOT inherit Utf8StringProxy; they are SEPARATE types.
// Utf8StringProxy (@ 0x0021eb98, 16 bytes, polymorphic) is a transient factory
// used to extract the string start; the iterator then operates independently.
//
// Binary ctors:
//   Iterator ctor (str)  @ 0x00235a70 — stack-builds Proxy, calls _Init
//   _Init                @ 0x0021ec20 — extracts begin from proxy, seeds cursor
//
// DIFFERS: binary flat iterator is 0x0C (no begin field); port adds a 4th
// PORT-ONLY m_Begin for Reset() so it can rewind without re-creating a Proxy.
// sizeof 0x10 not 0x0C.
// v1.6.1 Utf8StringIterator @ 0x0021ebcc

namespace Mortar {

namespace utf8 {
    // Standard UTF-8 1-6 byte decoder. Advances *cursor past the decoded bytes.
    // Returns 0 when *cursor points at '\0'; returns 0xFFFD on malformed input.
    uint32_t decode_next_unicode_character(const char** cursor);

    // ASM-spec v1.6.1 Mortar::utf8::encode_unicode_character @ 0x0022dd7c:
    //   Standard RFC 2279 multi-byte UTF-8 encoder.
    //   buf: pre-allocated output char buffer; *len_ptr: current byte offset, updated in-place.
    //   codepoint: UCS-4 value. Writes 1-6 bytes; no output for cp >= 0x80000000.
    void encode_unicode_character(char* buf, int* len_ptr, uint32_t codepoint);
}

class Utf8StringIterator {
public:
    // Binary @ 0x00235a70 — stack-builds a Utf8StringProxy, then calls _Init
    // (@ 0x0021ec20) which extracts begin from the proxy and seeds the cursor.
    explicit Utf8StringIterator(const char* str);
    // Copy-ctor: copies all four fields (m_Cursor, m_Src, m_CurrentCodepoint, m_Begin).
    Utf8StringIterator(const Utf8StringIterator& other);

    bool IsEmpty() const { return m_CurrentCodepoint == 0; }

    // Binary @ 0x0021ebcc — Advance n steps through the UTF-8 string.
    void Advance(int n);
    void operator++(int) { Advance(1); }
    Utf8StringIterator operator+(int n) const;

    // _Init / Reset — binary @ 0x0021ec20.
    // Rewinds cursor to string start and re-primes the first codepoint.
    // DIFFERS: binary re-seeds from a fresh Utf8StringProxy via _Init;
    // port uses port-only m_Begin to avoid re-creating the proxy.
    // v1.6.1 Utf8StringIterator @ 0x0021ec20
    void Reset();

    // +0x08 — decoded codepoint value; 0 = end-of-string.
    // Public for direct access by Font.cpp consumers.
    uint32_t m_CurrentCodepoint;

private:
    const char* m_Cursor;  // +0x00 binary: cursor (live decode position)
    const char* m_Src;     // +0x04 binary: src (decode scratch target)

    // PORT-ONLY: not in the binary's flat 3-word iterator.
    // Stores string start so Reset() can rewind without re-creating a Proxy.
    // DIFFERS: original = flat 3-word iterator re-seeds begin from a fresh
    // Utf8StringProxy via _Init; port keeps m_Begin for Reset() -- adds 4 bytes
    // (sizeof 0x10 not 0x0C). v1.6.1 Utf8StringIterator @ 0x0021ebcc
    const char* m_Begin;   // +0x0C PORT-ONLY
};

#ifdef __bada__
// sizeof is 0x10 (not 0x0C) due to PORT-ONLY m_Begin field; see DIFFERS above.
static_assert(sizeof(Utf8StringIterator) == 0x10, "Utf8StringIterator sizeof mismatch");
#endif

}  // namespace Mortar
#endif // FN_ENGINE_RENDER_UTF8STRINGITERATOR_H
