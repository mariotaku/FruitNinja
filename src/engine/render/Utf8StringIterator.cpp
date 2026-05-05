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
        // Malformed leading byte — skip it
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

Utf8StringIterator::Utf8StringIterator(const char* str)
    : m_PrevBegin(str)
    , m_NextScan(str)
    , m_CurrentCodepoint(0)
    , m_String(str)
    , m_NumChars(0)
    , m_End(str)
{
    // Walk once to count codepoints and find the end
    const char* scan = str;
    while (*scan) {
        const char* before = scan;
        uint32_t cp = utf8::decode_next_unicode_character(&scan);
        if (cp != 0) {
            m_NumChars++;
        }
        (void)before;
    }
    m_End = scan;
    m_NextScan = str;
    // Prime the iterator by advancing once
    Advance(1);
}

Utf8StringIterator::Utf8StringIterator(const Utf8StringIterator& other)
    : m_PrevBegin(other.m_PrevBegin)
    , m_NextScan(other.m_NextScan)
    , m_CurrentCodepoint(other.m_CurrentCodepoint)
    , m_String(other.m_String)
    , m_NumChars(other.m_NumChars)
    , m_End(other.m_End)
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

}  // namespace Mortar
