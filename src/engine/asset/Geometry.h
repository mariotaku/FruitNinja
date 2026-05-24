#ifndef FN_ENGINE_ASSET_GEOMETRY_H
#define FN_ENGINE_ASSET_GEOMETRY_H

#include "util/SmartPtr.h"
#include "util/ReferenceCounter.h"
#include "render/gl_funcs.h"
#include "math/Matrix44.h"
#include <memory>
#include <cstring>
#include <cstdint>

namespace Mortar {

class SharedEffectProperties;
class EffectPropertyList;
struct MeshMaterial;

// GeometryBinding -- shape-preserved defunct stub.
// Binary @ 0x001a3990..0x001a40c0 (ctor/PassBinding::Apply chain).
// The PassBinding::Apply chain is not ported; port's Geometry::Render reads
// m_Vbo/m_Ibo/m_Layout directly. Class defined here so SmartPtr<GeometryBinding>
// compiles in any TU that includes Geometry.h.
// Defunct: GeometryBinding/PassBinding stack -- no-op stub; binary @ 0x001a3990
class GeometryBinding : public ReferenceCounter {
public:
    // Defunct: GeometryBinding -- no-op stub; binary @ 0x001a3990
    GeometryBinding() {}
    virtual ~GeometryBinding() {}
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
    std::auto_ptr<EffectPropertyList> m_PropList;          // +0x14
    // === end binary fields ===
};

}  // namespace Mortar

#endif  // FN_ENGINE_ASSET_GEOMETRY_H
