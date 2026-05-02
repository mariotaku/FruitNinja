#ifndef MORTAR_TYPES_H
#define MORTAR_TYPES_H

#include <cstdint>

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
