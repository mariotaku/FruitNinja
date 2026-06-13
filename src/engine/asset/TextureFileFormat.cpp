// Mortar::TextureFileFormat -- 4-reader format registry + reader implementations.
//
// Live path: Tex1 (ReadTex1Format). Tex2/DDS/Tex3 full decode are TODO;
// their accept-gate checks are implemented (reject-path) so the registry
// loop falls through correctly to Tex1 for all shipped assets.
//
// Binary reader registry @ 0x2cf8e8 (array of 4 ReadFn pointers):
//   [0] = ReadTex3Format @0x0022bd7c
//   [1] = ReadDDSFormat  @0x0022cc04
//   [2] = ReadTex2Format @0x0022baf8
//   [3] = ReadTex1Format @0x0022b324

#include "asset/TextureFileFormat.h"
#include <cstdint>
#include <cstring>
#include <cstdlib>

namespace Mortar {

// ---- Tex3 FourCC ----------------------------------------------------------
// Static-init in binary: _GLOBAL__I_Tex3Format.cpp @ 0x0022be94 copies
// .rodata @0x0029ac00 (bytes 54 45 58 01) into .bss @0x0034e3f4.
// Port: compile-time constant; value identical.
// DIFFERS: original = DAT_0034e3f4 (static-init copy); port = const literal.
const uint32_t TextureFileFormat::kTex3FourCC = 0x01584554u; // "TEX\x01" LE

// ---- Reader [3]: Tex1 -----------------------------------------------------
// Binary @0x0022b324 (outer dispatch) / @0x0022ad04 (inner ReadFormatInternal).
// The outer checks size >= 0xd and byte[2] in range 0..0x11;
// the inner allocates Tex1Data (0x48 bytes in binary), fills DataInfo, stores pixel ptr.
//
// Tex1 header (12 bytes):
//   [0] wLog2   width  = 1<<wLog2
//   [1] hLog2   height = 1<<hLog2
//   [2] fmt     format index (0..0x11)
//   [3..11]     padding / reserved
// Pixel data: bytes[12..size-1]
//
// Accepted format bytes + expected bpp (bits-per-pixel):
//   0x00 = RGB888    (24 bpp)
//   0x01 = RGBA8888  (32 bpp)
//   0x02 = ?         (binary switch has case, bpp unknown; not in shipped packs)
//   ...
//   0x0b..0x0e = PVRTC compressed (skip size validation in binary; not decoded here)
//   0x0f = RGBA5551  (16 bpp)
//   0x10 = RGBA4444  (16 bpp)
//   0x11 = RGB565    (16 bpp)
//
// Size guard (for non-PVRTC formats):
//   expected = 12 + (((bpp << wLog2) + 7) >> 3) << hLog2
//
// Binary fmt 0x00..0x11 bpp table (from Tex1 switch):
//   0x00=24, 0x01=32, 0x02=24, 0x03=32, 0x04=8, 0x05=16, 0x06=8,
//   0x07=16, 0x08=8, 0x09=4, 0x0a=4, 0x0b=PVRTC, 0x0c=PVRTC,
//   0x0d=PVRTC, 0x0e=PVRTC, 0x0f=16, 0x10=16, 0x11=16

static const unsigned int kTex1BppTable[0x12] = {
    24, // 0x00 RGB888
    32, // 0x01 RGBA8888
    24, // 0x02 (unknown; same channel count as RGB888)
    32, // 0x03 (unknown)
     8, // 0x04
    16, // 0x05
     8, // 0x06
    16, // 0x07
     8, // 0x08
     4, // 0x09
     4, // 0x0a
     0, // 0x0b PVRTC (skip size check)
     0, // 0x0c PVRTC
     0, // 0x0d PVRTC
     0, // 0x0e PVRTC
    16, // 0x0f RGBA5551
    16, // 0x10 RGBA4444
    16  // 0x11 RGB565
};

TextureSourceData* TextureFileFormat::ReadTex1Format(const void* data, unsigned long size) {
    if (size < 0xd) {
        return 0;
    }
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint8_t wLog2  = bytes[0];
    uint8_t hLog2  = bytes[1];
    uint8_t fmt    = bytes[2];

    if (fmt > 0x11) {
        return 0;
    }

    // Size validation for non-PVRTC formats.
    bool isPVRTC = (fmt >= 0x0b && fmt <= 0x0e);
    if (!isPVRTC) {
        unsigned int bpp = kTex1BppTable[fmt];
        unsigned long rowBytes   = ((((unsigned long)bpp << wLog2) + 7u) >> 3u);
        unsigned long pixBytes   = rowBytes << hLog2;
        unsigned long expected   = 12u + pixBytes;
        if (size < expected) {
            return 0;
        }
    }

    Tex1Data* d = new Tex1Data();
    d->texFmt = fmt;
    d->wLog2  = wLog2;
    d->hLog2  = hLog2;

    d->info.width      = (uint16_t)(1u << wLog2);
    d->info.height     = (uint16_t)(1u << hLog2);
    d->info.depth      = 1;
    d->info.arraySize  = 1;
    d->info.dataSize   = (uint32_t)(size - 12u);
    d->info.slicePitch = 0;

    d->pixels     = bytes + 12;
    d->pixelsSize = size - 12u;

    return d;
}

// ---- Reader [2]: Tex2 -----------------------------------------------------
// Binary @0x0022baf8 (outer) / @0x0022b404 (inner).
// Accept gate: size >= 0x11 && u16@+2 == 4.
// TODO: 0x0022b404 -- full Tex2 decode (no Tex2 assets in shipped packs).
TextureSourceData* TextureFileFormat::ReadTex2Format(const void* data, unsigned long size) {
    if (size < 0x11) {
        return 0;
    }
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint16_t gate;
    memcpy(&gate, bytes + 2, 2);
    if (gate != 4) {
        return 0;
    }
    // TODO: 0x0022b404 -- full Tex2 decode: wLog2=bytes[8], hLog2=bytes[9],
    //   mipCount=bytes[0xb], format=(u32@+4 & 0xf) via MakeIntFormat switch;
    //   validate total size via per-mip accumulation; allocate Tex2Data (0x48B).
    //   No Tex2 assets in shipped 1.5.1/1.6.1 packs.
    return 0;
}

// ---- Reader [1]: DDS ------------------------------------------------------
// Binary @0x0022cc04 (outer) / @0x0022c7d4 (inner).
// Accept gate: size >= 0x80 && u32@0 LE == 0x20534444 ("DDS ").
// TODO: 0x0022c7d4 -- full DDS decode (no DDS assets in shipped packs).
TextureSourceData* TextureFileFormat::ReadDDSFormat(const void* data, unsigned long size) {
    if (size < 0x80) {
        return 0;
    }
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint32_t magic;
    memcpy(&magic, bytes, 4);
    if (magic != 0x20534444u) { // "DDS " LE
        return 0;
    }
    // TODO: 0x0022c7d4 -- full DDS decode: 0x7c-byte DDS_HEADER, LE-convert ~19 dwords,
    //   pixelflags&4 -> compressed (FourCC DXT*); else channel-mapping from RGBA masks
    //   (VerifyBitRun + sort<ChannelMapping>); dims from header w@+0xc, h@+8, mips@+0x14;
    //   allocate DDSTextureData (0x44 bytes).
    //   No DDS assets in shipped 1.5.1/1.6.1 packs.
    return 0;
}

// ---- Reader [0]: Tex3 -----------------------------------------------------
// Binary @0x0022bd7c (outer) / @0x0022bc6c (internal ReadFormatInternal).
// Accept gate: u32@0 == kTex3FourCC (0x01584554 = "TEX\x01").
// FourCC static-init: _GLOBAL__I_Tex3Format.cpp @0x0022be94 copies
//   .rodata @0x0029ac00 -> .bss @0x0034e3f4.
// TODO: 0x0022bc6c -- ReadFormatInternal: allocate Tex3Data (0x4c bytes), read
//   TextureInfo fields via MakeIntFormat helpers, read per-layer size table,
//   accumulate layer-data offsets.
//   No Tex3 assets in shipped 1.5.1/1.6.1 packs.
TextureSourceData* TextureFileFormat::ReadTex3Format(const void* data, unsigned long size) {
    if (size < 4) {
        return 0;
    }
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint32_t magic;
    memcpy(&magic, bytes, 4);
    if (magic != kTex3FourCC) {
        return 0;
    }
    // TODO: 0x0022bc6c -- full Tex3 decode (no Tex3 assets in shipped packs).
    return 0;
}

// ---- g_readers registry ---------------------------------------------------
// Binary: 4-entry array @ 0x2cf8e8.
// Order: [0]=Tex3, [1]=DDS, [2]=Tex2, [3]=Tex1.
TextureReadFn g_readers[4] = {
    TextureFileFormat::ReadTex3Format,  // [0] @0x0022bd7c
    TextureFileFormat::ReadDDSFormat,   // [1] @0x0022cc04
    TextureFileFormat::ReadTex2Format,  // [2] @0x0022baf8
    TextureFileFormat::ReadTex1Format   // [3] @0x0022b324
};

} // namespace Mortar
