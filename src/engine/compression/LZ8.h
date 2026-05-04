#ifndef FN_ENGINE_COMPRESSION_LZ8_H
#define FN_ENGINE_COMPRESSION_LZ8_H

namespace Math {

// Binary @ 0x00195154 — returns 24-bit LE decompressed size from bytes [1..3] of header
unsigned int GetUncompressedSizeLZ8(const void* src);

// Binary @ 0x00195168 — LZSS decompressor
// 4-byte header: magic byte 0x10 + 24-bit LE uncompressed size
// Processes 8-token blocks; flag byte bit 7..0 selects literal (1) vs back-ref (0)
// Back-ref token: 2 bytes big-endian — upper 4 bits = (matchLen-3), lower 12 bits = (offset-1)
void UncompressLZ8(const void* src, void* dst);

// Binary @ 0x0019500c — LZSS compressor; not needed at runtime
unsigned int CompressLZ(const unsigned char* src, unsigned long size, unsigned char* dst);

} // namespace Math

#endif
