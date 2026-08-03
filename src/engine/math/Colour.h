#ifndef MORTAR_COLOUR_H
#define MORTAR_COLOUR_H

#include <cstdint>
#include <algorithm>

// ASM-spec v1.6.1 Colour (Colour.cpp TU @ 0x0021e7e4..0x0021eadc):
//   sizeof == 4, align 4. Byte layout  +0 b, +1 g, +2 r, +3 a.
//   Evidence: (a) global.constructors.keyed.to.Colour.cpp @0x0021e9b8 writes
//   Red=[2]=0xff, Green=[1]=0xff, Blue=[0]=0xff, all with [3]=0xff;
//   (b) Colour::Colour(uint8,uint8,uint8,uint8) @0x0013f970 stores r->+2,
//   g->+1, b->+0, a(stack arg)->+3;  (c) the six BSS statics at 0x0034e2f4
//   are spaced exactly 4 bytes apart.
//
//   The binary also has a 32-bit lvalue aliasing the four bytes at +0 (a
//   union member): Colour::Colour(unsigned long) @0x0021e7e4 opens with
//   `str r1,[r0,#0]` before patching two bytes, and callers compare two
//   Colours with a single `ldr`/`ldr`/`cmp` word compare (e.g.
//   BakedStringBox::SetStroke @0x002453f0 +0x5c/+0x60/+0x64). Numerically
//   that word is 0xAARRGGBB -- which is why ToString() prints "(argb)".
//   TODO: v1.6.1 0x0021e7e4 (Colour::Colour(unsigned long)) -- port the
//   packed union member + the unsigned-long ctor (inverse of PlatformColour).
//
//   Also unported: the empty user-declared ~Colour @0x0021eadc and the
//   user-declared `Colour operator=(Colour const&)` @0x0011e064 (returns by
//   VALUE, sret). Those make Colour non-POD in the binary, so it is passed
//   and returned in memory, not in r0. See the report notes in Colour.cpp.
struct Colour {
    uint8_t b, g, r, a;

    Colour() : b(0), g(0), r(0), a(255) {}
    Colour(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
        : b(b), g(g), r(r), a(a) {}

    // ASM-spec v1.6.1 Colour::PlatformColour @ 0x0021e7f8:
    //   builds a byte-swapped temp (tmp.b=r, tmp.g=g, tmp.r=b, tmp.a=a) and
    //   returns its word, i.e. (a<<24)|(b<<16)|(g<<8)|r. In memory that is
    //   r,g,b,a -- GL_RGBA order. This matches the expression below exactly.
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

    // Mortar::TintColour @0x0015f2fc -- per-channel tint with [0..255] clamp.
    // tintRGB[0..2] multiplies R/G/B independently; alpha is preserved.
    // ASM-spec v1.6.1 Mortar::TintColour @0x0015f2fc: per-channel signed->float, *tintRGB, clamp [0,255], alpha preserved.
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
    // TODO: v1.6.1 0x0021e900 (Colour::Lerp(Colour const&, float) const,
    //   _ZNK6Colour4LerpERKS_f) -- signature and semantics both diverge.
    //   Binary returns a NEW Colour BY VALUE (sret) and leaves *this alone:
    //     r0=sret, r1=this, r2=&a; it copies *this and a onto the stack and
    //     tail-calls the 3-arg Lerp(sret, /*p1=*/*this, /*p2=*/a, t).
    //   So the result is  a + (*this - a) * t   (t=0 -> a, t=1 -> *this).
    //   The port instead mutates *this and passes (a, *this) -- args swapped.
    Colour* Lerp(Colour const& a, float t) const;
    // ASM-spec v1.6.1 Colour::Lerp(Colour, Colour, float) @ 0x0021e828
    //   (_ZN6Colour4LerpES_S_f): seeds *this from the SECOND param, then
    //   subtracts the scaled delta per channel:
    //     *this = b;  this->ch -= (b.ch - a.ch) * t
    //   i.e. result = b + (a - b) * t. t=0 gives b, t=1 gives a. There is no
    //   clamp on t. The negative clamp is `vcvt.u32.f32` (round-to-zero,
    //   saturates <0 to 0) followed by strb -- equivalent to the port's
    //   (char)(int) expression. Mutates and returns *this.
    Colour* Lerp(Colour a, Colour b, float t);
    // ASM-spec v1.6.1 Colour::ToString @ 0x0021e95c: snprintf
    //   "%d, %d, %d, %d (argb)" (a,r,g,b) into the shared 0x100 static buffer
    //   at 0x0034e1f4 and return it.
    char* ToString() const;

    // Binary static colour constants: BSS, zero-init, then filled by
    // global.constructors.keyed.to.Colour.cpp @ 0x0021e9b8 (each also
    // registers Colour::~Colour with __aeabi_atexit).
    static const Colour Red;    // 0x0034e2fc  Colour(255,0,0,255)
    static const Colour White;  // 0x0034e2f8  Colour(255,255,255,255)
    static const Colour Black;  // 0x0034e2f4  Colour(0,0,0,255)
    // TODO: v1.6.1 0x0021e9b8 (global ctors keyed to Colour.cpp) -- port the
    //   three missing statics: Green 0x0034e300 Colour(0,255,0,255),
    //   Blue 0x0034e304 Colour(0,0,255,255), Yellow 0x0034e308
    //   Colour(255,255,0,255). Binary declares them non-const.
};

// ASM-spec v1.6.1 LerpColourFromArray @ 0x0014f254:
//   Interpolates within a colour gradient array.
//   t: normalised position [0,1] across the array. arr: array of count Colour
//   entries; count must be >= 1.
//   Guards, in the binary's order:
//     t >= 1.0f             -> arr[count-1]
//     t <= 0.0f || count==1 -> arr[0]
//   Otherwise: scaled=t*(count-1); idx=(int)scaled; frac=fmod(scaled,1.0);
//   result.Lerp(arr[idx+1], arr[idx], frac) -- a=arr[idx+1], b=arr[idx]
//   (binary arg order), which walks arr[idx] -> arr[idx+1] as frac goes 0 -> 1.
//   The `t >= 1.0f` guard is what keeps idx+1 in bounds: without it, t == 1.0f
//   reaches the main path and reads arr[count].
//   No NaN test -- the binary has none. See Colour.cpp for why.
Colour LerpColourFromArray(float t, Colour* arr, int count);

#endif
