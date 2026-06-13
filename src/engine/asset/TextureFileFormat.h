#ifndef FN_ENGINE_ASSET_TEXTURE_FILE_FORMAT_H
#define FN_ENGINE_ASSET_TEXTURE_FILE_FORMAT_H

// Mortar::TextureFileFormat -- 4-reader format registry + dispatcher.
//
// Binary: a 4-entry function-pointer array @ 0x2cf8e8 (g_readers), iterated by
// TextureLoader::LockLayers @0x00226ee8. Each entry is a ReadFormat function that
// inspects the file bytes, returns a heap TextureSource::Data* on success, null on
// format mismatch. The loop calls each reader in order; first non-null result wins.
//
// Reader order:
//   [0] Tex3   magic FourCC == 0x01584554 ("TEX\x01")  @0x0022bd7c  Data sz 0x4c
//   [1] DDS    size>=0x80 && u32@0 == 0x20534444       @0x0022cc04  Data sz 0x44
//   [2] Tex2   size>=0x11 && u16@+2 == 4               @0x0022baf8  Data sz 0x48
//   [3] Tex1   size>=0xd, byte[2]=fmt(0..0x11)         @0x0022b324  Data sz 0x48
//
// The Tex1 path is the only live reader in the shipped 1.5.1/1.6.1 asset packs.
// Tex2/DDS/Tex3 full decode branches carry // TODO: <addr> markers.
//
// MakeIntFormat -- helper that maps (channelClass, numberType, channelCount) to
// a PixelFormat. Used by Tex2 and DDS readers. Not used by the Tex1 live path
// (Tex1 maps fmt byte -> GL directly in the upload layer).

#include "asset/TextureSource.h"
#include <cstdint>

namespace Mortar {

// Function signature for each reader entry in g_readers[].
// Binary: Data* ReadFormat(void const* data, unsigned long size)
typedef TextureSourceData* (*TextureReadFn)(const void* data, unsigned long size);

namespace TextureFileFormat {

// ---------------------------------------------------------------------------
// Reader [3]: Tex1 -- 12-byte header format (the port's live path).
//
// Header layout (12 bytes):
//   byte[0]  wLog2   (width  = 1 << wLog2)
//   byte[1]  hLog2   (height = 1 << hLog2)
//   byte[2]  format  (see switch below; 0..0x11)
//   byte[3..11]  padding
// Pixel data: bytes[12..]
//
// Binary Tex1 reader @0x0022b324 (outer), @0x0022ad04 (inner).
// Accepted formats:
//   0x00 = RGB888, 0x01 = RGBA8888, 0x0f = RGBA5551,
//   0x10 = RGBA4444, 0x11 = RGB565,
//   0x0b..0x0e = PVRTC compressed (not usable on desktop GL, pass-through in Data).
// ---------------------------------------------------------------------------

// Tex1Format::Data -- parsed payload (Data sz 0x48 in binary; port: base + fields).
struct Tex1Data : public TextureSourceData {
    // format byte from the header (used by the GL upload layer to select glTexImage2D params)
    uint8_t  texFmt;
    // raw width/height from header (already stored in info.width/info.height too)
    uint8_t  wLog2;
    uint8_t  hLog2;

    Tex1Data() : texFmt(0), wLog2(0), hLog2(0) {}
};

// Binary @0x0022b324 / internal @0x0022ad04.
// Returns null if the buffer is too small or the format byte is out of range.
TextureSourceData* ReadTex1Format(const void* data, unsigned long size);

// ---------------------------------------------------------------------------
// Reader [2]: Tex2 -- binary @0x0022baf8 / internal @0x0022b404.
// Accept gate: size>=0x11 && u16@+2 == 4.
// Full decode: per-mip size accumulation, DataInfo fields, 12-byte PixelFormat
//   channel-mapping block from (fmt&0xf00)/(fmt&0xf) switches + MakeIntFormat.
// No Tex2 assets ship in 1.5.1/1.6.1 packs, but the decode is implemented.
// ---------------------------------------------------------------------------
TextureSourceData* ReadTex2Format(const void* data, unsigned long size);

// ---------------------------------------------------------------------------
// Reader [1]: DDS -- binary @0x0022cc04 / internal @0x0022c7d4.
// Accept gate: size>=0x80 && u32@0 LE == 0x20534444 ("DDS ").
// TODO: 0x0022c7d4 -- full decode: 0x7c-byte DDS_HEADER, LE-convert ~19 dwords,
//   pixelflags&4 -> compressed (FourCC DXT*); else channel-mapping from RGBA masks
//   (VerifyBitRun + sort<ChannelMapping>); dims from header w@+0xc, h@+8, mips@+0x14;
//   allocate DDSTextureData (0x44 bytes).
//   No DDS assets ship in 1.5.1/1.6.1 packs.
// ---------------------------------------------------------------------------
TextureSourceData* ReadDDSFormat(const void* data, unsigned long size);

// ---------------------------------------------------------------------------
// Reader [0]: Tex3 -- binary @0x0022bd7c / internal @0x0022bc6c.
// Accept gate: u32@0 == 0x01584554 ("TEX\x01").
// Static-init: FourCC copied from .rodata @0x0029ac00 into .bss @0x0034e3f4 by
//   thunk @0x0022be94. Bytes: 'T','E','X',0x01 (LE uint32 = 0x01584554).
// TODO: 0x0022bc6c -- ReadFormatInternal: allocate Tex3Data (0x4c bytes), read
//   TextureInfo fields (format/numLayersX/numLayersY via MakeIntFormat helpers),
//   read per-layer size table, accumulate layer-data offsets.
//   No Tex3 assets ship in 1.5.1/1.6.1 packs.
// ---------------------------------------------------------------------------
TextureSourceData* ReadTex3Format(const void* data, unsigned long size);

// The Tex3 FourCC: "TEX\x01" (LE u32 = 0x01584554).
// Binary static-init @0x0022be94 copies .rodata @0x0029ac00 -> .bss @0x0034e3f4.
// DIFFERS: original = DAT_0034e3f4 (static-init copy at runtime); port = compile-time const.
// The value is identical: bytes { 0x54, 0x45, 0x58, 0x01 }.
extern const uint32_t kTex3FourCC; // = 0x01584554

} // namespace TextureFileFormat

// g_readers[4] -- the 4-entry reader registry @ 0x2cf8e8 in the binary.
// Index order matches binary: [0]=Tex3, [1]=DDS, [2]=Tex2, [3]=Tex1.
extern TextureReadFn g_readers[4];

} // namespace Mortar

#endif // FN_ENGINE_ASSET_TEXTURE_FILE_FORMAT_H
