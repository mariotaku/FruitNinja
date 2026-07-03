#ifndef MORTAR_TYPES_H
#define MORTAR_TYPES_H

#include <cstdint>
#include "math/Vec2.h"

// Screen constants (use FN_ prefix to avoid MSYS2 conflicts)
#define FN_SCREEN_W 480
#define FN_SCREEN_H 320

namespace Mortar {

// MortarRectangle matching original (16 bytes)
struct MortarRectangle {
    int left, top, right, bottom;

    int Width() const { return right - left; }
    int Height() const { return bottom - top; }
};

// MortarRectangleT<T> — float/double typed rect; binary uses MortarRectangleT<float>
// for clip rects (e.g. scissor rect passed to SetupQuad). Cross-build: template
// bodies must be in-header; GCC 4.4 handles this fine.
template<typename T>
struct MortarRectangleT {
    T left, top, right, bottom;

    T Width()  const { return right - left; }
    T Height() const { return bottom - top; }

    // v1.6.1 MortarRectangleT<float>::Scale(float) @0x0024e194 -- uniform scale of all 4 edges.
    void Scale(T s) { left *= s; top *= s; right *= s; bottom *= s; }

    // v1.6.1 MortarRectangleT<float>::Centre() @0x001a3900 -- returns rect midpoint (binary: _Point2D<float>).
    Vec2 Centre() const { return Vec2((left + right) * (T)0.5, (top + bottom) * (T)0.5); }
};

// Binary v1.6.1 Mortar::ALIGNMENT_TYPE (1 byte under -fshort-enums).
// Bits 0-1: H-align (3=centre, 2=right, 0/1=left). Bits 2-3: V-align (0xc=centre-V).
// Values verified from BakedStringTTF::Draw mangled sig + BakedStringBox ctor callers.
enum ALIGNMENT_TYPE {
    ALIGN_LEFT     = 0x0,
    ALIGN_RIGHT    = 0x2,
    ALIGN_H_CENTRE = 0x3,
    ALIGN_V_CENTRE = 0xc,
    ALIGN_CENTRE   = 0xf,
};

} // namespace Mortar

// Convenient type aliases. typedef rather than `using` so the cross-build
// (GCC 4.4/4.5 for asm-verify) parses without -std=c++11 (alias-decls came
// in 4.7).
typedef uint8_t  uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;
typedef int32_t  int32;

#endif
