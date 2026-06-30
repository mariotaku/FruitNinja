//
// QuadUtil.cpp — SetupQuad() implementation.
// v1.6.1: SetupQuad @ 0x00175ca0
//
// Fills 4 QUADCUSTOMVERTEX entries (TL, TR, BL, BR order) for a 2D quad.
// Normals are hardcoded (0,0,1) — facing the viewer, matching the binary.
//
// TODO: v1.6.1 SetupQuad @0x00175ca0 Y-axis scissor path (clip top/bottom
//   against a global MortarRectangleT<float> clip rect and recompute v0/v1
//   proportionally). Requires RE of the clip rect global address.
//

#include "QuadUtil.h"

// ASM-spec v1.6.1 SetupQuad @ 0x00175ca0
void SetupQuad(QUADCUSTOMVERTEX* verts,
               float left, float top, float right, float bottom,
               float u0, float v0, float u1, float v1,
               uint32_t colour)
{
    // TL
    verts[0].x  = left;  verts[0].y  = top;    verts[0].z  = 0.0f;
    verts[0].nx = 0.0f;  verts[0].ny = 0.0f;   verts[0].nz = 1.0f;
    verts[0].colour = colour;
    verts[0].u  = u0;    verts[0].v  = v0;

    // TR
    verts[1].x  = right; verts[1].y  = top;    verts[1].z  = 0.0f;
    verts[1].nx = 0.0f;  verts[1].ny = 0.0f;   verts[1].nz = 1.0f;
    verts[1].colour = colour;
    verts[1].u  = u1;    verts[1].v  = v0;

    // BL
    verts[2].x  = left;  verts[2].y  = bottom; verts[2].z  = 0.0f;
    verts[2].nx = 0.0f;  verts[2].ny = 0.0f;   verts[2].nz = 1.0f;
    verts[2].colour = colour;
    verts[2].u  = u0;    verts[2].v  = v1;

    // BR
    verts[3].x  = right; verts[3].y  = bottom; verts[3].z  = 0.0f;
    verts[3].nx = 0.0f;  verts[3].ny = 0.0f;   verts[3].nz = 1.0f;
    verts[3].colour = colour;
    verts[3].u  = u1;    verts[3].v  = v1;
}
