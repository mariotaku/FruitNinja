#include "Colour.h"

#include <cstdio>

// Binary @ 0x00183f58 -- Colour::Lerp(Colour const&, float) const
// Builds local copies of `a` and `*this`, then delegates to the 3-arg
// overload which writes the interpolated result into `*this`. The binary marks
// this overload const yet mutates *this through the non-const 3-arg Lerp using
// the same `this` pointer, so we cast away const to match that behaviour.
Colour* Colour::Lerp(Colour const& a, float t) const {
    Colour ca = a;
    Colour cb = *this;
    return const_cast<Colour*>(this)->Lerp(ca, cb, t);
}

// Binary @ 0x00183e98 -- Colour::Lerp(Colour, Colour, float)
// this = a; then per channel  this -= (b - a) * t  with a clamp to >= 0.
// The binary computes (b - a) as a signed int subtraction of the unsigned
// byte channels, converts to float (VectorSignedToFloat == int->float vcvt),
// multiplies by t, subtracts from the float-converted channel of *this (which
// now holds a), truncates toward zero, and clamps negatives to 0 via the
// idiom  (0.0 < f) * (char)(int)f .
Colour* Colour::Lerp(Colour a, Colour b, float t) {
    *this = a;

    // Channel order in the binary's BGRA struct: offset 0=b, 1=g, 2=r, 3=a.
    float db = (float)((int)b.b - (int)a.b);
    float dg = (float)((int)b.g - (int)a.g);
    float dr = (float)((int)b.r - (int)a.r);
    float da = (float)((int)b.a - (int)a.a);

    float fr = (float)this->r - dr * t;
    float fg = (float)this->g - dg * t;
    float fb = (float)this->b - db * t;
    float fa = (float)this->a - da * t;

    this->r = (uint8_t)((0.0f < fr) ? (char)(int)fr : 0);
    this->g = (uint8_t)((0.0f < fg) ? (char)(int)fg : 0);
    this->b = (uint8_t)((0.0f < fb) ? (char)(int)fb : 0);
    this->a = (uint8_t)((0.0f < fa) ? (char)(int)fa : 0);
    return this;
}

// Static colour constants -- binary has these as BSS-zero-init statics,
// set by a global ctor. Port defines them const for safety.
// TODO: verify exact binary addresses / ctor site via Ghidra.
const Colour Colour::Red(255, 0, 0, 255);
const Colour Colour::White(255, 255, 255, 255);
const Colour Colour::Black(0, 0, 0, 255);

// Binary @ 0x00183f98 -- Colour::ToString() const
// snprintf into a single shared 0x100 static buffer; format is
// "%d, %d, %d, %d (argb)" with args (a, r, g, b). Returns the buffer.
char* Colour::ToString() const {
    static char s_buf[0x100];
    snprintf(s_buf, sizeof(s_buf), "%d, %d, %d, %d (argb)",
             (unsigned)this->a, (unsigned)this->r,
             (unsigned)this->g, (unsigned)this->b);
    return s_buf;
}
