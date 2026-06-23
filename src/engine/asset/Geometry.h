#ifndef FN_ENGINE_ASSET_GEOMETRY_H
#define FN_ENGINE_ASSET_GEOMETRY_H

#include "util/SmartPtr.h"
#include "util/ReferenceCounter.h"
#include "util/AsciiString.h"
#include "render/gl_funcs.h"
#include "math/Matrix44.h"
#include <cstring>
#include <cstdint>
#include <vector>

namespace Mortar {

class SharedEffectProperties;
class EffectPropertyList;
struct EffectProperty;
class Texture;
class IVertexStream;
class IIndexStream;
class EffectGroup;

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
// Fields used by LoadMesh (VertexStreamAdd/IndexStreamSet/EffectGroupSet) are exposed
// as real typed fields. m_NamedIndexStreams and m_EffectBindings are padded because
// their element types (EffectBinding/PassBinding) are unported subsystems.
// Geometry::Render bypasses PassBinding::Apply entirely (GLES2 path).
// Binary @ 0x001a57bc
class GeometryBinding_Bada : public ReferenceCounter {
public:
    GeometryBinding_Bada();
    virtual ~GeometryBinding_Bada();

    SmartPtr<EffectGroup>                  m_EffectGroup;   // +0x0C, 4 bytes
    std::vector<SmartPtr<IVertexStream> >  m_VertexStreams; // +0x10, 12 bytes
    SmartPtr<IIndexStream>                 m_IndexStream;   // +0x1C, 4 bytes
    // TODO: v1.6.1 0x001a57bc (GeometryBinding_Bada) -- m_NamedIndexStreams: map<AsciiString,SmartPtr<IIndexStream>> (24B)
    char _pad_namedstreams[24];  // +0x20..+0x37: m_NamedIndexStreams placeholder
    // TODO: v1.6.1 0x001a57bc (GeometryBinding_Bada) -- m_EffectBindings: vector<EffectBinding> (12B, EffectBinding unported)
    char _pad_effectbindings[12]; // +0x38..+0x43: m_EffectBindings placeholder
};

// Event1<GeometryBinding&> placeholder (8 bytes per binary record).
// Full template not ported; this POD stub has the correct size.
struct Event1_GeometryBinding {
    void* _ptr0;  // +0x00
    void* _ptr1;  // +0x04
    Event1_GeometryBinding() : _ptr0(0), _ptr1(0) {}
};

// GeometryBinding -- constructed by LoadMesh to hold stream references and
// the EffectGroup pointer. Render-time use (PassBinding::Apply chain) is defunct
// in the port -- Geometry::Render uses a direct GLES2 path instead.
// Binary @ 0x001a3990 (ctor zeros all), 0x002640c8 (VertexStreamAdd),
//          0x001a4f90 (IndexStreamSet), 0x001a00f8 (EffectGroupSet).
class GeometryBinding : public GeometryBinding_Bada {
public:
    // Binary @ 0x001a3990
    GeometryBinding();
    virtual ~GeometryBinding();

    Event1_GeometryBinding m_OnDestroyed;  // +0x44 (8 bytes)

    // Binary @ 0x002640c8 -- find-or-push_back into m_VertexStreams.
    // v1.6.1 LoadMesh @0x0023890c calls this after EffectGroupSet.
    void VertexStreamAdd(SmartPtr<IVertexStream> stream);

    // Binary @ 0x001a4f90 -- sets m_IndexStream; name ignored by LoadMesh (empty string).
    // v1.6.1 LoadMesh @0x0023890c calls this before VertexStreamAdd.
    void IndexStreamSet(SmartPtr<IIndexStream> stream, const AsciiString& name);

    // Binary @ 0x001a00f8 -- stores EffectGroup ptr into m_EffectGroup.
    // v1.6.1 LoadMesh @0x0023890c calls this after creating the binding.
    // Binary body is minimal (1-2 instructions in the stub); shape preserved.
    void EffectGroupSet(SmartPtr<EffectGroup> group);
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

#if defined(__bada__)
#include <cstddef>
static_assert(sizeof(Mortar::GeometryBinding_Bada) == 68, "GeometryBinding_Bada size mismatch");
static_assert(sizeof(Mortar::GeometryBinding)      == 76, "GeometryBinding size mismatch (0x4C)");
static_assert(offsetof(Mortar::GeometryBinding, m_OnDestroyed)  == 68, "GeometryBinding::m_OnDestroyed offset");
static_assert(offsetof(Mortar::GeometryBinding_Bada, m_EffectGroup)    == 0x0c, "GeometryBinding_Bada::m_EffectGroup offset");
static_assert(offsetof(Mortar::GeometryBinding_Bada, m_VertexStreams)   == 0x10, "GeometryBinding_Bada::m_VertexStreams offset");
static_assert(offsetof(Mortar::GeometryBinding_Bada, m_IndexStream)     == 0x1c, "GeometryBinding_Bada::m_IndexStream offset");
static_assert(offsetof(Mortar::GeometryBinding_Bada, _pad_namedstreams) == 0x20, "GeometryBinding_Bada::_pad_namedstreams offset");
static_assert(offsetof(Mortar::GeometryBinding_Bada, _pad_effectbindings) == 0x38, "GeometryBinding_Bada::_pad_effectbindings offset");
#endif

#endif  // FN_ENGINE_ASSET_GEOMETRY_H
