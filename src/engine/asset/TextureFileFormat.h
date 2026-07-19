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
#if defined(FRUIT_PLATFORM_WII)
    // Port specific: native GX texture format of a GXT1 payload
    // (GX_TF_RGB565=4 / GX_TF_RGB5A3=5 / GX_TF_RGBA8=6) when texFmt ==
    // kGxtxTexFmt (the routing sentinel); 0 otherwise. Set by ReadGxtx from
    // header byte[8]; Texture.cpp passes it through to Wii_UploadTiledGX.
    uint8_t  gxNativeFmt;
#endif

    Tex1Data() : texFmt(0), wLog2(0), hLog2(0)
#if defined(FRUIT_PLATFORM_WII)
        , gxNativeFmt(0)
#endif
    {}
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
// (no libwebp dependency; game textures stay raw Tex1 and the WebP-only widget
// art is pre-tiled to the GXTX container instead -- see ReadGxtx below and
// stage-assets.py --wii).
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

#if defined(FRUIT_PLATFORM_WII)
// ---------------------------------------------------------------------------
// Reader [wii]: GXTX -- Port specific: pre-tiled native GX textures.
//
// Has NO binary counterpart. tools/assets/stage-assets.py --wii pre-tiles
// ALL Wii textures at STAGING time (tools/lib/gx_encoder.py encode_gxtx)
// into this self-describing container, still under the same .tex
// relpath/basename so LoadLocalisedTexture("box.tex") finds it:
//   - every transcodable Tex1 game texture, with a GX format chosen to
//     PRESERVE the source bit-depth (the MEM1 win -- no 32bpp expansion):
//     RGBA8888 -> GX_TF_RGBA8, RGB565/RGB888 -> GX_TF_RGB565,
//     RGBA5551/RGBA4444 -> GX_TF_RGB5A3 (gx_encoder.TEX1_TO_GX);
//   - the WebP-only UI widget art (assets/ui-widgets/generated/*.rgba
//     sidecars; the Wii has no WebP decoder -- ReadWebP is compiled out
//     above), always GX_TF_RGBA8.
// Non-transcodable .tex (Tex2/Tex3/DDS/PVRTC) stay verbatim and fall through
// to the raw readers. Big-endian (Wii native) 12-byte header:
//   offset 0:  char  magic[4] = "GXT1"   (cannot collide: Tex1 starts with
//              wLog2/hLog2 bytes, WebP with "RIFF", DDS with "DDS ",
//              Tex3 with "TEX\x01")
//   offset 4:  u16be width   (apparent texels)
//   offset 6:  u16be height
//   offset 8:  u8    gxFormat (GX_TF_RGB565=4 / GX_TF_RGB5A3=5 / GX_TF_RGBA8=6)
//   offset 9:  u8    version  = 1
//   offset 10: u16be reserved = 0
//   offset 12: tiled texels, ((w+3)/4)*((h+3)/4) * (64 bytes/tile for RGBA8,
//              32 for the 16bpp formats), in the exact GX hardware tile
//              layout (see gx_encoder.py's module docstring) -- the loader
//              uploads directly via Wii_UploadTiledGX, no runtime
//              decode/tile.
// The reader returns a Tex1Data with texFmt = kGxtxTexFmt (routing sentinel)
// + gxNativeFmt = header byte[8] (actual GX format), whose `pixels` point at
// the tiled bytes inside the file buffer (File-owned, valid for the
// LockLayers..UnlockLayers window, like the real Tex1 reader);
// UploadTex1ToGL (Texture.cpp) dispatches kGxtxTexFmt to
// Wii_UploadTiledGX(gxNativeFmt) instead of glTexImage2D.
// ---------------------------------------------------------------------------

// Sentinel texFmt for GXTX payloads. Real Tex1 format bytes are 0x00..0x11;
// this value is port-side only and never appears in on-disk Tex1 headers.
// The actual GX format travels separately in Tex1Data::gxNativeFmt.
const uint8_t kGxtxTexFmt = 0xF0;

// Returns null (inert) if data is not a GXT1 container (or its gxFormat byte
// isn't one of 4/5/6), else a Tex1Data with texFmt=kGxtxTexFmt, gxNativeFmt
// from the header, apparent dims from the header, and pixels/pixelsSize
// referencing the pre-tiled texel bytes at offset 12.
TextureSourceData* ReadGxtx(const void* data, unsigned long size);
#endif // FRUIT_PLATFORM_WII

} // namespace TextureFileFormat

// g_readers -- the reader registry @ 0x2cf8e8 in the binary (4 entries:
// [0]=Tex3, [1]=DDS, [2]=Tex2, [3]=Tex1). The host/web build prepends a
// port-specific WebP reader at index 0; Wii (no libwebp) instead prepends the
// GXTX pre-tiled native-GX reader (FN_TEXTURE_NUM_READERS == 5 either way); the
// __bada__ cross-build keeps the binary-exact 4-entry array so asm-verify
// sees no divergence.
#if defined(__bada__)
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
