// fn_png_write.h -- minimal single-header PNG writer for test screenshots.
//
// Writes RGB (3-channel, 8-bit) images to PNG using uncompressed deflate
// (store mode). Output is valid PNG; any viewer/browser can open it.
// No external dependencies (no zlib, no libpng).
//
// Usage (define once in exactly one TU before including):
//   #define FN_PNG_WRITE_IMPLEMENTATION
//   #include "third_party/fn_png_write.h"
//
// Then call:
//   fn_png_write_rgb(path, pixels, width, height, top_down);
//   // pixels: packed RGB bytes; top_down=1 if row 0 is the top (normal),
//   //         top_down=0 if row 0 is the bottom (glReadPixels default).
//   // Returns 0 on success, non-zero on error.
//
// License: public domain (written for the fruit-ninja port project).

#ifndef FN_PNG_WRITE_H
#define FN_PNG_WRITE_H

#ifdef __cplusplus
extern "C" {
#endif

int fn_png_write_rgb(const char* path, const unsigned char* pixels,
                     int width, int height, int top_down);

#ifdef __cplusplus
}
#endif

// ---------------------------------------------------------------------------
// Implementation
// ---------------------------------------------------------------------------
#ifdef FN_PNG_WRITE_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// CRC-32 table (ISO 3309 polynomial 0xEDB88320, reflected).
static unsigned int fn__crc32_table[256];
static int          fn__crc32_table_ready = 0;

static void fn__crc32_init(void) {
    unsigned int i, j, c;
    for (i = 0; i < 256; ++i) {
        c = i;
        for (j = 0; j < 8; ++j) {
            if (c & 1) c = 0xEDB88320u ^ (c >> 1);
            else       c = c >> 1;
        }
        fn__crc32_table[i] = c;
    }
    fn__crc32_table_ready = 1;
}

static unsigned int fn__crc32_update(unsigned int crc, const unsigned char* buf, size_t len) {
    size_t i;
    if (!fn__crc32_table_ready) fn__crc32_init();
    for (i = 0; i < len; ++i)
        crc = fn__crc32_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

static unsigned int fn__crc32(const unsigned char* buf, size_t len) {
    return fn__crc32_update(0xFFFFFFFFu, buf, len) ^ 0xFFFFFFFFu;
}

// Adler-32 (zlib checksum).
static unsigned int fn__adler32(const unsigned char* buf, size_t len) {
    unsigned int s1 = 1, s2 = 0;
    size_t i;
    for (i = 0; i < len; ++i) {
        s1 = (s1 + buf[i]) % 65521u;
        s2 = (s2 + s1)     % 65521u;
    }
    return (s2 << 16) | s1;
}

// Write a big-endian uint32 to file.
static void fn__write_u32be(FILE* f, unsigned int v) {
    unsigned char b[4];
    b[0] = (unsigned char)((v >> 24) & 0xFF);
    b[1] = (unsigned char)((v >> 16) & 0xFF);
    b[2] = (unsigned char)((v >>  8) & 0xFF);
    b[3] = (unsigned char)((v      ) & 0xFF);
    fwrite(b, 1, 4, f);
}

// Write a PNG chunk: 4-byte length + 4-byte type + data + 4-byte CRC.
static void fn__write_chunk(FILE* f, const char type[4],
                            const unsigned char* data, unsigned int dlen) {
    unsigned char type_u[4];
    unsigned int  crc;
    fn__write_u32be(f, dlen);
    type_u[0] = (unsigned char)type[0]; type_u[1] = (unsigned char)type[1];
    type_u[2] = (unsigned char)type[2]; type_u[3] = (unsigned char)type[3];
    fwrite(type_u, 1, 4, f);
    if (dlen > 0) fwrite(data, 1, dlen, f);
    crc = fn__crc32_update(0xFFFFFFFFu, type_u, 4);
    if (dlen > 0) crc = fn__crc32_update(crc, data, dlen);
    crc ^= 0xFFFFFFFFu;
    fn__write_u32be(f, crc);
}

// Build the IDAT payload: zlib stream wrapping deflate store blocks.
// Each deflate store block can hold at most 65535 bytes.
// We emit one filter byte (0x00 = None) per row before the pixel data.
//
// Filtered row stream = width * height * 3 + height bytes (1 filter per row).
// We pack that into deflate store blocks, then wrap in a zlib container.

static unsigned char* fn__build_idat(const unsigned char* pixels,
                                     int width, int height, int top_down,
                                     size_t* out_size) {
    // Filtered data: 1 filter byte + width*3 pixel bytes per row.
    size_t row_bytes  = (size_t)width * 3;
    size_t filt_size  = ((size_t)height) * (1 + row_bytes);

    // Max deflate store block payload: 65535 bytes.
    // Number of blocks needed.
    size_t nblocks = (filt_size + 65534) / 65535;
    if (nblocks == 0) nblocks = 1;

    // zlib header (2) + blocks (5 + payload each) + adler32 (4).
    size_t idat_size = 2 + nblocks * 5 + filt_size + 4;
    unsigned char* idat = (unsigned char*)malloc(idat_size);
    if (!idat) return NULL;

    // Build filtered data in a scratch buffer.
    unsigned char* filt = (unsigned char*)malloc(filt_size);
    if (!filt) { free(idat); return NULL; }
    {
        size_t pos = 0;
        int y;
        for (y = 0; y < height; ++y) {
            int src_y = top_down ? y : (height - 1 - y);
            filt[pos++] = 0x00; // filter type None
            memcpy(filt + pos, pixels + (size_t)src_y * row_bytes, row_bytes);
            pos += row_bytes;
        }
    }

    // Adler-32 of uncompressed data (filt).
    unsigned int adler = fn__adler32(filt, filt_size);

    // Assemble zlib stream.
    size_t wp = 0;
    idat[wp++] = 0x78; // zlib CMF: deflate, window=32k
    idat[wp++] = 0x01; // zlib FLG: no dict, check bits (0x7801 % 31 == 0)

    // Deflate store blocks.
    size_t remaining = filt_size;
    size_t src_off   = 0;
    while (remaining > 0) {
        size_t chunk = remaining < 65535 ? remaining : 65535;
        int    bfinal = (remaining <= 65535) ? 1 : 0;
        unsigned short len  = (unsigned short)chunk;
        unsigned short nlen = (unsigned short)(~len);
        idat[wp++] = (unsigned char)bfinal; // BFINAL | (BTYPE=00)<<1
        idat[wp++] = (unsigned char)( len        & 0xFF);
        idat[wp++] = (unsigned char)((len  >> 8) & 0xFF);
        idat[wp++] = (unsigned char)( nlen       & 0xFF);
        idat[wp++] = (unsigned char)((nlen >> 8) & 0xFF);
        memcpy(idat + wp, filt + src_off, chunk);
        wp      += chunk;
        src_off += chunk;
        remaining -= chunk;
    }

    // Adler-32 (big-endian).
    idat[wp++] = (unsigned char)((adler >> 24) & 0xFF);
    idat[wp++] = (unsigned char)((adler >> 16) & 0xFF);
    idat[wp++] = (unsigned char)((adler >>  8) & 0xFF);
    idat[wp++] = (unsigned char)((adler      ) & 0xFF);

    free(filt);
    *out_size = wp;
    return idat;
}

int fn_png_write_rgb(const char* path, const unsigned char* pixels,
                     int width, int height, int top_down) {
    FILE* f;
    unsigned char ihdr[13];
    unsigned char* idat;
    size_t idat_size;

    if (!path || !pixels || width <= 0 || height <= 0) return 1;

    f = fopen(path, "wb");
    if (!f) return 2;

    // PNG signature.
    static const unsigned char sig[8] = {137,80,78,71,13,10,26,10};
    fwrite(sig, 1, 8, f);

    // IHDR chunk: width, height, bit_depth=8, color_type=2 (RGB), compress=0, filter=0, interlace=0.
    ihdr[ 0] = (unsigned char)((width  >> 24) & 0xFF);
    ihdr[ 1] = (unsigned char)((width  >> 16) & 0xFF);
    ihdr[ 2] = (unsigned char)((width  >>  8) & 0xFF);
    ihdr[ 3] = (unsigned char)((width       ) & 0xFF);
    ihdr[ 4] = (unsigned char)((height >> 24) & 0xFF);
    ihdr[ 5] = (unsigned char)((height >> 16) & 0xFF);
    ihdr[ 6] = (unsigned char)((height >>  8) & 0xFF);
    ihdr[ 7] = (unsigned char)((height      ) & 0xFF);
    ihdr[ 8] = 8;   // bit depth
    ihdr[ 9] = 2;   // color type: RGB
    ihdr[10] = 0;   // compression method
    ihdr[11] = 0;   // filter method
    ihdr[12] = 0;   // interlace method
    fn__write_chunk(f, "IHDR", ihdr, 13);

    // IDAT chunk.
    idat = fn__build_idat(pixels, width, height, top_down, &idat_size);
    if (!idat) { fclose(f); return 3; }
    fn__write_chunk(f, "IDAT", idat, (unsigned int)idat_size);
    free(idat);

    // IEND chunk.
    fn__write_chunk(f, "IEND", NULL, 0);

    fclose(f);
    return 0;
}

#endif // FN_PNG_WRITE_IMPLEMENTATION
#endif // FN_PNG_WRITE_H
