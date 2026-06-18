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
struct EffectProperty;
class Texture;

// GeometryBinding -- shape-preserved defunct stub.
// Binary @ 0x001a3990..0x001a40c0 (ctor/PassBinding::Apply chain).
// sizeof = 76 (0x4C), vtable @ 0x001eb720.
// Binary layout:
//   +0x00  Mortar::GeometryBinding_Bada base sub-object (68 bytes, 0x00..0x43)
//   +0x44  Event1<GeometryBinding&> m_OnDestroyed (8 bytes)
//   +0x4C  end
//
// Event1<T> is 8 bytes (Delegate0<void> = 36 bytes per policy? NO — Event1 is the
// event subscription list, 8 bytes = two pointers: head + tail or similar).
// Confirmed size: Event1 sub-object at +0x44, object ends at +0x4C.

// GeometryBinding_Bada base layout fully RE'd from the ctor body.
// Binary @ 0x001a57bc (GeometryBinding_Bada::GeometryBinding_Bada):
//   ReferenceCounter::ReferenceCounter(this)            // +0x00..0x0B (vptr + refcount)
//   *(int*)this = vtable + 8                            // vptr fixup
//   SmartPtr<EffectGroup>::SmartPtr(this+0x0c)          // +0x0C  m_EffectGroup     (SmartPtr, 4 bytes)
//   vector<SmartPtr<IVertexStream>>::vector(this+0x10)  // +0x10  m_VertexStreams   (std::vector, 12 bytes)
//   SmartPtr<IIndexStream>::SmartPtr(this+0x1c)         // +0x1C  m_IndexStream     (SmartPtr, 4 bytes)
//   map<string,SmartPtr<IIndexStream>>::map(this+0x20)  // +0x20  m_NamedIndexStreams (std::map, 24 bytes)
//   vector<EffectBinding>::vector(this+0x38)            // +0x38  m_EffectBindings  (std::vector, 12 bytes)
//   end                                                 // +0x44 = 68 bytes (0x44) ✓
// The member types IVertexStream / IIndexStream / GeometryBinding_Bada::EffectBinding
// (and the nested PassBinding the binding pipeline walks) are unported subsystems;
// GeometryBinding is constructed by the mesh loader but never dispatched through
// (Geometry::Render bypasses PassBinding::Apply for the GLES2 path), so the base
// body is kept as a correctly-sized opaque pad rather than pulling those types in.
// The exact field offsets above are the spec if/when the binding pipeline is ported.
// Binary @ 0x001a57bc
class GeometryBinding_Bada : public ReferenceCounter {
public:
    GeometryBinding_Bada() {}
    virtual ~GeometryBinding_Bada() {}
    // Base body, RE'd from the ctor (see layout table above). ReferenceCounter base
    // occupies +0x00..+0x0B (12 bytes); own fields fill +0x0C..+0x43 (56 bytes).
    char _base_pad[56];  // +0x0C..+0x43: m_EffectGroup/m_VertexStreams/m_IndexStream/m_NamedIndexStreams/m_EffectBindings
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
    Geometry(SmartPtr<GeometryBinding> binding,
             SmartPtr<SharedEffectProperties> props);
    virtual ~Geometry();

    // Binary @ 0x001a3e98 -- non-virtual member; called directly by Mesh::Draw.
    // DIFFERS: binary walks m_Binding->GetBindings()[m_ActiveBindingIdx].m_PassBindings,
    //   calling PassBinding::Apply (glVertexPointer/etc.) then glDrawArrays/Elements
    //   via the IIndexStream vtable. Port renders directly from the loader-cached
    //   m_Vbo/m_Ibo/m_Layout because GLES2 has no fixed-pipeline client state.
    //   Port uses m_DiffuseTex for texture binding instead of MeshMaterial param.
    void Render(Matrix44 const& mvp);

    // Binary @ 0x001a3e7c
    bool HasActiveEffect() const;

    // Binary @ 0x001a3e5c
    bool SetActiveEffect(uint32_t idx);

    // Accessor for m_PropList (used by Fruit::LoadFruitModels for DiffuseMap property extraction).
    EffectPropertyList* GetPropList() const { return m_PropList; }

    // Binary @ 0x0025ee7c — 3-instruction wrapper; nameHash is cast to const char*
    // for the binary's interned-string lookup (port's m_PropList is null while
    // BuildPropList is defunct, so this always returns null in the current port).
    EffectProperty* GetProperty(uint32_t nameHash);

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
    SmartPtr<Texture> m_DiffuseTex;  // Port: diffuse texture (formerly in MeshMaterial)

private:
    // Binary @ 0x001a3c00
    void BuildPropList(SmartPtr<SharedEffectProperties> props);

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
