#ifndef FN_ENGINE_ASSET_GEOMETRY_H
#define FN_ENGINE_ASSET_GEOMETRY_H

#include "util/SmartPtr.h"
#include "util/ReferenceCounter.h"
#include "render/gl_funcs.h"
#include "math/Matrix44.h"
#include <cstring>
#include <cstdint>

namespace Mortar {

class SharedEffectProperties;
class EffectPropertyList;
struct MeshMaterial;

// GeometryBinding -- shape-preserved defunct stub.
// Binary @ 0x001a3990..0x001a40c0 (ctor/PassBinding::Apply chain).
// sizeof = 76 (0x4C), vtable @ 0x001eb720.
// Binary layout:
//   +0x00  Mortar::GeometryBinding_Bada base sub-object (68 bytes, 0x00..0x43)
//              vptr at +0x00; base fields (vertex stream(s), index stream,
//              effect-group SmartPtrs) at +0x04..+0x43 — layout NOT YET RE'd.
//   +0x44  Event1<GeometryBinding&> m_OnDestroyed (8 bytes)
//   +0x4C  end
//
// TODO: 0x001a3990 -- GeometryBinding_Bada base layout (0x04..0x43) not yet RE'd;
//   GeometryBinding_Bada body is a 1-byte Ghidra placeholder. RE base separately
//   to fill in VertexStreamAdd/IndexStreamSet/EffectGroupSet field offsets.
//
// Event1<T> is 8 bytes (Delegate0<void> = 36 bytes per policy? NO — Event1 is the
// event subscription list, 8 bytes = two pointers: head + tail or similar).
// Confirmed size: Event1 sub-object at +0x44, object ends at +0x4C.
class GeometryBinding_Bada : public ReferenceCounter {
public:
    GeometryBinding_Bada() {}
    virtual ~GeometryBinding_Bada() {}
    // TODO: 0x001a3990 -- GeometryBinding_Bada own fields at +0x0C..+0x43 (56 bytes) not yet RE'd
    // (ReferenceCounter base occupies +0x00..+0x0B = 12 bytes; own fields fill +0x0C..+0x43)
    char _base_pad[56];  // +0x0C..+0x43 opaque base body; binary layout unknown
};

// Event1<GeometryBinding&> placeholder (8 bytes per binary record).
// Full template not ported; this POD stub has the correct size.
struct Event1_GeometryBinding {
    void* _ptr0;  // +0x00
    void* _ptr1;  // +0x04
    Event1_GeometryBinding() : _ptr0(0), _ptr1(0) {}
};

// Defunct: GeometryBinding -- no-op stub preserving binary layout; binary @ 0x001a3990
// Live class used by the mesh loader (VertexStreamAdd/IndexStreamSet/EffectGroupSet).
// Port's Geometry::Render bypasses the PassBinding::Apply chain entirely (GLES2 path),
// so GeometryBinding is constructed by the loader but never dispatched through.
class GeometryBinding : public GeometryBinding_Bada {
public:
    // Defunct: GeometryBinding -- no-op stub; binary @ 0x001a3990
    GeometryBinding() {}
    virtual ~GeometryBinding() {}

    Event1_GeometryBinding m_OnDestroyed;  // +0x44 (8 bytes)
};

// Vertex attribute layout (from PSP vertex declaration).
// Port specific: replaces the original Effect/Geometry/GeometryBinding system.
// Defined here (in Geometry.h) so Geometry can embed it by value.
// Mesh.h includes Geometry.h and provides MeshMaterial; both are accessible
// to any TU that includes either header.
struct VertexLayout {
    int posOffset;    int posSize;     // 3 floats typically
    int normalOffset; int normalSize;  // 3 floats or 3 shorts
    int colorOffset;  int colorSize;   // 0, 2, or 4 bytes
    int colorFmt;                      // 0=none, 1=BGR5650, 2=ABGR5551, 3=RGBA8888
    int texOffset;    int texSize;     // 2 floats
    int totalStride;
};

// Shape-preserved port of Mortar::Geometry (binary @ 0x001a3c50 ctor; sizeof 0x18).
// Binary fields preserved at canonical offsets +0x0C..+0x14 relative to base.
// Port appends VBO/IBO/material data after the binary fields; the binary's
// binding-stack pipeline (PassBinding::Apply etc.) is defunct because GLES2
// shaders replace the fixed-pipeline client-state calls.
class Geometry : public ReferenceCounter {
public:
    // Binary @ 0x001a3c50 (C1) / 0x001a3cc4 (C2)
    Geometry(SmartPtr<GeometryBinding> const& binding,
             SmartPtr<SharedEffectProperties> const& props);
    virtual ~Geometry();

    // Binary @ 0x001a3e98 -- non-virtual member; called directly by Mesh::Draw.
    // DIFFERS: binary walks m_Binding->GetBindings()[m_ActiveBindingIdx].m_PassBindings,
    //   calling PassBinding::Apply (glVertexPointer/etc.) then glDrawArrays/Elements
    //   via the IIndexStream vtable. Port renders directly from the loader-cached
    //   m_Vbo/m_Ibo/m_Layout because GLES2 has no fixed-pipeline client state.
    void Render(MeshMaterial const& mat, Matrix44 const& mvp);

    // Binary @ 0x001a3e7c
    bool HasActiveEffect() const;

    // Binary @ 0x001a3e5c
    bool SetActiveEffect(uint32_t idx);

    // === port-only fields populated by MeshManager loader; appended after the
    //     binary fields so m_ActiveBindingIdx/m_Binding/m_PropList stay at
    //     canonical offsets ===
    GLuint       m_Vbo;
    GLuint       m_Ibo;
    int          m_VertCount;
    int          m_IndexCount;
    GLenum       m_PrimType;
    int          m_MaterialIndex;
    VertexLayout m_Layout;

private:
    // Binary @ 0x001a3c00
    void BuildPropList(SmartPtr<SharedEffectProperties> const& props);

    // === binary fields (offsets +0x0C..+0x14 from ReferenceCounter base) ===
    uint32_t                          m_ActiveBindingIdx;  // +0x0c
    SmartPtr<GeometryBinding>         m_Binding;           // +0x10
    EffectPropertyList*               m_PropList;          // +0x14 (always NULL in port; BuildPropList is defunct)
    // === end binary fields ===
};

}  // namespace Mortar

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
#include <cstddef>
static_assert(sizeof(Mortar::GeometryBinding_Bada) == 68, "GeometryBinding_Bada size mismatch");
static_assert(sizeof(Mortar::GeometryBinding)      == 76, "GeometryBinding size mismatch (0x4C)");
static_assert(offsetof(Mortar::GeometryBinding, m_OnDestroyed) == 68, "GeometryBinding::m_OnDestroyed offset");
#endif

#endif  // FN_ENGINE_ASSET_GEOMETRY_H
