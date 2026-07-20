#!/usr/bin/env python3
"""tools/lib/gx_encoder.py -- GX texture tiler + "GXT1" container encoder.

Single source of truth for pre-tiling RGBA8888 pixel data into the Wii GX
hardware texture layouts and wrapping them in the port's "GXT1" container
(reader: src/engine/asset/TextureFileFormat.cpp ReadGxtx; uploader:
src/engine/render/gl_funcsWii.cpp Wii_UploadTiledGX). Used by
tools/assets/stage-assets.py --wii for BOTH the UI widget art (RRAW sidecars,
always GX_TF_RGBA8) and every transcodable Tex1 game texture (GX format chosen
to preserve the source bit-depth -- see TEX1_TO_GX). Pure stdlib.

GXT1 container (12-byte big-endian header, Wii native):
    offset 0:  magic  "GXT1"
    offset 4:  u16be  width  (apparent texels)
    offset 6:  u16be  height
    offset 8:  u8     gxFormat (GX_TF_RGB565=4 / GX_TF_RGB5A3=5 / GX_TF_RGBA8=6)
    offset 9:  u8     version = 1
    offset 10: u16be  reserved = 0
    offset 12: tiled texels, ((w+3)//4) * ((h+3)//4) * bytes-per-tile
               (64 for RGBA8, 32 for the 16bpp formats)

Tile layouts (must stay byte-identical to what GX hardware fetches; the Wii
loader uploads these bytes verbatim with no runtime decode/tile):

  GX_TF_RGBA8 (64 bytes/tile): 4x4 texel tiles scanned left-to-right,
    top-to-bottom across the image; within a tile, a 32-byte AR half (16
    texels row-major, 2 bytes each: A then R) followed by a 32-byte GB half
    (G then B). Out-of-bounds pad texels are a=255, r=g=b=0. Mirrors
    src/engine/render/gl_funcsWii.cpp TileRGBA8 exactly.

  GX_TF_RGB565 (32 bytes/tile): 4x4 texel tiles; 16 texels row-major within
    the tile, each a u16 BIG-ENDIAN (R5<<11)|(G6<<5)|B5 with R5=R>>3,
    G6=G>>2, B5=B>>3. Out-of-bounds pad = 0x0000.

  GX_TF_RGB5A3 (32 bytes/tile): 4x4 texel tiles; 16 texels row-major, each a
    u16 BIG-ENDIAN. Alpha >= 0xE0 (would round to A3=7 = fully opaque) uses
    the opaque encoding 0x8000|(R5<<10)|(G5<<5)|B5 (R5=R>>3 etc.); lower
    alpha uses the translucent encoding (A3<<12)|(R4<<8)|(G4<<4)|B4
    (A3=A>>5, R4=R>>4 etc.). Out-of-bounds pad = 0x0000. Lossless for
    RGBA5551 sources (opaque->RGB555, alpha==0->A3=0); alpha 4->3 bit for
    RGBA4444.

  GX_TF_IA8 (32 bytes/tile): 4x4 texel tiles; 16 texels row-major, each a u16
    BIG-ENDIAN = (A<<8)|I from a 2-byte-per-texel [I][A] (intensity, alpha)
    source buffer. Out-of-bounds pad = 0x0000 (transparent black). Mirrors
    src/engine/render/gl_funcsWii.cpp TileIA8/TileRegion(TILE_FMT_IA8)
    exactly -- used by tools/wii/bake-fonts.py for the prebaked TTF glyph
    atlases (font-baker's source layout is [I][A] same as the runtime's LA8
    font atlas path in FontInterface.cpp).

Self-check: `python3 gx_encoder.py --selftest` round-trips known texels
through each tiler and asserts the exact tile bytes (registered as the
`gx_encoder` ctest case in tests/CMakeLists.txt).
"""

import struct

GX_TF_IA8    = 3
GX_TF_RGB565 = 4
GX_TF_RGB5A3 = 5
GX_TF_RGBA8  = 6

BYTES_PER_TILE = {
    GX_TF_IA8:    32,
    GX_TF_RGB565: 32,
    GX_TF_RGB5A3: 32,
    GX_TF_RGBA8:  64,
}

# Tex1 source format byte (byte[2] of the .tex header, see
# tools/lib/tex_decoder.py TEX_KNOWN_FORMATS) -> GX format, preserving the
# source bit-depth (near-lossless):
#   0x01 RGBA8888 -> GX_TF_RGBA8  (32bpp, lossless; particles/soft-alpha art
#                                  live here -- never downgrade to RGB5A3)
#   0x11 RGB565   -> GX_TF_RGB565 (16bpp, lossless)
#   0x0f RGBA5551 -> GX_TF_RGB5A3 (16bpp, lossless)
#   0x10 RGBA4444 -> GX_TF_RGB5A3 (16bpp, alpha 4->3 bit)
#   0x00 RGB888   -> GX_TF_RGB565 (16bpp; no alpha, minor color loss)
# Keys deliberately equal tex_decoder.TEX_KNOWN_FORMATS: anything
# parse_tex1 accepts has a GX mapping; anything else stays verbatim.
TEX1_TO_GX = {
    0x00: GX_TF_RGB565,
    0x01: GX_TF_RGBA8,
    0x0f: GX_TF_RGB5A3,
    0x10: GX_TF_RGB5A3,
    0x11: GX_TF_RGB565,
}


def tiled_size(w, h, gx_fmt):
    """Byte length of the tiled body -- equals libogc
    GX_GetTexBufferSize(w, h, gx_fmt, GX_FALSE, 0) for these formats."""
    return ((w + 3) // 4) * ((h + 3) // 4) * BYTES_PER_TILE[gx_fmt]


def tile_rgba8(rgba, w, h):
    """Tile a flat row-major RGBA8888 buffer into GX_TF_RGBA8 layout.
    EXACT copy of gl_funcsWii.cpp TileRGBA8 (4x4 tiles, AR half then GB half,
    64 bytes/tile, oob pad a=255 r=g=b=0)."""
    out = bytearray()
    for ty in range(0, h, 4):
        for tx in range(0, w, 4):
            # AR half-tile (16 texels x 2 bytes = 32 bytes).
            for y in range(4):
                for x in range(4):
                    sx, sy = tx + x, ty + y
                    if sx < w and sy < h:
                        i = (sy * w + sx) * 4
                        out.append(rgba[i + 3])  # a
                        out.append(rgba[i + 0])  # r
                    else:
                        out.append(255)
                        out.append(0)
            # GB half-tile.
            for y in range(4):
                for x in range(4):
                    sx, sy = tx + x, ty + y
                    if sx < w and sy < h:
                        i = (sy * w + sx) * 4
                        out.append(rgba[i + 1])  # g
                        out.append(rgba[i + 2])  # b
                    else:
                        out.append(0)
                        out.append(0)
    return bytes(out)


def _tile_u16(vals, w, h):
    """Tile a flat row-major list of per-texel u16 values into 4x4 tiles of
    16 big-endian u16s (32 bytes/tile). Out-of-bounds pad = 0x0000."""
    out = bytearray()
    pack16 = struct.Struct(">16H").pack
    for ty in range(0, h, 4):
        for tx in range(0, w, 4):
            if tx + 4 <= w and ty + 4 <= h:
                base = ty * w + tx
                t = (vals[base:base + 4]
                     + vals[base + w:base + w + 4]
                     + vals[base + 2 * w:base + 2 * w + 4]
                     + vals[base + 3 * w:base + 3 * w + 4])
            else:
                t = []
                for y in range(4):
                    for x in range(4):
                        sx, sy = tx + x, ty + y
                        t.append(vals[sy * w + sx] if (sx < w and sy < h) else 0)
            out += pack16(*t)
    return bytes(out)


def tile_rgb565(rgba, w, h):
    """Tile a flat row-major RGBA8888 buffer into GX_TF_RGB565 layout
    (alpha discarded)."""
    vals = [((rgba[i] >> 3) << 11) | ((rgba[i + 1] >> 2) << 5) | (rgba[i + 2] >> 3)
            for i in range(0, w * h * 4, 4)]
    return _tile_u16(vals, w, h)


def tile_rgb5a3(rgba, w, h):
    """Tile a flat row-major RGBA8888 buffer into GX_TF_RGB5A3 layout
    (opaque texels RGB555 with the 0x8000 flag, translucent A3R4G4B4)."""
    vals = []
    n4 = w * h * 4
    i = 0
    while i < n4:
        r = rgba[i]
        g = rgba[i + 1]
        b = rgba[i + 2]
        a = rgba[i + 3]
        if a >= 0xE0:
            vals.append(0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3))
        else:
            vals.append(((a >> 5) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4))
        i += 4
    return _tile_u16(vals, w, h)


def tile_ia8(ia, w, h):
    """Tile a flat row-major 2-byte-per-texel [I][A] (intensity, alpha)
    buffer into GX_TF_IA8 layout. EXACT match of gl_funcsWii.cpp
    TileRegion's TILE_FMT_IA8 branch: one BIG-ENDIAN u16 per texel =
    (A<<8)|I (alpha byte first, then intensity -- mirrors TileRGBA8's AR
    half writing A before R). Out-of-bounds pad = 0x0000."""
    vals = [(ia[i + 1] << 8) | ia[i] for i in range(0, w * h * 2, 2)]
    return _tile_u16(vals, w, h)


_TILERS = {
    GX_TF_IA8: tile_ia8,
    GX_TF_RGB565: tile_rgb565,
    GX_TF_RGB5A3: tile_rgb5a3,
    GX_TF_RGBA8: tile_rgba8,
}


def encode_gxtx(rgba, w, h, gx_fmt):
    """Encode a flat row-major pixel buffer as a "GXT1" container with the
    given GX format (module docstring has the full header + tile-layout
    spec). `rgba` is RGBA8888 (R,G,B,A) for GX_TF_RGBA8/RGB565/RGB5A3, or a
    2-byte-per-texel [I][A] buffer for GX_TF_IA8."""
    if gx_fmt not in _TILERS:
        raise ValueError("unsupported GX format {} (want 3/4/5/6)".format(gx_fmt))
    out = bytearray(b"GXT1")
    out += struct.pack(">HHBBH", w, h, gx_fmt, 1, 0)
    out += _TILERS[gx_fmt](rgba, w, h)
    return bytes(out)


# --- selftest ----------------------------------------------------------------

def _selftest():
    # 1) RGBA8 4x4 full tile: AR half then GB half, texels row-major.
    rgba = bytearray()
    for k in range(16):
        rgba += bytes([k, 100 + k, (200 + k) & 0xFF, 50 + k])  # r,g,b,a
    t = tile_rgba8(rgba, 4, 4)
    assert len(t) == 64
    for k in range(16):
        assert t[2 * k] == 50 + k, "AR half: a mismatch at texel %d" % k
        assert t[2 * k + 1] == k, "AR half: r mismatch at texel %d" % k
        assert t[32 + 2 * k] == 100 + k, "GB half: g mismatch at texel %d" % k
        assert t[32 + 2 * k + 1] == (200 + k) & 0xFF, "GB half: b mismatch at texel %d" % k

    # 2) RGBA8 out-of-bounds pad (5x1): 2 tiles; tile 1 texel (4,0) real,
    #    everything else padded a=255 r=g=b=0.
    rgba = bytes([10, 20, 30, 40]) * 5
    t = tile_rgba8(rgba, 5, 1)
    assert len(t) == 2 * 64
    # tile 0, texel 0: a=40 r=10 / g=20 b=30
    assert t[0] == 40 and t[1] == 10 and t[64 - 32] == 20 and t[64 - 32 + 1] == 30
    # tile 0, texel (0,1) is oob (h=1): pad
    assert t[2 * 4] == 255 and t[2 * 4 + 1] == 0
    assert t[32 + 2 * 4] == 0 and t[32 + 2 * 4 + 1] == 0
    # tile 1, texel 0 = pixel (4,0); texel 1 = oob pad
    assert t[64] == 40 and t[64 + 1] == 10
    assert t[64 + 2] == 255 and t[64 + 3] == 0

    # 3) RGB565: (255,132,3) -> R5=31 G6=33 B5=0 -> 0xFC20 big-endian.
    t = tile_rgb565(bytes([255, 132, 3, 0]), 1, 1)
    assert len(t) == 32
    assert t[0:2] == b"\xfc\x20", t[0:2]
    assert t[2:] == b"\x00" * 30  # oob pad = 0x0000

    # 4) RGB5A3 opaque: (8,16,248,255) -> 0x8000|1<<10|2<<5|31 = 0x845F.
    t = tile_rgb5a3(bytes([8, 16, 248, 255]), 1, 1)
    assert t[0:2] == b"\x84\x5f", t[0:2]
    assert t[2:] == b"\x00" * 30
    # Opaque threshold boundary: a=0xE0 -> opaque encoding (A3 would be 7).
    t = tile_rgb5a3(bytes([255, 255, 255, 0xE0]), 1, 1)
    assert t[0:2] == b"\xff\xff", t[0:2]  # 0x8000|0x7FFF
    # Just below: a=0xDF -> translucent A3=6, R4=G4=B4=15 -> 0x6FFF.
    t = tile_rgb5a3(bytes([255, 255, 255, 0xDF]), 1, 1)
    assert t[0:2] == b"\x6f\xff", t[0:2]
    # 5) RGB5A3 translucent: (0x10,0x20,0x30,0x40) -> A3=2 R4=1 G4=2 B4=3 -> 0x2123.
    t = tile_rgb5a3(bytes([0x10, 0x20, 0x30, 0x40]), 1, 1)
    assert t[0:2] == b"\x21\x23", t[0:2]
    # RGBA5551-source losslessness: alpha==0 -> fully transparent (A3=0).
    t = tile_rgb5a3(bytes([0x88, 0x88, 0x88, 0x00]), 1, 1)
    assert t[0:2] == b"\x08\x88", t[0:2]  # (0<<12)|(8<<8)|(8<<4)|8

    # 6) IA8: 2-byte-per-texel [I][A] source -> BE u16 (A<<8)|I.
    #    texel0 I=0x11 A=0x22 -> 0x2211; texel1 I=0xAB A=0xCD -> 0xCDAB.
    t = tile_ia8(bytes([0x11, 0x22, 0xAB, 0xCD]), 2, 1)
    assert len(t) == 32
    assert t[0:2] == b"\x22\x11", t[0:2]
    assert t[2:4] == b"\xcd\xab", t[2:4]
    assert t[4:] == b"\x00" * 28  # oob pad = 0x0000
    # IA8 out-of-bounds pad (3x1 within a 4x4 tile): texel(2,0) real, rest padded.
    t = tile_ia8(bytes([0x11, 0x22, 0x33, 0x44, 0x55, 0x66]), 3, 1)
    assert t[0:2] == b"\x22\x11" and t[2:4] == b"\x44\x33" and t[4:6] == b"\x66\x55"
    assert t[6:8] == b"\x00\x00"  # texel(3,0) oob

    # 7) Container: header fields + per-format body length.
    rgba = bytes(5 * 5 * 4)
    for fmt, tile_bytes in ((GX_TF_RGB565, 32), (GX_TF_RGB5A3, 32), (GX_TF_RGBA8, 64)):
        blob = encode_gxtx(rgba, 5, 5, fmt)
        assert blob[0:4] == b"GXT1"
        assert struct.unpack(">HHBBH", blob[4:12]) == (5, 5, fmt, 1, 0)
        assert len(blob) == 12 + 4 * tile_bytes  # 2x2 tiles
        assert len(blob) - 12 == tiled_size(5, 5, fmt)
    ia8_src = bytes(5 * 5 * 2)
    blob = encode_gxtx(ia8_src, 5, 5, GX_TF_IA8)
    assert struct.unpack(">HHBBH", blob[4:12]) == (5, 5, GX_TF_IA8, 1, 0)
    assert len(blob) == 12 + 4 * 32
    try:
        encode_gxtx(rgba, 5, 5, 7)
    except ValueError:
        pass
    else:
        raise AssertionError("encode_gxtx accepted invalid GX format 7")

    print("gx_encoder selftest: OK")


if __name__ == "__main__":
    import sys
    if "--selftest" in sys.argv[1:]:
        _selftest()
    else:
        print("usage: gx_encoder.py --selftest", file=sys.stderr)
        sys.exit(2)
