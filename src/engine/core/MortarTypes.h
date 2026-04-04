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

// Convenient type aliases
using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;
using uint64 = uint64_t;
using int32 = int32_t;

#endif
