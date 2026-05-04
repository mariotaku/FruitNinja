// Analysed: 2026-05-04T00:00
#include "LZ8.h"
#include <cstring>

namespace Math {

// Binary @ 0x00195154
unsigned int GetUncompressedSizeLZ8(const void* src) {
    const unsigned char* p = (const unsigned char*)src;
    return (unsigned int)p[1] | ((unsigned int)p[2] << 8) | ((unsigned int)p[3] << 16);
}

// Binary @ 0x00195168 — LZSS (PSP/GBA style, magic 0x10)
// Header: byte[0]=0x10, byte[1..3]=uncompressed size (24-bit LE)
// Each block: 1 flag byte (MSB first), then 8 tokens.
//   flag bit=1 -> literal byte, copy to dst
//   flag bit=0 -> back-ref: 2 bytes BE, upper nibble=(matchLen-3), lower 12 bits=(disp+1)
void UncompressLZ8(const void* src, void* dst) {
    const unsigned char* in  = (const unsigned char*)src + 4; // skip 4-byte header
    unsigned char*       out = (unsigned char*)dst;
    unsigned int dstSize = GetUncompressedSizeLZ8(src);
    unsigned char* end = out + dstSize;

    while (out < end) {
        unsigned char flags = *in++;
        for (int bit = 7; bit >= 0 && out < end; --bit) {
            if (flags & (1 << bit)) {
                // Literal
                *out++ = *in++;
            } else {
                // Back-reference: 2 bytes big-endian
                unsigned char hi = *in++;
                unsigned char lo = *in++;
                int matchLen  = ((hi >> 4) & 0xF) + 3;
                int disp      = (((int)(hi & 0xF) << 8) | lo) + 1;
                const unsigned char* ref = out - disp;
                for (int i = 0; i < matchLen && out < end; ++i) {
                    *out++ = *ref++;
                }
            }
        }
    }
}

// Binary @ 0x0019500c — asset re-compression not needed at runtime
unsigned int CompressLZ(const unsigned char* /*src*/, unsigned long /*size*/, unsigned char* /*dst*/) {
    // Defunct: asset re-compression not needed at runtime; binary @ 0x0019500c
    return 0;
}

} // namespace Math
