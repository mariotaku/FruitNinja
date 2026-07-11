#!/usr/bin/env python3
"""tools/lib/tex_decoder.py -- Shared Tex1 (.tex) decode core.

Single source of truth for parsing and unpacking Fruit Ninja's Tex1 texture
format, used by both tools/web/stage-web-assets.py (web build staging) and
tools/assets/convert_tex.py (gallery generation). Pure stdlib -- no PIL, no
ffmpeg/subprocess.

Tex1 header (12 bytes): byte0 wLog2, byte1 hLog2, byte2 format, [3..11] pad.
Pixel data starts at offset 12.
"""

import array
import struct
import sys

TEX_HEADER_SIZE = 12  # Tex1 header: byte0 wLog2, byte1 hLog2, byte2 fmt, [3..11] pad

# Tex1 format byte -> bits-per-pixel, restricted to the formats this module can
# unpack to RGBA8888. Must match Mortar::TextureFileFormat::ReadTex1Format +
# Texture.cpp's GL upload (RGB888/RGBA8888/RGBA5551/RGBA4444/RGB565). Any other
# format byte (0x02..0x0e PVRTC/etc.) -> not transcodable (caller copies verbatim).
TEX_KNOWN_FORMATS = {
    0x00: 24,  # RGB888
    0x01: 32,  # RGBA8888
    0x0f: 16,  # RGBA5551
    0x10: 16,  # RGBA4444
    0x11: 16,  # RGB565
}

TEX_FORMAT_NAMES = {
    0x00: "RGB888",
    0x01: "RGBA8888",
    0x0F: "RGBA5551",
    0x10: "RGBA4444",
    0x11: "RGB565",
}

# Channel-expansion lookup tables (bit replication, matching the GL/ES upload
# expansion used on desktop): 5-bit -> 8-bit, 6-bit -> 8-bit, 4-bit -> 8-bit.
_EXP5 = tuple(((v << 3) | (v >> 2)) for v in range(32))
_EXP6 = tuple(((v << 2) | (v >> 4)) for v in range(64))
_EXP4 = tuple(((v << 4) | v) for v in range(16))


def unpack_tex1_to_rgba(fmt, w, h, body):
    """Decode a Tex1 pixel body (one of the 5 known formats) to a flat RGBA8888
    bytearray (w*h*4 bytes, row-major, R,G,B,A). Bulk slice assignment is used so
    the per-pixel work stays at C speed for the common formats."""
    n = w * h
    if fmt == 0x01:  # RGBA8888 -- straight copy
        return bytearray(body[:n * 4])

    out = bytearray(n * 4)
    if fmt == 0x00:  # RGB888 (R,G,B bytes) -> RGBA, A=255
        out[0::4] = body[0:n * 3:3]
        out[1::4] = body[1:n * 3:3]
        out[2::4] = body[2:n * 3:3]
        out[3::4] = b"\xff" * n
        return out

    # 16-bit formats: read the body as native-order uint16, force little-endian.
    u16 = array.array("H")
    u16.frombytes(bytes(body[:n * 2]))
    if sys.byteorder != "little":
        u16.byteswap()

    if fmt == 0x0f:  # RGBA5551: R[15..11] G[10..6] B[5..1] A[0]
        out[0::4] = bytes(_EXP5[(p >> 11) & 0x1f] for p in u16)
        out[1::4] = bytes(_EXP5[(p >> 6) & 0x1f] for p in u16)
        out[2::4] = bytes(_EXP5[(p >> 1) & 0x1f] for p in u16)
        out[3::4] = bytes(0xff if (p & 0x1) else 0x00 for p in u16)
    elif fmt == 0x10:  # RGBA4444: R[15..12] G[11..8] B[7..4] A[3..0]
        out[0::4] = bytes(_EXP4[(p >> 12) & 0xf] for p in u16)
        out[1::4] = bytes(_EXP4[(p >> 8) & 0xf] for p in u16)
        out[2::4] = bytes(_EXP4[(p >> 4) & 0xf] for p in u16)
        out[3::4] = bytes(_EXP4[p & 0xf] for p in u16)
    elif fmt == 0x11:  # RGB565: R[15..11] G[10..5] B[4..0], A=255
        out[0::4] = bytes(_EXP5[(p >> 11) & 0x1f] for p in u16)
        out[1::4] = bytes(_EXP6[(p >> 5) & 0x3f] for p in u16)
        out[2::4] = bytes(_EXP5[p & 0x1f] for p in u16)
        out[3::4] = b"\xff" * n
    return out


def parse_tex1(raw):
    """If raw is a Tex1 with a known transcodable format and passes the reader's
    size check, return (fmt, w, h, body); else None (copy verbatim)."""
    if len(raw) < 0xd:
        return None
    w_log2 = raw[0]
    h_log2 = raw[1]
    fmt = raw[2]
    if fmt not in TEX_KNOWN_FORMATS:
        return None
    bpp = TEX_KNOWN_FORMATS[fmt]
    row_bytes = (((bpp << w_log2) + 7) >> 3)
    pix_bytes = row_bytes << h_log2
    expected = TEX_HEADER_SIZE + pix_bytes
    if len(raw) < expected:
        return None
    w = 1 << w_log2
    h = 1 << h_log2
    body = raw[TEX_HEADER_SIZE:TEX_HEADER_SIZE + pix_bytes]
    return (fmt, w, h, body)


def decode_tex(path):
    """Read a .tex file and fully decode it to RGBA8888.
    Returns (w, h, rgba_bytearray) or None if it doesn't parse as a known Tex1."""
    with open(path, "rb") as f:
        raw = f.read()
    parsed = parse_tex1(raw)
    if parsed is None:
        return None
    fmt, w, h, body = parsed
    rgba = unpack_tex1_to_rgba(fmt, w, h, body)
    return (w, h, rgba)
