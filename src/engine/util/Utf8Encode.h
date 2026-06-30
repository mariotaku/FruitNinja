#ifndef FN_ENGINE_UTIL_UTF8ENCODE_H
#define FN_ENGINE_UTIL_UTF8ENCODE_H

#include <string>

// ASM-spec v1.6.1 EncodeUTF8FromUCS @ 0x0018ce0c:
//   Converts an array of UCS-4 codepoints to UTF-8, appending into out.
//   cp: array of count unsigned long codepoints (unsigned long = 4 bytes on ARM32).
//   Allocates a temporary buffer of (count*6+1) bytes, encodes each codepoint via
//   Mortar::utf8::encode_unicode_character, null-terminates, assigns to out, then frees.
void EncodeUTF8FromUCS(unsigned long* cp, unsigned long count, std::string& out);

#endif // FN_ENGINE_UTIL_UTF8ENCODE_H
