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
// v1.6.1 Mortar::GeometryBinding::GeometryBinding @0x00263e90 (ctor; PassBinding::Apply chain in the GLES1 binding).
// sizeof = 76 (0x4C). TODO: re-verify v1.6.1 vtable address (was: 0x001eb720 stale v1.5.x).
// Binary layout:
//   +0x00  Mortar::GeometryBinding_Bada base sub-object (68 bytes, 0x00..0x43)
//   +0x44  Event1<GeometryBinding&> m_OnDestroyed (8 bytes)
//   +0x4C  end
//
// Event1<T> is 8 bytes (Delegate0<void> = 36 bytes per policy? NO — Event1 is the
// event subscription list, 8 bytes = two pointers: head + tail or similar).
// Confirmed size: Event1 sub-object at +0x44, object ends at +0x4C.

// GeometryBinding_Bada base layout fully RE'd from the ctor body.
// TODO: re-verify v1.6.1 GeometryBinding_Bada ctor (symbol absent in Bada v1.6.1; binding is GeometryBinding_GLES1, base folded into GeometryBinding @0x00263e90; was: 0x001a57bc stale v1.5.x):
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
// Geometry::Render draws from load-cached m_Vbo/m_Ibo/m_Layout rather than walking PassBinding::Apply
// (structural divergence; both paths are fixed-function GLES1.x -- NOT a GLES2 shader path).
// TODO: re-verify v1.6.1 GeometryBinding_Bada ctor address (symbol absent in Bada v1.6.1 binary; was: 0x001a57bc stale v1.5.x)
class GeometryBinding_Bada : public ReferenceCounter {
public:
    GeometryBinding_Bada();
    virtual ~GeometryBinding_Bada();

    SmartPtr<EffectGroup>                  m_EffectGroup;   // +0x0C, 4 bytes
    std::vector<SmartPtr<IVertexStream> >  m_VertexStreams; // +0x10, 12 bytes
    SmartPtr<IIndexStream>                 m_IndexStream;   // +0x1C, 4 bytes
    // TODO: re-verify v1.6.1 GeometryBinding_Bada address (was: 0x001a57bc stale v1.5.x) -- m_NamedIndexStreams: map<AsciiString,SmartPtr<IIndexStream>> (24B)
    char _pad_namedstreams[24];  // +0x20..+0x37: m_NamedIndexStreams placeholder
    // TODO: re-verify v1.6.1 GeometryBinding_Bada address (was: 0x001a57bc stale v1.5.x) -- m_EffectBindings: vector<EffectBinding> (12B, EffectBinding unported)
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
// in the port -- Geometry::Render draws from load-cached m_Vbo/m_Ibo/m_Layout
// (structural divergence; same fixed-function GLES1.x calls, NOT a GLES2 shader path).
// v1.6.1 VertexStreamAdd @0x002640c8, GeometryBinding ctor @0x00263e90,
//   IndexStreamSet @0x00264108, EffectGroupSet @0x0026406c.
class GeometryBinding : public GeometryBinding_Bada {
public:
    // v1.6.1 Mortar::GeometryBinding::GeometryBinding @0x00263e90
    GeometryBinding();
    virtual ~GeometryBinding();

    Event1_GeometryBinding m_OnDestroyed;  // +0x44 (8 bytes)

    // v1.6.1 Mortar::GeometryBinding::VertexStreamAdd @0x002640c8 -- find-or-push_back into m_VertexStreams.
    // v1.6.1 LoadMesh @0x0023890c calls this after EffectGroupSet.
    void VertexStreamAdd(SmartPtr<IVertexStream> stream);

    // v1.6.1 Mortar::GeometryBinding::IndexStreamSet @0x00264108 -- sets m_IndexStream; name ignored by LoadMesh (empty string).
    // v1.6.1 LoadMesh @0x0023890c calls this before VertexStreamAdd.
    void IndexStreamSet(SmartPtr<IIndexStream> stream, const AsciiString& name);

    // v1.6.1 Mortar::GeometryBinding::EffectGroupSet @0x0026406c -- stores EffectGroup ptr into m_EffectGroup.
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

// Shape-preserved port of Mortar::Geometry.
// TODO: re-verify v1.6.1 Geometry ctor (C1/C2) address (was: 0x001a3c50/0x001a3cc4 stale v1.5.x); sizeof 0x18. (dtor confirmed v1.6.1 @0x00264f40)
// Binary fields preserved at canonical offsets +0x0C..+0x14 relative to base.
// Port appends VBO/IBO/material data after the binary fields; the binary's binding-stack
// pipeline (PassBinding::Apply etc.) is structurally bypassed -- Geometry::Render draws
// from load-cached m_Vbo/m_Ibo/m_Layout (same fixed-function GLES1.x calls, NOT GLES2 shaders).
class Geometry : public ReferenceCounter {
public:
    // TODO: re-verify v1.6.1 Geometry ctor (C1/C2) address (was: 0x001a3c50/0x001a3cc4 stale v1.5.x)
    Geometry(SmartPtr<GeometryBinding> binding,
             SmartPtr<SharedEffectProperties> props);
    virtual ~Geometry();

    // v1.6.1 Geometry::Render @0x00264468 -- non-virtual member; called directly by Mesh::Draw.
    // DIFFERS: original = param-less Geometry::Render() (v1.6.1 @0x00264468) reads
    //   "World"(0x2a07af) + "SceneCamera.View"(0x2a07b5) from m_PropList -> MODELVIEW, and
    //   "SceneCamera.Projection"(0x2a0798) * DisplayManager::m_ProjMatrix -> PROJECTION (NOT a
    //   WVP read -- Render never reads a combined WorldViewProjection key; Mesh::Draw @0x00272e98
    //   writes WVP into the EffectGroup props but Render only consumes World/View/Projection),
    //   then calls GLES1 glMatrixMode/glLoadMatrixf. Using precomputed Matrix44 const& mvp passed
    //   from Mesh.cpp because the EffectProperty Matrix44 storage + BuildPropList child-list
    //   (parent-chained to Mesh's EffectGroup props) + DisplayManager::m_ProjMatrix are not yet
    //   revived (see task #374 -- too invasive to do as part of this fix, all 3D rendering at risk).
    //   Symbol will NOT pair in asm-verify (different mangled name) -- accepted DIFFERS.
    // DIFFERS: (2) port walks load-cached m_Vbo/m_Ibo/m_Layout instead of
    //   m_Binding->GetBindings()[idx].m_PassBindings (same GL calls, byte-equivalent output).
    //   Port uses m_DiffuseTex for texture binding instead of MeshMaterial param.
    void Render(Matrix44 const& mvp);

    // v1.6.1 Mortar::Geometry::HasActiveEffect @0x00264440
    bool HasActiveEffect() const;

    // v1.6.1 Mortar::Geometry::SetActiveEffect @0x00264410
    bool SetActiveEffect(uint32_t idx);

    // Accessor for m_PropList (used by Fruit::LoadFruitModels for DiffuseMap property extraction).
    EffectPropertyList* GetPropList() const { return m_PropList; }

    // v1.6.1 Mortar::Geometry::GetProperty @0x0025ee7c -- return m_PropList ? m_PropList->GetProperty(name) : NULL;
    // No hashing -- EffectPropertyList::GetProperty does a string compare against stored names
    // (see SharedEffectProperties.cpp). Call sites pass literal property names, e.g. "DiffuseMap"
    // (0x2843d1 is the rodata ADDRESS of that string literal in the binary, not a hash).
    // Port's m_PropList is null while BuildPropList is defunct, so this always returns null.
    EffectProperty* GetProperty(const char* name);

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
    // v1.6.1 Mortar::Geometry::BuildPropList @0x00264170
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
