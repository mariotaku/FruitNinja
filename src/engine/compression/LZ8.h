#ifndef FN_ENGINE_COMPRESSION_LZ8_H
#define FN_ENGINE_COMPRESSION_LZ8_H

namespace Math {

// v1.6.1 Math::GetUncompressedSizeLZ8 @0x00242644 — returns 24-bit LE decompressed size from bytes [1..3] of header
unsigned int GetUncompressedSizeLZ8(const void* src);

// v1.6.1 Math::UncompressLZ8 @0x00242660 — LZSS decompressor
// 4-byte header: magic byte 0x10 + 24-bit LE uncompressed size
// Processes 8-token blocks; flag byte bit 7..0 selects literal (bit==0) vs back-ref (bit==1)
// Back-ref token: byte0(hi) full value forms bits[11:4] of (disp-1), byte1(lo) upper nibble
// forms bits[3:0] of (disp-1), byte1 lower nibble = matchLen-3
void UncompressLZ8(const void* src, void* dst);

// Binary @ 0x0019500c — LZSS compressor; not needed at runtime
unsigned int CompressLZ(const unsigned char* src, unsigned long size, unsigned char* dst);

} // namespace Math

#endif
