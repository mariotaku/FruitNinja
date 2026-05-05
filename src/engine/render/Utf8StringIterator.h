#ifndef FN_ENGINE_RENDER_UTF8STRINGITERATOR_H
#define FN_ENGINE_RENDER_UTF8STRINGITERATOR_H

#include <cstdint>

// Utf8StringIterator — matches binary ABI (3 pointer words + 1 uint32 + 2 ptr + 1 int)
// The ctor walks the string once to compute m_NumChars and m_End, then primes
// m_CurrentCodepoint via Advance(1).  m_PrevBegin points to the start of the
// most-recently-decoded codepoint; m_NextScan is the cursor passed to
// decode_next_unicode_character on the next Advance call.
//
// Binary does NOT have a length-aware constructor overload.

namespace Mortar {

namespace utf8 {
    // Standard UTF-8 1-6 byte decoder.  Advances *cursor past the decoded bytes.
    // Returns 0 when *cursor points at '\0'; returns 0xFFFD on malformed input.
    uint32_t decode_next_unicode_character(const char** cursor);
}

class Utf8StringIterator {
public:
    explicit Utf8StringIterator(const char* str);
    Utf8StringIterator(const Utf8StringIterator& other);

    bool     IsEmpty() const { return m_CurrentCodepoint == 0; }
    void     Advance(int n);
    void     operator++(int) { Advance(1); }
    Utf8StringIterator operator+(int n) const;

    const char* m_PrevBegin;       // start of currently-decoded codepoint
    const char* m_NextScan;        // cursor passed to decode_next_unicode_character
    uint32_t    m_CurrentCodepoint; // 0 = end-of-string

private:
    const char* m_String;
    int         m_NumChars;
    const char* m_End;
};


}  // namespace Mortar
#endif // FN_ENGINE_RENDER_UTF8STRINGITERATOR_H
