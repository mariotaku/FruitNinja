// Analysed: 2026-04-29T00:00
#include "render/Utf8StringIterator.h"

namespace Mortar {

namespace utf8 {

uint32_t decode_next_unicode_character(const char** cursor) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(*cursor);
    if (*p == 0) return 0;

    uint32_t cp;
    int extra;
    if (*p < 0x80) {
        cp = *p++;
        extra = 0;
    } else if ((*p & 0xE0) == 0xC0) {
        cp = *p++ & 0x1F;
        extra = 1;
    } else if ((*p & 0xF0) == 0xE0) {
        cp = *p++ & 0x0F;
        extra = 2;
    } else if ((*p & 0xF8) == 0xF0) {
        cp = *p++ & 0x07;
        extra = 3;
    } else if ((*p & 0xFC) == 0xF8) {
        cp = *p++ & 0x03;
        extra = 4;
    } else if ((*p & 0xFE) == 0xFC) {
        cp = *p++ & 0x01;
        extra = 5;
    } else {
        p++;
        *cursor = reinterpret_cast<const char*>(p);
        return 0xFFFD;
    }

    for (int i = 0; i < extra; i++) {
        if ((*p & 0xC0) != 0x80) {
            *cursor = reinterpret_cast<const char*>(p);
            return 0xFFFD;
        }
        cp = (cp << 6) | (*p++ & 0x3F);
    }

    *cursor = reinterpret_cast<const char*>(p);
    return cp;
}

} // namespace utf8

// Binary @ 0x0012fe00 — delegates to proxy base ctor then calls _Init (0x000f8514).
// Port: walk string once to set m_NumChars / m_End, prime with Advance(1).
Utf8StringIterator::Utf8StringIterator(const char* str)
    : Utf8StringProxy(str)
{
    // Walk once to count codepoints and locate end
    const char* scan = str;
    int count = 0;
    while (*scan) {
        uint32_t cp = utf8::decode_next_unicode_character(&scan);
        if (cp != 0) {
            count++;
        }
    }
    m_NumChars = static_cast<uint32_t>(count);
    m_End      = scan;
    m_NextScan = str;
    // Prime the iterator: decode first codepoint
    Advance(1);
}

// Binary @ 0x00160cbc
Utf8StringIterator::Utf8StringIterator(const Utf8StringIterator& other)
    : Utf8StringProxy(other)
{
}

void Utf8StringIterator::Advance(int n) {
    for (int i = 0; i < n; i++) {
        if (m_NextScan >= m_End) {
            m_CurrentCodepoint = 0;
            return;
        }
        m_PrevBegin = m_NextScan;
        m_CurrentCodepoint = utf8::decode_next_unicode_character(&m_NextScan);
        if (m_CurrentCodepoint == 0) return;
    }
}

Utf8StringIterator Utf8StringIterator::operator+(int n) const {
    Utf8StringIterator copy(*this);
    copy.Advance(n);
    return copy;
}

// Binary @ 0x0012fe00 area -- Reset(): rewind the iterator to the string start.
// m_PrevBegin holds the start of the first codepoint (set during ctor's Advance(1));
// restoring m_NextScan to it and re-decoding gives the same state as after the ctor.
void Utf8StringIterator::Reset() {
    m_NextScan = m_PrevBegin;
    Advance(1);
}

}  // namespace Mortar
