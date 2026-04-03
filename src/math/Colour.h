#ifndef FN_COLOUR_H
#define FN_COLOUR_H

#include <cstdint>

// Matches original Colour (BGRA byte order, 4 bytes)
struct Colour {
    uint8_t b, g, r, a;

    Colour() : b(0), g(0), r(0), a(255) {}
    Colour(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
        : b(b), g(g), r(r), a(a) {}

    // Convert to packed uint32 (platform colour)
    uint32_t PlatformColour() const {
        return (uint32_t)a << 24 | (uint32_t)b << 16 | (uint32_t)g << 8 | r;
    }

    // For GL: return as float array (RGBA order for shader)
    void toFloat(float* out) const {
        out[0] = r / 255.0f;
        out[1] = g / 255.0f;
        out[2] = b / 255.0f;
        out[3] = a / 255.0f;
    }
};

#endif
