#ifndef MORTAR_QUADCUSTOMVERTEX_H
#define MORTAR_QUADCUSTOMVERTEX_H

#include <cstdint>

// Matches original QUADCUSTOMVERTEX (0x24 = 36 bytes)
// Used by DrawTriList/DrawTriStrip for 3D and HUD rendering
struct QUADCUSTOMVERTEX {
    float x, y, z;       // +0x00: position
    float nx, ny, nz;    // +0x0C: normal
    uint32_t colour;     // +0x18: packed BGRA
    float u, v;          // +0x1C: texture coordinates
};
static_assert(sizeof(QUADCUSTOMVERTEX) == 36, "QUADCUSTOMVERTEX must be 36 bytes");

// Compact vertex for DrawQuadUnCached (0x14 = 20 bytes)
// Used by 2D immediate-mode quad rendering
struct QuadVertex {
    float x, y;          // position (2D)
    float u, v;          // texcoords
    uint32_t color;      // packed BGRA
};
static_assert(sizeof(QuadVertex) == 20, "QuadVertex must be 20 bytes");

#endif
