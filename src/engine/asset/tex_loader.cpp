#include "asset/tex_loader.h"
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
        out.width  = 1 << widthLog2;
        out.height = 1 << heightLog2;
    }

    uint32_t pixelCount = (uint32_t)out.width * out.height;

    // Determine bytes per pixel for raw data
    int bpp = 2; // RGBA4444, RGB565
    if (out.format == 0x01) bpp = 4; // RGBA8888
    else if (out.format == 0x00) bpp = 3; // RGB888

    uint32_t rawSize = pixelCount * bpp;
    std::vector<uint8_t> raw(rawSize);
    size_t bytesRead = fread(raw.data(), 1, rawSize, f);
    fclose(f);

    if (bytesRead < rawSize) return false;

    // Convert to RGBA8888
    out.pixels.resize(pixelCount * 4);

    for (uint32_t i = 0; i < pixelCount; i++) {
        uint8_t r, g, b, a;

        if (out.format == 0x10) {
            // RGBA4444
            uint16_t pixel = (uint16_t)(raw[i * 2] | (raw[i * 2 + 1] << 8));
            r = ((pixel >> 12) & 0xF) * 17;
            g = ((pixel >>  8) & 0xF) * 17;
            b = ((pixel >>  4) & 0xF) * 17;
            a = ((pixel >>  0) & 0xF) * 17;
        } else if (out.format == 0x11) {
            // RGB565
            uint16_t pixel = (uint16_t)(raw[i * 2] | (raw[i * 2 + 1] << 8));
            r = (uint8_t)(((pixel >> 11) & 0x1F) * 255 / 31);
            g = (uint8_t)(((pixel >>  5) & 0x3F) * 255 / 63);
            b = (uint8_t)(((pixel >>  0) & 0x1F) * 255 / 31);
            a = 255;
        } else if (out.format == 0x01) {
            // RGBA8888
            r = raw[i * 4 + 0];
            g = raw[i * 4 + 1];
            b = raw[i * 4 + 2];
            a = raw[i * 4 + 3];
        } else if (out.format == 0x00) {
            // RGB888
            r = raw[i * 3 + 0];
            g = raw[i * 3 + 1];
            b = raw[i * 3 + 2];
            a = 255;
        } else {
            // Unknown format, treat as RGBA4444
            uint16_t pixel = (uint16_t)(raw[i * 2] | (raw[i * 2 + 1] << 8));
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
