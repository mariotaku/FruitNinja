#ifndef FN_ENGINE_RENDER_QUADUTIL_H
#define FN_ENGINE_RENDER_QUADUTIL_H

//
// QuadUtil — SetupQuad() free function.
// v1.6.1: SetupQuad @ 0x00175ca0
// Binary signature (Ghidra + ASM-confirmed AAPCS-VFP arg mapping):
//   bool SetupQuad(QUADCUSTOMVERTEX*, _Vector3<float> centrePos, float width,
//                  float height, MortarRectangleT<float> clipRect, Colour colour)
//
// Fills 4 QUADCUSTOMVERTEX entries (TL, TR, BL, BR order) for a quad centred at
// (centrePos.x, centrePos.y) with full size (width, height), Y-clipped against
// clipRect. U is hardcoded 0.0 (left column) / 1.0 (right column); V is derived
// purely from the Y-clip fractions -- there is NO UV-rect input. Only clipRect.top
// and clipRect.bottom are read (clipRect.left/right are dead in the binary body);
// centrePos.z is likewise unused, present only as part of the _Vector3<float>
// argument shape.
//
// Returns false (no verts written) when the quad is fully clipped out
// (`clippedBottom <= clipRect.top || clipRect.bottom <= clippedTop`), true
// otherwise (verts[0..3] written).
//
// No caller exists on either side -- the binary's only xrefs to 0x00175ca0 are a
// data reference and an unrelated import thunk. Kept for public-API shape.
//

#include "QUADCUSTOMVERTEX.h"
#include "math/_Vector3.h"
#include "math/Colour.h"
#include "core/MortarTypes.h"

// ASM-spec v1.6.1 SetupQuad @0x00175ca0
// Fills verts[0..3] with TL/TR/BL/BR positions (Y-clipped), normals (0,0,1), UV,
// and colour. Caller draws the resulting quad via DrawTriStrip/DrawTriList.
// The three aggregates are passed BY VALUE (binary mangling has no RK), even
// though GCC 4.4.1 AAPCS lowers _Vector3 via an implicit r1 pointer and Colour
// via r2. Parameter ORDER is unchanged from the binary -- only the port's reading
// of each parameter's ROLE was wrong.
bool SetupQuad(QUADCUSTOMVERTEX* verts,
               _Vector3<float> centrePos,
               float width, float height,
               Mortar::MortarRectangleT<float> clipRect,
               Colour colour);

#endif // FN_ENGINE_RENDER_QUADUTIL_H
