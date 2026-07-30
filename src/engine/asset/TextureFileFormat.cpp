// Mortar::TextureFileFormat -- 4-reader format registry + reader implementations.
//
// Live path: Tex1 (ReadTex1Format). Tex2 full decode is implemented.
// DDS/Tex3 full decode are TODO; their accept-gate checks are implemented
// (reject-path) so the registry loop falls through correctly to Tex1 for
// all shipped assets.
//
// Binary reader registry @ 0x2cf8e8 (array of 4 ReadFn pointers):
//   [0] = ReadTex3Format v1.6.1 Tex3Format::Read @0x0022bd7c
//   [1] = ReadDDSFormat  v1.6.1 DDSFormat::Read @0x0022cc04
//   [2] = ReadTex2Format v1.6.1 Tex2Format::Read @0x0022baf8
//   [3] = ReadTex1Format v1.6.1 Tex1Format::Read @0x0022b324

#include "asset/TextureFileFormat.h"
#include "util/Endian.h"
#include <cstdint>
#include <cstring>
#include <cstdlib>

#if !defined(__bada__) && !defined(FRUIT_PLATFORM_WII)
#include "webp/decode.h" // Port specific: web compressed textures (libwebp)
#endif

namespace Mortar {

// ---- Tex3 FourCC ----------------------------------------------------------
// Static-init in binary: v1.6.1 _GLOBAL__I_Tex3Format.cpp @0x0022be94 copies
// .rodata @0x0029ac00 (bytes 54 45 58 01) into .bss @0x0034e3f4.
// Port: compile-time constant; value identical.
// DIFFERS: original = DAT_0034e3f4 (static-init copy); port = const literal.
const uint32_t TextureFileFormat::kTex3FourCC = 0x01584554u; // "TEX\x01" LE

// ---- Reader [3]: Tex1 -----------------------------------------------------
// Binary v1.6.1 Tex1Format::Read @0x0022b324 (outer dispatch) / Tex1Format::ReadFormatInternal @0x0022ad04 (inner).
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

    d->info.rawWidth       = (uint16_t)(1u << wLog2);
    d->info.rawHeight      = (uint16_t)(1u << hLog2);
    d->info.depth          = 1;
    d->info.levels         = 1;
    d->info.apparentWidth  = (uint32_t)(1u << wLog2);
    d->info.apparentHeight = (uint32_t)(1u << hLog2);

    d->pixels     = bytes + 12;
    d->pixelsSize = size - 12u;

    return d;
}

// ---- Reader [2]: Tex2 -----------------------------------------------------
// Binary v1.6.1 Tex2Format::Read @0x0022baf8 (outer) / Tex2Format::ReadFormatInternal @0x0022b404 (inner).
// Accept gate: size >= 0x11 && u16@+2 == 4.
// Header: bytes[8]=wLog2, bytes[9]=hLog2, bytes[0xb]=mipCount; u32@+4 = format
//   descriptor; u16@+0xc = apparentWidth; u16@+0xe = apparentHeight.
// Validation: max(wLog2,hLog2)+1 >= mipCount, and per-mip byte accumulation
//   (sum of (bpp*w*h)>>3 over the mip chain) + 0x10 == size.
// PixelFormat (12-byte channel-mapping block) is built byte-for-byte from the
// two switches over (u32@+4 & 0xf00) (type byte pair, pf[0]/pf[1]) and
// (u32@+4 & 0xf) (channel layout, pf[4..0xb] + NumberType -> MakeIntFormat).
// v1.6.1 MakeIntFormat @0x0022b3dc: ret = (((n-1)*0x10)&0xff)<<8  -> pf[2]/pf[3].
// Binary zeroes the pixel-ptr fields (piVar3[9..0x11]); pixels/pixelsSize stay 0.
// No Tex2 assets ship in 1.5.1/1.6.1 packs, but the decode is faithful per policy.
TextureSourceData* TextureFileFormat::ReadTex2Format(const void* data, unsigned long size) {
    if (size < 0x11) {
        return 0;
    }
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint16_t gate;
    memcpy(&gate, bytes + 2, 2);
#if defined(FN_BIG_ENDIAN)
    gate = Endian::fnByteSwap16(gate);
#endif
    if (gate != 4) {
        return 0;
    }

    uint8_t wLog2    = bytes[8];
    uint8_t hLog2    = bytes[9];
    uint8_t mipCount = bytes[0xb];

    unsigned int maxLog = (wLog2 < hLog2) ? (unsigned int)hLog2 : (unsigned int)wLog2;
    if ((maxLog + 1u) < (unsigned int)mipCount) { // binary: blt -> reject
        return 0;
    }

    uint32_t fmt;
    memcpy(&fmt, bytes + 4, 4);
#if defined(FN_BIG_ENDIAN)
    fmt = Endian::fnByteSwap32(fmt);
#endif

    // bits-per-pixel from the format descriptor.
    unsigned int hi = fmt & 0xf00u;
    unsigned int bpp;
    if (hi == 0x200u) {
        bpp = 2;
    } else if (hi == 0x300u) {
        bpp = 4;
    } else if (hi == 0x100u) {
        unsigned int lo = fmt & 0xfu;
        if (lo > 0xbu) {
            bpp = 2;
        } else {
            unsigned int bit = 1u << lo;
            if (bit & 0x8b8u)      bpp = 16;
            else if (bit & 0x42u)  bpp = 24;
            else if (bit & 0x204u) bpp = 32;
            else                   bpp = 2;
        }
    } else {
        return 0;
    }

    // Per-mip size accumulation.
    unsigned int w = 1u << wLog2;
    unsigned int h = 1u << hLog2;
    unsigned int total = 0;
    unsigned int m;
    for (m = (unsigned int)mipCount; m != 0; --m) {
        unsigned int area = w * h;
        if (w > 1u) w >>= 1;
        if (h > 1u) h >>= 1;
        total += (unsigned int)(bpp * area) >> 3;
    }
    if (total + 0x10u != (unsigned int)size) {
        return 0;
    }

    TextureSourceData* d = new TextureSourceData();
    // Binary zeroes piVar3[9..0x11]; pixels/pixelsSize remain 0 (base ctor).

    // ---- Scalar DataInfo fields ----
    d->info.numberFormat = 0;
    d->info.rawWidth     = (uint16_t)(1u << wLog2);
    d->info.rawHeight    = (uint16_t)(1u << hLog2);
    d->info.depth        = 1;
    d->info.levels       = 1;
    {
        uint16_t v;
        memcpy(&v, bytes + 0xc, 2);
#if defined(FN_BIG_ENDIAN)
        v = Endian::fnByteSwap16(v);
#endif
        d->info.apparentWidth  = v;
        memcpy(&v, bytes + 0xe, 2);
#if defined(FN_BIG_ENDIAN)
        v = Endian::fnByteSwap16(v);
#endif
        d->info.apparentHeight = v;
    }

    // ---- PixelFormat (12-byte channel-mapping block) ----
    uint8_t* pf = d->info.pixelFormat.data;
    int i;
    for (i = 0; i < 12; ++i) pf[i] = 0;

    // Type byte pair (pf[0], pf[1]) from (fmt & 0xf00).
    switch (hi) {
        case 0x100u: pf[0] = 0;    pf[1] = 0;    break;
        case 0x200u: pf[0] = 2;    pf[1] = 2;    break;
        case 0x300u: pf[0] = 2;    pf[1] = 4;    break;
        case 0x400u: pf[0] = 4;    pf[1] = 0x53; break;
        case 0x500u: pf[0] = 4;    pf[1] = 0x35; break;
        case 0x600u: pf[0] = 4;    pf[1] = 8;    break;
        case 0x700u: pf[0] = 4;    pf[1] = 4;    break;
        case 0x800u: pf[0] = 4;    pf[1] = 2;    break;
        default: break; // leaves pf[0]=pf[1]=0
    }

    // Channel mapping (pf[4..0xb]) + NumberType, from (fmt & 0xf) switch.
    unsigned int chan = fmt & 0xfu;
    int numType = 0; // 0 = default/skip (no MakeIntFormat, pf[2]=pf[3]=0)
    switch (chan) {
        case 1:  pf[4]=0x08;pf[5]=0x02;pf[6]=0x08;pf[7]=0x03; pf[8]=0x08;pf[9]=0x04;pf[10]=0x00;pf[11]=0x00; numType=1; break;
        case 2:  pf[4]=0x08;pf[5]=0x01;pf[6]=0x08;pf[7]=0x04; pf[8]=0x08;pf[9]=0x03;pf[10]=0x08;pf[11]=0x02; numType=4; break;
        case 3:  pf[4]=0x05;pf[5]=0x02;pf[6]=0x05;pf[7]=0x03; pf[8]=0x05;pf[9]=0x04;pf[10]=0x01;pf[11]=0x01; numType=2; break;
        case 4:  pf[4]=0x04;pf[5]=0x02;pf[6]=0x04;pf[7]=0x03; pf[8]=0x04;pf[9]=0x04;pf[10]=0x04;pf[11]=0x01; numType=2; break;
        case 5:  pf[4]=0x05;pf[5]=0x02;pf[6]=0x06;pf[7]=0x03; pf[8]=0x05;pf[9]=0x04;pf[10]=0x00;pf[11]=0x00; numType=2; break;
        case 6:  pf[4]=0x08;pf[5]=0x04;pf[6]=0x08;pf[7]=0x03; pf[8]=0x08;pf[9]=0x02;pf[10]=0x00;pf[11]=0x00; numType=1; break;
        case 7:  pf[4]=0x01;pf[5]=0x01;pf[6]=0x05;pf[7]=0x02; pf[8]=0x05;pf[9]=0x03;pf[10]=0x05;pf[11]=0x04; numType=2; break;
        case 8:  pf[4]=0x08;pf[5]=0x01;pf[6]=0x08;pf[7]=0x02; pf[8]=0x08;pf[9]=0x03;pf[10]=0x08;pf[11]=0x04; numType=4; break;
        case 9:  pf[4]=0x08;pf[5]=0x00;pf[6]=0x08;pf[7]=0x02; pf[8]=0x08;pf[9]=0x03;pf[10]=0x08;pf[11]=0x04; numType=4; break;
        case 10: pf[4]=0x05;pf[5]=0x02;pf[6]=0x05;pf[7]=0x03; pf[8]=0x05;pf[9]=0x04;pf[10]=0x01;pf[11]=0x00; numType=2; break;
        case 11: pf[4]=0x04;pf[5]=0x01;pf[6]=0x04;pf[7]=0x02; pf[8]=0x04;pf[9]=0x03;pf[10]=0x04;pf[11]=0x04; numType=2; break;
        default: break; // chan==0 or chan>0xb: skip; pf[2..0xb] stay 0
    }

    if (numType != 0) {
        // v1.6.1 MakeIntFormat @0x0022b3dc: (((n-1)*0x10)&0xff)<<8
        unsigned int makeInt = ((((unsigned int)(numType - 1)) * 0x10u) & 0xffu) << 8;
        pf[2] = (uint8_t)(makeInt & 0xffu);         // always 0
        pf[3] = (uint8_t)((makeInt >> 8) & 0xffu);  // ((n-1)*0x10)&0xff
    }

    return d;
}

// ---- Reader [1]: DDS ------------------------------------------------------
// Binary v1.6.1 DDSFormat::Read @0x0022cc04 (outer) / DDSFormat::ReadFormatInternal @0x0022c7d4 (inner).
// Accept gate: size >= 0x80 && u32@0 LE == 0x20534444 ("DDS ").
// TODO: v1.6.1 0x0022c7d4 (DDSFormat::ReadFormatInternal) -- full DDS decode (no DDS assets in shipped packs).
TextureSourceData* TextureFileFormat::ReadDDSFormat(const void* data, unsigned long size) {
    if (size < 0x80) {
        return 0;
    }
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint32_t magic;
    memcpy(&magic, bytes, 4);
#if defined(FN_BIG_ENDIAN)
    magic = Endian::fnByteSwap32(magic);
#endif
    if (magic != 0x20534444u) { // "DDS " LE
        return 0;
    }
    // TODO: v1.6.1 0x0022c7d4 (DDSFormat::ReadFormatInternal) -- full DDS decode: 0x7c-byte DDS_HEADER, LE-convert ~19 dwords,
    //   pixelflags&4 -> compressed (FourCC DXT*); else channel-mapping from RGBA masks
    //   (VerifyBitRun + sort<ChannelMapping>); dims from header w@+0xc, h@+8, mips@+0x14;
    //   allocate DDSTextureData (0x44 bytes).
    //   No DDS assets in shipped 1.5.1/1.6.1 packs.
    return 0;
}

// ---- Reader [0]: Tex3 -----------------------------------------------------
// Binary v1.6.1 Tex3Format::Read @0x0022bd7c (outer) / Tex3Format::ReadFormatInternal @0x0022bc6c (inner).
// Accept gate: u32@0 == kTex3FourCC (0x01584554 = "TEX\x01").
// FourCC static-init: v1.6.1 _GLOBAL__I_Tex3Format.cpp @0x0022be94 copies
//   .rodata @0x0029ac00 -> .bss @0x0034e3f4.
// TODO: v1.6.1 0x0022bc6c (Tex3Format::ReadFormatInternal) -- allocate Tex3Data (0x4c bytes), read
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
#if defined(FN_BIG_ENDIAN)
    magic = Endian::fnByteSwap32(magic);
#endif
    if (magic != kTex3FourCC) {
        return 0;
    }
    // TODO: v1.6.1 0x0022bc6c (Tex3Format::ReadFormatInternal) -- full Tex3 decode (no Tex3 assets in shipped packs).
    return 0;
}

#if !defined(__bada__) && !defined(FRUIT_PLATFORM_WII)
// ---- Reader [web]: WebP ---------------------------------------------------
// Port specific: web compressed textures (libwebp). No binary counterpart.
// Web textures are transcoded to WebP and stored inside .tex-named files; this
// reader decodes them. On desktop the reader is present but never fires (real
// Tex1 .tex fail WebPGetInfo). See TextureFileFormat.h for the ownership note.
TextureFileFormat::WebPData::~WebPData() {
    if (m_OwnedPixels) {
        WebPFree(m_OwnedPixels);
        m_OwnedPixels = 0;
    }
}

TextureSourceData* TextureFileFormat::ReadWebP(const void* data, unsigned long size) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    int w = 0;
    int h = 0;
    // Inert on non-WebP input (real Tex1 .tex): WebPGetInfo returns 0.
    if (WebPGetInfo(bytes, (size_t)size, &w, &h) == 0) {
        return 0;
    }
    uint8_t* rgba = WebPDecodeRGBA(bytes, (size_t)size, &w, &h);
    if (!rgba) {
        return 0;
    }

    WebPData* d = new WebPData();
    d->m_OwnedPixels = rgba;           // freed by ~WebPData -> WebPFree
    d->texFmt = 0x01;                  // RGBA8888 -> existing GL upload case 0x01
    d->wLog2  = 0;                     // dims are non-log2; apparent dims are authoritative
    d->hLog2  = 0;

    d->info.rawWidth       = (uint16_t)w;
    d->info.rawHeight      = (uint16_t)h;
    d->info.depth          = 1;
    d->info.levels         = 1;
    d->info.apparentWidth  = (uint32_t)w;
    d->info.apparentHeight = (uint32_t)h;

    d->pixels     = rgba;
    d->pixelsSize = (unsigned long)w * (unsigned long)h * 4u;

    return d;
}
#endif // !__bada__ && !FRUIT_PLATFORM_WII

#if defined(FRUIT_PLATFORM_WII)
// ---- Reader [wii]: GXTX ---------------------------------------------------
// Port specific: pre-tiled native GX textures (no binary counterpart).
// stage-assets.py --wii pre-tiles every transcodable Tex1 game texture
// (bit-depth-preserving GX format) plus the WebP-only UI widget art into
// this "GXT1" container at staging time; the upload skips runtime
// decode/tiling (Wii_UploadTiledGX). Byte layout + collision argument
// documented in TextureFileFormat.h. Inert on any other content (magic
// mismatch).
TextureSourceData* TextureFileFormat::ReadGxtx(const void* data, unsigned long size) {
    if (size < 12) {
        return 0;
    }
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    if (bytes[0] != 'G' || bytes[1] != 'X' || bytes[2] != 'T' || bytes[3] != '1') {
        return 0;
    }
    uint8_t gxFmt = bytes[8]; // GX_TF_RGB565=4 / GX_TF_RGB5A3=5 / GX_TF_RGBA8=6
    if ((gxFmt != 4 && gxFmt != 5 && gxFmt != 6) || bytes[9] != 1 /*version*/) {
        return 0;
    }
    // Header fields are explicit big-endian (Wii native).
    uint32_t w = ((uint32_t)bytes[4] << 8) | bytes[5];
    uint32_t h = ((uint32_t)bytes[6] << 8) | bytes[7];
    if (w == 0 || h == 0) {
        return 0;
    }

    Tex1Data* d = new Tex1Data();
    d->texFmt      = kGxtxTexFmt; // routing sentinel (upload-path dispatch)
    d->gxNativeFmt = gxFmt;       // actual GX format for Wii_UploadTiledGX
    d->wLog2  = 0; // dims are non-log2; apparent dims are authoritative
    d->hLog2  = 0;

    d->info.rawWidth       = (uint16_t)w;
    d->info.rawHeight      = (uint16_t)h;
    d->info.depth          = 1;
    d->info.levels         = 1;
    d->info.apparentWidth  = w;
    d->info.apparentHeight = h;

    // Points into the File-owned buffer (valid across the LockLayers..
    // UnlockLayers window), like the real Tex1 reader.
    d->pixels     = bytes + 12;
    d->pixelsSize = size - 12;

    return d;
}
#endif // FRUIT_PLATFORM_WII

// ---- g_readers registry ---------------------------------------------------
// Binary: 4-entry array @ 0x2cf8e8. Order: [0]=Tex3, [1]=DDS, [2]=Tex2, [3]=Tex1.
// Host/web prepend a port-specific WebP reader at index 0 (highest priority) so
// WebP-in-.tex content is detected before the raw parsers; Wii instead
// prepends the GXTX pre-tiled native-GX reader (no libwebp there); the __bada__
// cross-build keeps the binary-exact 4-entry array (see TextureFileFormat.h).
#if defined(__bada__)
TextureReadFn g_readers[4] = {
    TextureFileFormat::ReadTex3Format,  // [0] v1.6.1 Tex3Format::Read @0x0022bd7c
    TextureFileFormat::ReadDDSFormat,   // [1] v1.6.1 DDSFormat::Read @0x0022cc04
    TextureFileFormat::ReadTex2Format,  // [2] v1.6.1 Tex2Format::Read @0x0022baf8
    TextureFileFormat::ReadTex1Format   // [3] v1.6.1 Tex1Format::Read @0x0022b324
};
#elif defined(FRUIT_PLATFORM_WII)
TextureReadFn g_readers[5] = {
    TextureFileFormat::ReadGxtx,        // [0] Port specific: pre-tiled native GX textures
    TextureFileFormat::ReadTex3Format,  // [1] v1.6.1 Tex3Format::Read @0x0022bd7c
    TextureFileFormat::ReadDDSFormat,   // [2] v1.6.1 DDSFormat::Read @0x0022cc04
    TextureFileFormat::ReadTex2Format,  // [3] v1.6.1 Tex2Format::Read @0x0022baf8
    TextureFileFormat::ReadTex1Format   // [4] v1.6.1 Tex1Format::Read @0x0022b324
};
#else
TextureReadFn g_readers[5] = {
    TextureFileFormat::ReadWebP,        // [0] Port specific: web compressed textures (libwebp)
    TextureFileFormat::ReadTex3Format,  // [1] v1.6.1 Tex3Format::Read @0x0022bd7c
    TextureFileFormat::ReadDDSFormat,   // [2] v1.6.1 DDSFormat::Read @0x0022cc04
    TextureFileFormat::ReadTex2Format,  // [3] v1.6.1 Tex2Format::Read @0x0022baf8
    TextureFileFormat::ReadTex1Format   // [4] v1.6.1 Tex1Format::Read @0x0022b324
};
#endif

// ---------------------------------------------------------------------------
// TextureInfo Read<T> overloads
// ---------------------------------------------------------------------------

// ASM-spec v1.6.1 Read(DataStreamReader&, TextureInfo::ChannelDescription&) @0x0026bb0c
// Reads 2 raw bytes: m_Bits then m_TypeFlag.
void Read(DataStreamReader& reader, TextureInfo::ChannelDescription& cd) {
    reader.ReadRaw<unsigned char>(cd.m_Bits);
    reader.ReadRaw<unsigned char>(cd.m_TypeFlag);
}

// ASM-spec v1.6.1 Read(DataStreamReader&, TextureInfo::Compression&) @0x0026bb84
// Reads 2 raw bytes into m_Field0, m_Field1.
void Read(DataStreamReader& reader, TextureInfo::Compression& c) {
    reader.ReadRaw<unsigned char>(c.m_Field0);
    reader.ReadRaw<unsigned char>(c.m_Field1);
}

// ASM-spec v1.6.1 Read(DataStreamReader&, TextureInfo::NumberFormat&) @0x0026bb44
// Reads 2 raw bytes; second byte is bit-shuffled from the on-disk packed format.
// Bit shuffle (binary exact):
//   stored = (raw >> 7) | (uint8_t)(((raw & 0x7f) >> 4) << 1) | (uint8_t)((raw & 0x0f) << 4)
void Read(DataStreamReader& reader, TextureInfo::NumberFormat& nf) {
    uint8_t b0 = 0;
    uint8_t b1_raw = 0;
    reader.ReadRaw<unsigned char>(b0);
    reader.ReadRaw<unsigned char>(b1_raw);
    nf.m_Field0 = b0;
    nf.m_Field1 = (uint8_t)((b1_raw >> 7)
                  | (uint8_t)(((b1_raw & 0x7fu) >> 4) << 1)
                  | (uint8_t)((b1_raw & 0x0fu) << 4));
}

// ASM-spec v1.6.1 Read(DataStreamReader&, TextureInfo::TextureType&) @0x0026bba4
// ReadBasicType<unsigned long>(v); truncate to byte.
void Read(DataStreamReader& reader, TextureInfo::TextureType& tt) {
    unsigned long v = 0UL;
    reader.ReadBasicType<unsigned long>(v);
    tt.m_Value = (uint8_t)((unsigned int)v & 0xFFu);
}

// Read<TextureInfo::ChannelDescription, 4u> -- explicit specialisation.
// ASM-spec v1.6.1 @0x0026bc94: loop i=0..3 Read(arr[i]); on m_Error zero
// remaining entries (m_Bits=0, clear bit7 of m_TypeFlag for entries i..3).
template<>
void Read<TextureInfo::ChannelDescription, 4u>(DataStreamReader& reader,
                                               TextureInfo::ChannelDescription (&arr)[4]) {
    unsigned int i = 0;
    for (; i < 4u; ++i) {
        Read(reader, arr[i]);
        if (reader.m_Error) {
            for (unsigned int j = i; j < 4u; ++j) {
                arr[j].m_Bits    = 0;
                arr[j].m_TypeFlag = arr[j].m_TypeFlag & static_cast<uint8_t>(~0x80u);
            }
            return;
        }
    }
}

// ASM-spec v1.6.1 Read(DataStreamReader&, TextureInfo::PixelFormat&) @0x0026bbc0
// Composite: Compression(2B) + NumberFormat(2B) + ChannelDescription[4](8B) = 12B.
// DIFFERS: binary's PixelFormat has named sub-fields; port keeps it an opaque
// data[12] blob (nested as Mortar::TextureInfo::PixelFormat -- see
// TextureSource.h -- so this Read overload still mangles against the binary's
// nested type) and accesses sub-fields by offset via reinterpret_cast.
void Read(DataStreamReader& reader, PixelFormat& pf) {
    Read(reader, *reinterpret_cast<TextureInfo::Compression*>(pf.data + 0));
    Read(reader, *reinterpret_cast<TextureInfo::NumberFormat*>(pf.data + 2));
    Read<TextureInfo::ChannelDescription, 4u>(reader,
        *reinterpret_cast<TextureInfo::ChannelDescription (*)[4]>(pf.data + 4));
}

// ASM-spec v1.6.1 Read(DataStreamReader&, TextureInfo::DataInfo&) @0x0026bbec
// Composite: PixelFormat(12B) + TextureType->numberFormat(4B read, 1B stored)
//   + rawWidth/Height/depth/levels (u16 each, 8B total)
//   + [_pad16 NOT read -- struct alignment gap only]
//   + apparentWidth/Height (u32 each, 8B).
// Total stream bytes consumed: 12+4+2+2+2+2+4+4 = 32 = 0x20.
// Note: unsigned long and uint32_t are distinct types on MSVC (uint32_t=unsigned int);
// intermediate unsigned long variables are used so ReadBasicType template T matches.
void Read(DataStreamReader& reader, TextureInfo::DataInfo& di) {
    Read(reader, di.pixelFormat);
    TextureInfo::TextureType tt;
    Read(reader, tt);
    di.numberFormat = tt.m_Value;
    unsigned short rw = 0, rh = 0, dep = 0, lev = 0;
    reader.ReadBasicType<unsigned short>(rw);
    reader.ReadBasicType<unsigned short>(rh);
    reader.ReadBasicType<unsigned short>(dep);
    reader.ReadBasicType<unsigned short>(lev);
    di.rawWidth  = (uint16_t)rw;
    di.rawHeight = (uint16_t)rh;
    di.depth     = (uint16_t)dep;
    di.levels    = (uint16_t)lev;
    // _pad16 at +0x16 is NOT read -- alignment gap in struct, not present on disk.
    unsigned long aw = 0UL, ah = 0UL;
    reader.ReadBasicType<unsigned long>(aw);
    reader.ReadBasicType<unsigned long>(ah);
    di.apparentWidth  = (uint32_t)aw;
    di.apparentHeight = (uint32_t)ah;
}

} // namespace Mortar
