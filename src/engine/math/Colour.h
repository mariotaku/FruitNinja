#ifndef MORTAR_COLOUR_H
#define MORTAR_COLOUR_H

#include <cstdint>
#include <algorithm>

// Matches original Colour (BGRA byte order, 4 bytes)
struct Colour {
    uint8_t b, g, r, a;

    Colour() : b(0), g(0), r(0), a(255) {}
    Colour(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
        : b(b), g(g), r(r), a(a) {}

    // Return packed BGRA value
    uint32_t PlatformColour() const {
        return (uint32_t)a << 24 | (uint32_t)b << 16 | (uint32_t)g << 8 | r;
    }

    // Float RGB (0-1) to packed BGRA white-tinted colour (alpha = 255)
    static Colour TintWhite(float rf, float gf, float bf) {
        uint8_t rc = (uint8_t)(rf < 0.0f ? 0 : (rf > 1.0f ? 255 : (int)(rf * 255.0f)));
        uint8_t gc = (uint8_t)(gf < 0.0f ? 0 : (gf > 1.0f ? 255 : (int)(gf * 255.0f)));
        uint8_t bc = (uint8_t)(bf < 0.0f ? 0 : (bf > 1.0f ? 255 : (int)(bf * 255.0f)));
        return Colour(rc, gc, bc, 255);
    }

    // Multiply RGB channels by scale, clamp to [0, 255]
    static Colour TintColour(Colour c, float scale) {
        int ri = (int)(c.r * scale);
        int gi = (int)(c.g * scale);
        int bi = (int)(c.b * scale);
        if (ri > 255) ri = 255; if (ri < 0) ri = 0;
        if (gi > 255) gi = 255; if (gi < 0) gi = 0;
        if (bi > 255) bi = 255; if (bi < 0) bi = 0;
        return Colour((uint8_t)ri, (uint8_t)gi, (uint8_t)bi, c.a);
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
