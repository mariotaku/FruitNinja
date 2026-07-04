// Analysed: 2026-05-04T00:00
#include "LZ8.h"
#include <cstring>

namespace Math {

// v1.6.1 Math::GetUncompressedSizeLZ8 @0x00242644
unsigned int GetUncompressedSizeLZ8(const void* src) {
    const unsigned char* p = (const unsigned char*)src;
    return (unsigned int)p[1] | ((unsigned int)p[2] << 8) | ((unsigned int)p[3] << 16);
}

// v1.6.1 Math::UncompressLZ8 @0x00242660 — LZSS decompressor
// Header: byte[0]=0x10, byte[1..3]=uncompressed size (24-bit LE)
// Each block: 1 flag byte (MSB first), then 8 tokens.
//   flag bit==0 -> literal byte, copy to dst
//   flag bit==1 -> back-ref: 2 bytes, hi(full 8 bits)<<4 | lo>>4 = (disp-1), lo&0xF = (matchLen-3)
// NOTE: no live caller found -- fix is fidelity-only, no runtime asset exercises this path.
void UncompressLZ8(const void* src, void* dst) {
    const unsigned char* in  = (const unsigned char*)src + 4; // skip 4-byte header
    unsigned char*       out = (unsigned char*)dst;
    unsigned int dstSize = GetUncompressedSizeLZ8(src);
    unsigned char* end = out + dstSize;

    while (out < end) {
        unsigned char flags = *in++;
        for (int bit = 7; bit >= 0 && out < end; --bit) {
            if ((flags & (1 << bit)) == 0) {
                // Literal -- v1.6.1: control bit==0 means literal (verified via disasm tst/bne at 0x242748/0x242754)
                *out++ = *in++;
            } else {
                // Back-reference -- control bit==1
                unsigned char hi = *in++;   // read_ptr[0]
                unsigned char lo = *in++;   // read_ptr[1]
                int matchLen = (lo & 0xF) + 3;                   // length code = LOW nibble of the SECOND byte
                int disp     = (((int)hi << 4) | (lo >> 4)) + 1;  // offset-1 = hi(full 8 bits)<<4 | high-nibble(lo)
                const unsigned char* ref = out - disp;
                for (int i = 0; i < matchLen && out < end; ++i) {
                    *out++ = *ref++;
                }
            }
        }
    }
}

// v1.6.1 Math::CompressLZ @0x00242420 — asset re-compression not needed at runtime
unsigned int CompressLZ(const unsigned char* /*src*/, unsigned long /*size*/, unsigned char* /*dst*/) {
    // Defunct: asset re-compression not needed at runtime; v1.6.1 Math::CompressLZ @0x00242420
    return 0;
}

} // namespace Math
