// src/engine/asset/MeshDraw.cpp
//
// File-scope immediate-mode primitive drawers.
// In the binary these live under namespace Mortar::Mesh (same as the class),
// which C++ cannot represent when class Mortar::Mesh already occupies that name.
// Port declares them under Mortar::MeshDraw instead — a name-mangling deviation
// that is ACCEPT-cosmetic for symbol-diff purposes.
//
// Most binary stubs (DrawCube, DrawLine, DrawSphere) are documented in the
// Defunct block in Mesh.h and are not compiled here (they were BX LR stubs).
// The live ones are DrawTris + its 2 wrappers + DrawQuadUnCached overloads
// + the DrawQuad textured/transformed entry.

// Analysed: 2026-05-04T00:00

#include "asset/MeshDraw.h"
#include "asset/Mesh.h"
#include "asset/Texture.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "render/MatrixManager.h"
#include "render/MatrixStack.h"
#include "render/Renderer.h"
#include "math/Colour.h"
#include "math/Vec3.h"
#include "math/Matrix44.h"
#include "util/SmartPtr.h"
#include <cmath>

namespace Mortar { namespace MeshDraw {

// Binary @ 0x00193f5c — emit primType triangles from QUADCUSTOMVERTEX[count].
// Port: forwards to Renderer::DrawTriList / DrawTriStrip which handle the
// GLES2 VBO path. The original used GLES1 client-state arrays; the port
// Renderer already provides the equivalent GLES2 implementation.
// TODO: 0x00193f5c — blend flag was applied via glEnable/glDisable(GL_BLEND)
// around the draw call; Renderer does not currently thread that flag through
// DrawTriList/DrawTriStrip. Blend state is left at its current value.
void DrawTris(const QUADCUSTOMVERTEX* verts, long count, int primType, bool /*blend*/, void* /*fx*/) {
    if (!verts || count <= 0) return;
    Renderer* r = Renderer::GetInstance();
    if (!r) return;
    // primType 4 = GL_TRIANGLES, 5 = GL_TRIANGLE_STRIP (same values on GLES2).
    if (primType == 5) {
        r->DrawTriStrip(const_cast<QUADCUSTOMVERTEX*>(verts), (int)count);
    } else {
        r->DrawTriList(const_cast<QUADCUSTOMVERTEX*>(verts), (int)count);
    }
}

// Binary @ 0x0019404c — DrawTris wrapper, primType=GL_TRIANGLES (4)
void DrawTriList(const QUADCUSTOMVERTEX* v, long n, bool blend, void* fx) {
    DrawTris(v, n, 4, blend, fx);
}

// Binary @ 0x00194038 — DrawTris wrapper, primType=GL_TRIANGLE_STRIP (5)
void DrawTriStrip(const QUADCUSTOMVERTEX* v, long n, bool blend, void* fx) {
    DrawTris(v, n, 5, blend, fx);
}

// Binary @ 0x00194060 — emit a unit quad scaled to (w,h) with colour + UVs.
// Matches binary sequence: WorldStack.Reset / Scale(w,h,1) / upload MVP /
// DrawQuad(colour, uOff, vOff, uOff+1, vOff+1).
// When w==0.0f (the no-size overload) the scale defaults to 1x1.
void DrawQuadUnCached(Colour colour, float w, float h, float uOff, float vOff, void* /*fx*/) {
    MatrixManager& mm = MatrixManager::GetInstance();
    MatrixStack& ws = mm.GetWorldStack();
    ws.Reset();
    ws.Scale(Vec3(w == 0.0f ? 1.0f : w, h, 1.0f));
    Renderer* r = Renderer::GetInstance();
    if (!r) return;
    r->DrawQuad(colour, uOff, vOff, uOff + 1.0f, vOff + 1.0f);
}

// Binary @ 0x00194180 — DrawQuadUnCached delegate with default size 1x1
void DrawQuadUnCachedDefault(Colour colour, void* fx) {
    DrawQuadUnCached(colour, 0.0f, 1.0f, 0.0f, 1.0f, fx);
}

// Binary @ 0x001b09b0 — textured + transformed quad.
// Sequence: WorldStack.Reset / Scale(scale) / Translate(pos) / RotZ(rotZ);
// tex->Set; DrawQuadUnCached(colour, w, h, uOff, vOff); tex->UnSet.
// 'texture' is a SmartPtr<Texture>* in the binary; port receives void* and
// casts, matching the binary's SmartPtr dereference at the call site.
void DrawQuad(Colour colour, void* texture,
              const Vec3& pos, const Vec3& scale, float rotZ,
              float w, float h, float uOff, float vOff, void* fx) {
    MatrixManager& mm = MatrixManager::GetInstance();
    MatrixStack& ws = mm.GetWorldStack();
    ws.Reset();
    ws.Scale(scale);
    ws.Translate(pos);
    if (rotZ != 0.0f) {
        ws.m_Current.RotZ44(sinf(rotZ), cosf(rotZ));
        ws.m_Version++;
    }

    SmartPtr<Texture>* texPtr = static_cast<SmartPtr<Texture>*>(texture);
    if (texPtr && texPtr->IsValid()) {
        (*texPtr)->Set();
    }

    DrawQuadUnCached(colour, w, h, uOff, vOff, fx);

    if (texPtr && texPtr->IsValid()) {
        (*texPtr)->UnSet();
    }
}

}} // namespace Mortar::MeshDraw
