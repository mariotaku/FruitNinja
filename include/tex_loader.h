#ifndef TEX_LOADER_H
#define TEX_LOADER_H

#include <cstdint>
#include <string>
#include <vector>

struct TexImage {
    uint16_t width;
    uint16_t height;
    uint8_t  format;       // 0x10=RGBA4444, 0x11=RGB565
    std::vector<uint8_t> pixels;  // always RGBA8888 after load
};

// Load a .tex file and convert to RGBA8888
bool tex_load(const std::string& path, TexImage& out);

#endif
