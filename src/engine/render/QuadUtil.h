#ifndef FN_ENGINE_RENDER_QUADUTIL_H
#define FN_ENGINE_RENDER_QUADUTIL_H

//
// QuadUtil — SetupQuad() free function.
// v1.6.1: SetupQuad @ 0x00175ca0
// Binary signature (Ghidra + ASM-confirmed AAPCS-VFP arg mapping):
//   bool SetupQuad(QUADCUSTOMVERTEX*, _Vector3<float> centerSize, float clipBottom,
//                  float clipTop, MortarRectangleT<float> const& rect, Colour const&)
//
// Fills 4 QUADCUSTOMVERTEX entries (TL, TR, BL, BR order) for a quad centred at
// (rect.left, rect.top) with full size (centerSize.x, centerSize.y), Y-clipped
// against [clipTop, clipBottom]. U is hardcoded 0.0 (left column) / 1.0 (right
// column); V is derived purely from the Y-clip fractions -- there is NO separate
// UV-rect input despite `rect` being a MortarRectangleT. Only rect.left/rect.top
// are read by this function (rect.right/rect.bottom are dead in the binary body --
// confirmed via ASM: no load of the two trailing rect fields anywhere in the
// function). centerSize.z is likewise unused (present only as part of the
// _Vector3<float> AAPCS-VFP argument shape).
//
// Returns false (no verts written) when the quad is fully clipped out
// (`bottom <= clipTop || clipBottom <= top`, using the natural unclipped
// top/bottom of the quad), true otherwise (verts[0..3] written).
//

#include "QUADCUSTOMVERTEX.h"
#include "math/_Vector3.h"
#include "math/Colour.h"
#include "core/MortarTypes.h"

// ASM-verified: 2026-07-15T09:12Z v1.6.1 SetupQuad @ 0x00175ca0 (asm-inspector)
// Fills verts[0..3] with TL/TR/BL/BR positions (Y-clipped), normals (0,0,1), UV,
// and colour. Caller draws the resulting quad via DrawTriStrip/DrawTriList.
// The three aggregates are passed BY VALUE (binary mangling has no RK), even
// though GCC 4.4.1 AAPCS lowers _Vector3 via an implicit r1 pointer.
bool SetupQuad(QUADCUSTOMVERTEX* verts,
               _Vector3<float> centerSize,
               float clipBottom, float clipTop,
               Mortar::MortarRectangleT<float> rect,
               Colour colour);

#endif // FN_ENGINE_RENDER_QUADUTIL_H
