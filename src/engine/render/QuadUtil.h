#ifndef FN_ENGINE_RENDER_QUADUTIL_H
#define FN_ENGINE_RENDER_QUADUTIL_H

//
// QuadUtil — SetupQuad() free function.
// v1.6.1: SetupQuad @ 0x00175ca0 (_Z9SetupQuadP17QUADCUSTOMVERTEXfffffffff j)
//
// Fills 4 QUADCUSTOMVERTEX entries (TL, TR, BL, BR) for a clip-space quad
// with position rect [left, right] x [top, bottom], UV rect [u0,u1] x [v0,v1],
// and packed BGRA colour.
//
// The binary also applies Y-axis scissoring against a global MortarRectangleT<float>
// clip rect (adjusting top/bottom and recomputing v0/v1 proportionally) — that
// clip path is not yet ported (TODO: v1.6.1 SetupQuad @0x00175ca0 scissor path).
//

#include "QUADCUSTOMVERTEX.h"
#include <cstdint>

// SetupQuad (v1.6.1) @ 0x00175ca0
// Fills verts[0..3] with TL/TR/BL/BR positions, normals (0,0,1), UV, and colour.
// Caller is responsible for drawing the resulting quad via DrawTriStrip/DrawTriList.
void SetupQuad(QUADCUSTOMVERTEX* verts,
               float left, float top, float right, float bottom,
               float u0, float v0, float u1, float v1,
               uint32_t colour);

#endif // FN_ENGINE_RENDER_QUADUTIL_H
