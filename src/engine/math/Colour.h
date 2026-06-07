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

    // Helper for TintColour. Static method (rather than a C++11 lambda) so
    // GCC 4.4 / 4.5 (the cross-build asm-verify toolchain) can parse this.
    static uint8_t Clamp255(float v) {
        if (v <= 0.0f) return 0;
        if (v >= 255.0f) return 255;
        return (uint8_t)v;
    }

    // Mortar::TintColour @ 0x0013540c -- per-channel tint with [0..255] clamp.
    // tintRGB[0..2] multiplies R/G/B independently; alpha is preserved.
    // ASM-verified: 2026-04-29T03:29Z binary @ 0x0013540c (asm-inspector)
    static Colour TintColour(Colour src, const float tintRGB[3]) {
        return Colour(Clamp255(src.r * tintRGB[0]),
                      Clamp255(src.g * tintRGB[1]),
                      Clamp255(src.b * tintRGB[2]),
                      src.a);
    }

    // Identity tint (1,1,1): used when HUDControl+0x60 tint flag is 0.
    static const float* IdentityTint() {
        static const float kIdentityTint[3] = {1.0f, 1.0f, 1.0f};
        return kIdentityTint;
    }

    // For GL: return as float array (RGBA order for shader)
    void toFloat(float* out) const {
        out[0] = r / 255.0f;
        out[1] = g / 255.0f;
        out[2] = b / 255.0f;
        out[3] = a / 255.0f;
    }

public:
    // Binary @ 0x00183f58 -- two-arg Lerp: builds copies of `a` and `*this`
    //   then delegates to the 3-arg Lerp(this, a, *this, t). Returns *this.
    Colour* Lerp(Colour const& a, float t) const;
    // Binary @ 0x00183e98 -- three-arg Lerp: this = a; per channel
    //   this -= (b - a) * t (R/G/B/A), then clamp each to >= 0 (signed->float,
    //   truncate toward zero). Returns this.
    Colour* Lerp(Colour a, Colour b, float t);
    // Binary @ 0x00183f98 -- snprintf "%d, %d, %d, %d (argb)" (a,r,g,b) into a
    //   static 0x100 buffer and return it.
    char* ToString() const;
};

#endif
