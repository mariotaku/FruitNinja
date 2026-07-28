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

// ASM-spec v1.6.1 Mortar::utf8::encode_unicode_character @ 0x0022dd7c
// RFC 2279 UTF-8 encoder. Writes 1-6 bytes into buf[*len_ptr..]; updates *len_ptr.
// Encoding ranges (from decompile @ 0x0022dd7c):
//   cp < 0x80:       1 byte  (ASCII passthrough)
//   cp < 0x800:      2 bytes (0xC0 lead)
//   cp < 0x10000:    3 bytes (0xE0 lead)
//   cp < 0x200000:   4 bytes (0xF0 lead)
//   cp < 0x4000000:  5 bytes (0xF8 lead)
//   cp < 0x80000000: 6 bytes (0xFC lead)
//   cp >= 0x80000000: no output
void encode_unicode_character(char* buf, int* len_ptr, uint32_t codepoint) {
    if (codepoint < 0x80u) {
        buf[(*len_ptr)++] = (char)codepoint;
    } else if (codepoint < 0x800u) {
        buf[(*len_ptr)++] = (char)(0xC0u | (codepoint >> 6));
        buf[(*len_ptr)++] = (char)(0x80u | (codepoint & 0x3Fu));
    } else if (codepoint < 0x10000u) {
        buf[(*len_ptr)++] = (char)(0xE0u | (codepoint >> 12));
        buf[(*len_ptr)++] = (char)(0x80u | ((codepoint >> 6) & 0x3Fu));
        buf[(*len_ptr)++] = (char)(0x80u | (codepoint & 0x3Fu));
    } else if (codepoint < 0x200000u) {
        buf[(*len_ptr)++] = (char)(0xF0u | (codepoint >> 18));
        buf[(*len_ptr)++] = (char)(0x80u | ((codepoint >> 12) & 0x3Fu));
        buf[(*len_ptr)++] = (char)(0x80u | ((codepoint >> 6) & 0x3Fu));
        buf[(*len_ptr)++] = (char)(0x80u | (codepoint & 0x3Fu));
    } else if (codepoint < 0x4000000u) {
        buf[(*len_ptr)++] = (char)(0xF8u | (codepoint >> 24));
        buf[(*len_ptr)++] = (char)(0x80u | ((codepoint >> 18) & 0x3Fu));
        buf[(*len_ptr)++] = (char)(0x80u | ((codepoint >> 12) & 0x3Fu));
        buf[(*len_ptr)++] = (char)(0x80u | ((codepoint >> 6) & 0x3Fu));
        buf[(*len_ptr)++] = (char)(0x80u | (codepoint & 0x3Fu));
    } else if (codepoint < 0x80000000u) {
        buf[(*len_ptr)++] = (char)(0xFCu | (codepoint >> 30));
        buf[(*len_ptr)++] = (char)(0x80u | ((codepoint >> 24) & 0x3Fu));
        buf[(*len_ptr)++] = (char)(0x80u | ((codepoint >> 18) & 0x3Fu));
        buf[(*len_ptr)++] = (char)(0x80u | ((codepoint >> 12) & 0x3Fu));
        buf[(*len_ptr)++] = (char)(0x80u | ((codepoint >> 6) & 0x3Fu));
        buf[(*len_ptr)++] = (char)(0x80u | (codepoint & 0x3Fu));
    }
    // codepoint >= 0x80000000: no output (binary: returns without writing)
}

} // namespace utf8

// Binary @ 0x00235a70 — stack-builds a Utf8StringProxy (binary @ 0x0021eb98),
// then calls _Init (binary @ 0x0021ec20) which extracts begin from the proxy's
// +0x04 field and seeds m_Cursor/m_Src from it, then calls Advance(1).
// Port: construct proxy to obtain begin, seed flat fields, Advance(1).
Utf8StringIterator::Utf8StringIterator(const char* str)
    : m_CurrentCodepoint(0)
    , m_Cursor(str)
    , m_Src(str)
    , m_Begin(str)
{
    // _Init (@ 0x0021ec20): if proxy's begin is null, codepoint=0 and return;
    // otherwise cursor=src=begin, Advance(1).
    if (str) {
        Advance(1);
    }
}

// Copy-ctor: copies all four fields verbatim.
Utf8StringIterator::Utf8StringIterator(const Utf8StringIterator& other)
    : m_CurrentCodepoint(other.m_CurrentCodepoint)
    , m_Cursor(other.m_Cursor)
    , m_Src(other.m_Src)
    , m_Begin(other.m_Begin)
{
}

// Binary @ 0x0021ebcc — Advance n steps through the UTF-8 string.
// if(cursor==0)return; loop: if(*cursor=='\0'){codepoint=0;return;}
// src=cursor; codepoint=decode_next(&src); cursor=src.
// ASM-spec v1.6.1 Mortar::Utf8StringIterator::Advance @ 0x0021ebcc
void Utf8StringIterator::Advance(int n) {
    if (m_Cursor == 0) return;
    for (int i = 0; i < n; i++) {
        if (*m_Cursor == '\0') {
            m_CurrentCodepoint = 0;
            return;
        }
        m_Src = m_Cursor;
        m_CurrentCodepoint = utf8::decode_next_unicode_character(&m_Src);
        m_Cursor = m_Src;
        if (m_CurrentCodepoint == 0) return;
    }
}

Utf8StringIterator Utf8StringIterator::operator+(int n) const {
    Utf8StringIterator copy(*this);
    copy.Advance(n);
    return copy;
}

// Binary @ 0x0021ec20 (_Init / Reset) — rewinds cursor to string start and
// re-primes the first codepoint.
// DIFFERS: binary re-seeds from a fresh Utf8StringProxy via _Init;
// port uses port-only m_Begin to avoid re-creating the proxy.
// v1.6.1 Utf8StringIterator @ 0x0021ec20
void Utf8StringIterator::Reset() {
    m_Cursor = m_Begin;
    m_Src    = m_Begin;
    Advance(1);
}

}  // namespace Mortar
