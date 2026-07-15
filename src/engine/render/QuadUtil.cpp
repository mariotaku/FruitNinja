//
// QuadUtil.cpp — SetupQuad() implementation.
// v1.6.1: SetupQuad @ 0x00175ca0
//
// ASM-spec v1.6.1 SetupQuad @ 0x00175ca0 (disassemble_function + AAPCS-VFP register
// trace via GhidraMCP get_function_variables; 0 real callers in the binary -- the only
// xrefs are a data reference and an unrelated 12-byte import-thunk stub at 0x00112f14).
//
// Register mapping confirmed from ASM:
//   r0 = verts (QUADCUSTOMVERTEX*)
//   s0,s1,s2 = centerSize.{x,y,z} (q0, AAPCS-VFP homogeneous-float-aggregate arg)
//   s3 = clipBottom, s4 = clipTop
//   r1 = &rect (MortarRectangleT<float> passed by pointer -- NOT HFA-classified,
//        likely because MortarRectangleT has member functions; only rect[0]=left and
//        rect[1]=top are ever loaded, at 0x00175cac/0x00175cb0)
//   r2 = &colour (Colour passed by pointer; Colour::PlatformColour(this) called per
//        vertex inside the loop, confirming `this`-pointer-style indirect passing)
//
// Pre-loop math derives the Y-clipped top/bottom edges + their V-coordinates:
//   naturalTop    = rect.top - centerSize.y * 0.5
//   naturalBottom = rect.top + centerSize.y * 0.5
//   if (naturalTop < clipTop):    clippedTop = clipTop;    vTop = 0.5 - (clipTop - rect.top) / centerSize.y
//   else:                          clippedTop = naturalTop; vTop = 1.0
//   if (naturalBottom <= clipBottom): clippedBottom = naturalBottom; vBottom = 0.0
//   else:                              clippedBottom = clipBottom;   vBottom = 0.5 - (clipBottom - rect.top) / centerSize.y
//   fully clipped out (return false, no verts written) when
//     clippedBottom <= clipTop || clipBottom <= clippedTop
//
// Loop (verts[0..3] = TL,TR,BL,BR, matching existing port vertex order):
//   U: 0.0 for left column (verts 0,2), 1.0 for right column (verts 1,3)
//   X: rect.left -/+ centerSize.x * 0.5 (left/right column)
//   Y,V: (clippedTop, vTop) for top row (verts 0,1); (clippedBottom, vBottom) for
//        bottom row (verts 2,3)
//   z=0, nx=ny=0, nz=1 (facing viewer), colour = Colour::PlatformColour() every vert.
//

#include "QuadUtil.h"

bool SetupQuad(QUADCUSTOMVERTEX* verts,
               _Vector3<float> centerSize,
               float clipBottom, float clipTop,
               Mortar::MortarRectangleT<float> rect,
               Colour colour)
{
    float naturalTop    = rect.top + centerSize.y * -0.5f;
    float clippedTop    = naturalTop;
    float vTop          = 1.0f;
    if (naturalTop < clipTop) {
        vTop       = 0.5f - (clipTop - rect.top) / centerSize.y;
        clippedTop = clipTop;
    }

    float naturalBottom  = rect.top + centerSize.y * 0.5f;
    float clippedBottom  = naturalBottom;
    float vBottom        = 0.0f;
    if (naturalBottom > clipBottom) {
        vBottom      = 0.5f - (clipBottom - rect.top) / centerSize.y;
        clippedBottom = clipBottom;
    }

    if (clippedBottom <= clipTop || clipBottom <= clippedTop) {
        return false;
    }

    for (int i = 0; i < 4; ++i) {
        // Binary calls Colour::PlatformColour(this) once per vertex, inside the
        // loop (not hoisted) -- preserved here even though the result is invariant.
        const uint32_t packedColour = colour.PlatformColour();
        const bool leftCol = (i & 1) == 0;

        verts[i].x  = leftCol ? (rect.left + centerSize.x * -0.5f)
                               : (rect.left + centerSize.x *  0.5f);
        verts[i].y  = (i < 2) ? clippedTop : clippedBottom;
        verts[i].z  = 0.0f;
        verts[i].nx = 0.0f;
        verts[i].ny = 0.0f;
        verts[i].nz = 1.0f;
        verts[i].colour = packedColour;
        verts[i].u  = leftCol ? 0.0f : 1.0f;
        verts[i].v  = (i < 2) ? vTop : vBottom;
    }

    return true;
}
