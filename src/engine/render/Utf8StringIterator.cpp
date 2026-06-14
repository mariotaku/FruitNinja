// Analysed: 2026-04-29T00:00
#include "render/Utf8StringIterator.h"

namespace Mortar {

namespace utf8 {

// RE-ported: 0x0022da04 — binary applies per-length overlong/surrogate/noncharacter
// validation after assembling cp; NUL continuation byte returns 0 not 0xFFFD.
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
        // Binary returns 0 (not 0xFFFD) when a continuation byte is NUL.
        if (*p == 0x00) {
            *cursor = reinterpret_cast<const char*>(p);
            return 0;
        }
        if ((*p & 0xC0) != 0x80) {
            *cursor = reinterpret_cast<const char*>(p);
            return 0xFFFD;
        }
        cp = (cp << 6) | (*p++ & 0x3F);
    }

    *cursor = reinterpret_cast<const char*>(p);

    // Per-length overlong / surrogate / noncharacter validation (binary @ 0x0022da04).
    switch (extra) {
        case 1:
            // 2-byte: overlong if cp <= 0x7F (binary: !(0x7e < cp) || cp==0x7f -> 0xFFFD)
            if (cp <= 0x7F) cp = 0xFFFD;
            break;
        case 2:
            // 3-byte: overlong if cp <= 0x7FF, surrogate if 0xD800..0xDFFF,
            //         noncharacter if cp == 0xFFFE or 0xFFFF.
            if (cp <= 0x7FF) { cp = 0xFFFD; break; }
            if (cp >= 0xD800 && cp <= 0xDFFF) { cp = 0xFFFD; break; }
            if (cp == 0xFFFE || cp == 0xFFFF) { cp = 0xFFFD; break; }
            break;
        case 3:
            // 4-byte: overlong if cp <= 0xFFFF.
            if (cp <= 0xFFFF) cp = 0xFFFD;
            break;
        case 4:
            // 5-byte: overlong if cp <= 0x1FFFFF.
            if (cp <= 0x1FFFFF) cp = 0xFFFD;
            break;
        case 5:
            // 6-byte: overlong if cp <= 0x3FFFFFF.
            if (cp <= 0x3FFFFFF) cp = 0xFFFD;
            break;
        default:
            break;
    }

    return cp;
}

} // namespace utf8

// Binary @ 0x0012fe00 — delegates to proxy base ctor then calls _Init (0x000f8514).
// Port: walk string once to set m_NumChars / m_End, then _Init: rewind and decode first codepoint.
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
    // _Init: rewind to immutable start, prime first codepoint.
    // m_Begin is already set to str by the proxy ctor and never changes.
    m_NextScan = m_Begin;
    Advance(1);
}

// ASM-verified: 2026-06-12 binary @ 0x00160cbc (re-analyst) -- iterator copy-ctor
// copies the cursor triple (proxy copy-ctor @0x001840c0 copies m_Begin/m_NumChars/m_End
// and zeroes these three).
Utf8StringIterator::Utf8StringIterator(const Utf8StringIterator& other)
    : Utf8StringProxy(other)
{
    m_PrevBegin        = other.m_PrevBegin;
    m_NextScan         = other.m_NextScan;
    m_CurrentCodepoint = other.m_CurrentCodepoint;
}

// Binary @ 0x0021ebcc — Advance: outer guard if (m_PrevBegin != 0); per-step guard
// if (*m_PrevBegin != '\0'); then m_PrevBegin = m_NextScan; m_CurrentCodepoint = decode(&m_NextScan).
// +0x04 (m_Begin) is NEVER written here.
// ASM-verified: 2026-06-12 binary @ 0x0021ebcc (asm-inspector)
void Utf8StringIterator::Advance(int n) {
    if (m_PrevBegin == 0) return;
    for (int i = 0; i < n; i++) {
        if (*m_NextScan == '\0') {
            m_CurrentCodepoint = 0;
            return;
        }
        m_PrevBegin = m_NextScan;                                               // +0x10 = old +0x14
        m_CurrentCodepoint = utf8::decode_next_unicode_character(&m_NextScan);  // +0x18 = decode(&+0x14)
        if (m_CurrentCodepoint == 0) return;
    }
}

Utf8StringIterator Utf8StringIterator::operator+(int n) const {
    Utf8StringIterator copy(*this);
    copy.Advance(n);
    return copy;
}

// Binary @ 0x0021ec20 (_Init / Reset) — rewinds to the IMMUTABLE string start (m_Begin = +0x04)
// and re-primes the first codepoint, matching binary _Init exactly.
// ASM-verified: 2026-06-12 binary @ 0x0021ebcc (asm-inspector)
void Utf8StringIterator::Reset() {
    m_NextScan = m_Begin;   // rewind live cursor to immutable string start (+0x04)
    Advance(1);             // prime first codepoint into m_PrevBegin/m_CurrentCodepoint
}

}  // namespace Mortar
