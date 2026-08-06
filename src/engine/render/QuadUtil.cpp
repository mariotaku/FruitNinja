//
// QuadUtil.cpp — SetupQuad() implementation.
// v1.6.1: SetupQuad @ 0x00175ca0
//
// ASM-spec v1.6.1 SetupQuad @ 0x00175ca0. Zero callers on either side (the only
// binary xrefs are a data reference and an unrelated 12-byte import thunk at
// 0x00112f14), so this body has no runtime effect -- it is kept for API shape.
//
// AAPCS-VFP register mapping (corrected -- the earlier port permuted three roles):
//   r0      = verts (QUADCUSTOMVERTEX*)
//   r1      = &centrePos (_Vector3<float> lowered to an invisible reference, not an
//             HFA, because _Vector3 has member functions). x,y are the quad CENTRE;
//             z is unused.
//   s0, s1  = width, height (the quad's full size)
//   s2..s5  = clipRect (MortarRectangleT<float>, a genuine 4-float HFA -- the
//             `sub sp,#0x10` in the prologue materialises exactly those 16 bytes).
//             Only clipRect[1] (top) and clipRect[3] (bottom) are ever loaded.
//   r2      = &colour (Colour::PlatformColour(this) is called per vertex inside
//             the loop, confirming this-pointer-style indirect passing)
//
// Pre-loop math derives the Y-clipped top/bottom edges + their V-coordinates:
//   naturalTop    = centrePos.y - height * 0.5
//   naturalBottom = centrePos.y + height * 0.5
//   if (naturalTop < clipRect.top): clippedTop = clipRect.top;
//                                   vTop = 0.5 - (clipRect.top - centrePos.y) / height
//   else:                           clippedTop = naturalTop; vTop = 1.0
//   if (naturalBottom <= clipRect.bottom): clippedBottom = naturalBottom; vBottom = 0.0
//   else:                                  clippedBottom = clipRect.bottom;
//                                          vBottom = 0.5 - (clipRect.bottom - centrePos.y) / height
//   fully clipped out (return false, no verts written) when
//     clippedBottom <= clipRect.top || clipRect.bottom <= clippedTop
//
// Loop (verts[0..3] = TL,TR,BL,BR):
//   U: 0.0 for left column (verts 0,2), 1.0 for right column (verts 1,3)
//   X: centrePos.x -/+ width * 0.5 (left/right column)
//   Y,V: (clippedTop, vTop) for top row (verts 0,1); (clippedBottom, vBottom) for
//        bottom row (verts 2,3)
//   z=0, nx=ny=0, nz=1 (facing viewer), colour = Colour::PlatformColour() every vert.
//

#include "QuadUtil.h"

bool SetupQuad(QUADCUSTOMVERTEX* verts,
               _Vector3<float> centrePos,
               float width, float height,
               Mortar::MortarRectangleT<float> clipRect,
               Colour colour)
{
    float naturalTop    = centrePos.y + height * -0.5f;
    float clippedTop    = naturalTop;
    float vTop          = 1.0f;
    if (naturalTop < clipRect.top) {
        vTop       = 0.5f - (clipRect.top - centrePos.y) / height;
        clippedTop = clipRect.top;
    }

    float naturalBottom  = centrePos.y + height * 0.5f;
    float clippedBottom  = naturalBottom;
    float vBottom        = 0.0f;
    if (naturalBottom > clipRect.bottom) {
        vBottom       = 0.5f - (clipRect.bottom - centrePos.y) / height;
        clippedBottom = clipRect.bottom;
    }

    if (clippedBottom <= clipRect.top || clipRect.bottom <= clippedTop) {
        return false;
    }

    for (int i = 0; i < 4; ++i) {
        // Binary calls Colour::PlatformColour(this) once per vertex, inside the
        // loop (not hoisted) -- preserved here even though the result is invariant.
        const uint32_t packedColour = colour.PlatformColour();
        const bool leftCol = (i & 1) == 0;

        verts[i].x  = leftCol ? (centrePos.x + width * -0.5f)
                              : (centrePos.x + width *  0.5f);
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
