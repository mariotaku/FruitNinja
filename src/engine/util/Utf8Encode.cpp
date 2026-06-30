#include "util/Utf8Encode.h"
#include "render/Utf8StringIterator.h"
#include <cstdint>

// ASM-spec v1.6.1 EncodeUTF8FromUCS @ 0x0018ce0c
// alloc buf[count*6+1]; for each cp[i]: encode_unicode_character(buf, &len, cp[i]);
// buf[len]='\0'; out=buf; delete[] buf.
// Relies on Mortar::utf8::encode_unicode_character @ 0x0022dd7c for per-codepoint encoding.
void EncodeUTF8FromUCS(unsigned long* cp, unsigned long count, std::string& out) {
    char* buf = new char[count * 6 + 1];
    int len = 0;
    for (unsigned long i = 0; i < count; i++) {
        Mortar::utf8::encode_unicode_character(buf, &len, (uint32_t)cp[i]);
    }
    buf[len] = '\0';
    out = buf;
    delete[] buf;
}
