#include "tex_loader.h"
#include <cstdio>
#include <cstring>

bool tex_load(const std::string& path, TexImage& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;

    // Read 12-byte header
    uint8_t header[12];
    if (fread(header, 1, 12, f) != 12) {
        fclose(f);
        return false;
    }

    uint8_t widthLog2  = header[0];
    uint8_t heightLog2 = header[1];
    out.format = header[2];
    out.width  = (uint16_t)(header[4] | (header[5] << 8));
    out.height = (uint16_t)(header[6] | (header[7] << 8));

    // Verify dimensions match log2 values
    if (out.width != (1 << widthLog2) || out.height != (1 << heightLog2)) {
        // Use log2 values as authoritative
        out.width  = 1 << widthLog2;
        out.height = 1 << heightLog2;
    }

    uint32_t pixelCount = (uint32_t)out.width * out.height;

    // Read raw pixel data (2 bytes per pixel for both RGBA4444 and RGB565)
    uint32_t rawSize = pixelCount * 2;
    std::vector<uint8_t> raw(rawSize);
    size_t read = fread(raw.data(), 1, rawSize, f);
    fclose(f);

    if (read < rawSize) return false;

    // Convert to RGBA8888
    out.pixels.resize(pixelCount * 4);

    for (uint32_t i = 0; i < pixelCount; i++) {
        uint16_t pixel = (uint16_t)(raw[i * 2] | (raw[i * 2 + 1] << 8));
        uint8_t r, g, b, a;

        if (out.format == 0x10) {
            // RGBA4444
            r = ((pixel >> 12) & 0xF) * 17;
            g = ((pixel >>  8) & 0xF) * 17;
            b = ((pixel >>  4) & 0xF) * 17;
            a = ((pixel >>  0) & 0xF) * 17;
        } else if (out.format == 0x11) {
            // RGB565
            r = (uint8_t)(((pixel >> 11) & 0x1F) * 255 / 31);
            g = (uint8_t)(((pixel >>  5) & 0x3F) * 255 / 63);
            b = (uint8_t)(((pixel >>  0) & 0x1F) * 255 / 31);
            a = 255;
        } else if (out.format == 0x01) {
            // RGBA8888 — read 4 bytes per pixel instead
            // Need to re-read with correct size
            fclose(fopen(path.c_str(), "rb")); // already closed above
            r = g = b = a = 0;
        } else {
            // Unknown format, treat as RGBA4444
            r = ((pixel >> 12) & 0xF) * 17;
            g = ((pixel >>  8) & 0xF) * 17;
            b = ((pixel >>  4) & 0xF) * 17;
            a = ((pixel >>  0) & 0xF) * 17;
        }

        out.pixels[i * 4 + 0] = r;
        out.pixels[i * 4 + 1] = g;
        out.pixels[i * 4 + 2] = b;
        out.pixels[i * 4 + 3] = a;
    }

    return true;
}
