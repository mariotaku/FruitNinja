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
#include "asset/DataStreamReader.h"
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
    // raw width/height from header (also stored in info.rawWidth/rawHeight; apparentWidth/Height = pixel dims)
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
// TODO: v1.6.1 ReadFormatInternal @0x0022c7d4 -- full decode: 0x7c-byte DDS_HEADER, LE-convert ~19 dwords,
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
// TODO: v1.6.1 ReadFormatInternal @0x0022bc6c -- allocate Tex3Data (0x4c bytes), read
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

#if !defined(__bada__) && !defined(FRUIT_PLATFORM_WII)
// ---------------------------------------------------------------------------
// Reader [web]: WebP -- Port specific: web compressed textures (libwebp).
//
// Has NO binary counterpart. Both host and web builds ship textures
// transcoded to WebP but stored inside .tex-named files
// (tools/assets/stage-assets.py, always transcodes; both non-bada builds
// read from the same staged dir -- see CMakeLists.txt fn_asset_staging). This
// reader detects the RIFF/WEBP magic (WebPGetInfo) and decodes to RGBA8888, so
// the existing texFmt=0x01 GL upload path handles it unchanged. Any real Tex1
// .tex (e.g. an unstaged/raw FruitNinjaBada/Data run) fails WebPGetInfo and
// falls through to the Tex1 reader. Excluded from the __bada__ cross-build (no
// libwebp there, and the real device never ships WebP-in-.tex) and from Wii
// (ships raw uncompressed Tex1 only, no libwebp dependency -- see
// stage-assets.py --wii raw copy-only mode).
// ---------------------------------------------------------------------------

// WebPData -- Tex1Data whose pixel buffer is OWNED (a WebPDecodeRGBA() heap
// allocation) and freed with WebPFree in its virtual dtor. Unlike the other
// readers (whose `pixels` point into the mapped, File-owned buffer), the decoded
// RGBA does not live in the file bytes, so this Data must own + free it. Deriving
// from Tex1Data means the Cache()/UploadTex1ToGL static_cast<Tex1Data*> and the
// texFmt=0x01 upload path work unchanged.
struct WebPData : public Tex1Data {
    void* m_OwnedPixels; // WebPDecodeRGBA() buffer; WebPFree'd in ~WebPData
    WebPData() : m_OwnedPixels(0) {}
    virtual ~WebPData();
};

// Returns null (inert) if data is not WebP (real Tex1 .tex), else a WebPData
// with texFmt=0x01 RGBA8888 and the decoded pixels.
TextureSourceData* ReadWebP(const void* data, unsigned long size);
#endif // !__bada__ && !FRUIT_PLATFORM_WII

} // namespace TextureFileFormat

// g_readers -- the reader registry @ 0x2cf8e8 in the binary (4 entries:
// [0]=Tex3, [1]=DDS, [2]=Tex2, [3]=Tex1). The host/web build prepends a
// port-specific WebP reader at index 0 (FN_TEXTURE_NUM_READERS == 5); the
// __bada__ cross-build and Wii (no libwebp, raw Tex1 only) keep the
// binary-exact 4-entry array so asm-verify sees no divergence.
#if defined(__bada__) || defined(FRUIT_PLATFORM_WII)
#  define FN_TEXTURE_NUM_READERS 4
#else
#  define FN_TEXTURE_NUM_READERS 5
#endif
extern TextureReadFn g_readers[FN_TEXTURE_NUM_READERS];

// ---------------------------------------------------------------------------
// TextureInfo Read<T> overloads -- deserialise texture format descriptors from
// a DataStreamReader byte stream.
// Binary: Mortar namespace (NOT inside TextureFileFormat namespace).
// Home TU: TextureFileFormat.cpp.
// ---------------------------------------------------------------------------

// Fixed N-element array read (general template, no body -- only <ChannelDescription,4u>
// instantiation exists in the binary; that specialisation is defined in TextureFileFormat.cpp).
// Binary v1.6.1 Read<TextureInfo::ChannelDescription, 4u> @0x0026bc94.
template<typename T, unsigned int N>
void Read(DataStreamReader& reader, T (&arr)[N]);

// ASM-spec v1.6.1 Read(DataStreamReader&, TextureInfo::ChannelDescription&) @0x0026bb0c
void Read(DataStreamReader& reader, TextureInfo::ChannelDescription& cd);

// ASM-spec v1.6.1 Read(DataStreamReader&, TextureInfo::Compression&) @0x0026bb84
void Read(DataStreamReader& reader, TextureInfo::Compression& c);

// ASM-spec v1.6.1 Read(DataStreamReader&, TextureInfo::NumberFormat&) @0x0026bb44
void Read(DataStreamReader& reader, TextureInfo::NumberFormat& nf);

// ASM-spec v1.6.1 Read(DataStreamReader&, TextureInfo::TextureType&) @0x0026bba4
void Read(DataStreamReader& reader, TextureInfo::TextureType& tt);

// ASM-spec v1.6.1 Read(DataStreamReader&, TextureInfo::PixelFormat&) @0x0026bbc0
// DIFFERS: binary's PixelFormat has named sub-fields; port keeps it an opaque
// data[12] blob, same 12-byte layout, nested as Mortar::TextureInfo::PixelFormat
// (see TextureSource.h) so this overload mangles against the binary's nested type.
void Read(DataStreamReader& reader, PixelFormat& pf);

// ASM-spec v1.6.1 Read(DataStreamReader&, TextureInfo::DataInfo&) @0x0026bbec
void Read(DataStreamReader& reader, TextureInfo::DataInfo& di);

} // namespace Mortar

#endif // FN_ENGINE_ASSET_TEXTURE_FILE_FORMAT_H
