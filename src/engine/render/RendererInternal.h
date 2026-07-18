#ifndef MORTAR_RENDERER_INTERNAL_H
#define MORTAR_RENDERER_INTERNAL_H

#include <cstdint>

// Shared between Renderer.cpp (portable public API / quad builders) and
// RendererGL.cpp (GL-backend draw implementations). Not part of the public
// Renderer.h API -- internal vertex layout only.

// Interleaved vertex for the quad paths (DrawQuad / DrawColorQuad /
// draw_fullscreen_quad). QUADCUSTOMVERTEX keeps its own binary layout and
// is fed to DrawShaded2D with its own offsets.
struct Shaded2DVertex {
    float x, y, z;
    float u, v;
    uint32_t color;   // Colour::PlatformColour() packing (LE bytes r,g,b,a)
};

#endif
