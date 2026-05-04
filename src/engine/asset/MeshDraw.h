#ifndef FN_ENGINE_ASSET_MESHDRAW_H
#define FN_ENGINE_ASSET_MESHDRAW_H

// File-scope immediate-mode primitive drawers.
// In the binary these live under namespace Mortar::Mesh (same as the class).
// C++ cannot have a namespace and a class with the same unqualified name in
// the same enclosing namespace, so the port declares them under
// Mortar::MeshDraw — a name-mangling deviation that is ACCEPT-cosmetic for
// the asm-verify symbol diff.
//
// Implementation: MeshDraw.cpp

#include "math/Colour.h"
#include "math/Vec3.h"

struct QUADCUSTOMVERTEX;

namespace Mortar { namespace MeshDraw {

// Binary @ 0x00193f5c — emit primType triangles from QUADCUSTOMVERTEX[count]
void DrawTris(const QUADCUSTOMVERTEX* verts, long count, int primType, bool blend, void* fx);

// Binary @ 0x0019404c — DrawTris wrapper, primType=GL_TRIANGLES (4)
void DrawTriList(const QUADCUSTOMVERTEX* v, long n, bool blend, void* fx);

// Binary @ 0x00194038 — DrawTris wrapper, primType=GL_TRIANGLE_STRIP (5)
void DrawTriStrip(const QUADCUSTOMVERTEX* v, long n, bool blend, void* fx);

// Binary @ 0x00194060 — emit a unit quad scaled to (w,h) with colour + UVs
void DrawQuadUnCached(Colour colour, float w, float h, float uOff, float vOff, void* fx);

// Binary @ 0x00194180 — DrawQuadUnCached delegate with default size 1x1
void DrawQuadUnCachedDefault(Colour colour, void* fx);

// Binary @ 0x001b09b0 — textured + transformed quad
void DrawQuad(Colour colour, void* texture /* SmartPtr<Texture>* */,
              const Vec3& pos, const Vec3& scale, float rotZ,
              float w, float h, float uOff, float vOff, void* fx);

}} // namespace Mortar::MeshDraw

#endif
