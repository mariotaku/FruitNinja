#ifndef MORTAR_ASSET_MESH_H
#define MORTAR_ASSET_MESH_H

// Analysed: 2026-04-11T12:00

#include "util/SmartPtr.h"
#include "util/AsciiString.h"
#include "asset/IModelNode.h"
#include "asset/Texture.h"
#include "asset/SharedEffectProperties.h"
#include "asset/Geometry.h"
#include "math/Colour.h"
#include "render/gl_funcs.h"
#include <vector>
#include <map>
#include <cstdint>
#include "math/Colour.h"
#include "util/ReferenceCounter.h"
#include "asset/Effect.h"

struct QUADCUSTOMVERTEX;

namespace Mortar {

// Bounds3D — POD axis-aligned bounding box. 24 bytes.
// Binary copy-ctor @ 0x001b1630 just copies two Vec3s. Default ctor is
// implicit (callers like Mesh::GetBounds @ 0x001b07f0 inline-construct
// via _Vector3::_Vector3(this, +/-1e30, +/-1e30, +/-1e30)). No vtable,
// no refcount, no other methods — `Draw(Matrix44 const&, Colour) const`
// from earlier auto-stub work was fabricated and doesn't exist in the
// binary.
struct Bounds3D {
    _Vector3<float> min;  // +0x00
    _Vector3<float> max;  // +0x0C

    Bounds3D() : min(), max() {}
    Bounds3D(const _Vector3<float>& mn, const _Vector3<float>& mx) : min(mn), max(mx) {}
};

// Forward declarations for defunct/stub types referenced by binary API.
class DrawEffectContainer;
struct TextureAtlasPage; // struct (matches FontInterface.h definition; MSVC mangles the tag)

// SharedPropsInfo -- value-type stored in Mesh::m_GroupsByName.
// sizeof = 0x1c (28 bytes). Binary: ctor @0x002742c8, AddTextureMap @0x001b1394.
// v1.6.1 LoadMesh @0x0023890c -- GetPropertiesGroup inserts entries; AddTextureMap
// is called per material when mat.m_TextureName is non-empty.
struct SharedPropsInfo {
    SmartPtr<SharedEffectProperties>           m_Props;      // +0x00, 4 bytes
    std::map<AsciiString, TextureProps>        m_TextureMaps; // +0x04, 24 bytes
    // total: 4 + 24 = 28 = 0x1c

    // Binary @ 0x001b1394 -- inserts a TextureProps into m_TextureMaps.
    // Called when material texture-name is non-empty; TextureProps{} default-inserts.
    // The propName ("DiffuseMap") is unused in the current port stub (TextureProps
    // only carries an EffectProperty* handle looked up by GetProperty(propName), which
    // returns nullptr when the subsystem is defunct).
    void AddTextureMap(const AsciiString& name, const AsciiString& propName);
};

// VertexLayout is declared in Geometry.h (included above) so it can be
// embedded by value in Geometry. Mesh.h transitively provides it.

// MeshMaterial has been removed. Per-material diffuse textures are stored
// directly on Geometry::m_DiffuseTex (port field). Material colour/lit
// properties were unused (IsLit==false for all meshes).


// Matches original Mortar::Mesh (0x7C = 124 bytes)
// Inherits: Mortar::ReferenceCounter → IModelNode → Mesh
// Binary sizeof(Mesh) = 0x7C.
// Port specific: Geometry::Render draws from load-cached m_Vbo/m_Ibo/m_Layout rather than
// walking the PassBinding::Apply chain (structural divergence; same fixed-function GLES1.x calls).
// Per-geometry diffuse texture is stored on Geometry::m_DiffuseTex.
class Mesh : public IModelNode {
public:
    // Matches original Mortar::Mesh::BoneBinding (0x44 = 68 bytes)
    // Nested inside Mesh to match binary mangling (Mortar::Mesh::BoneBinding).
    //   +0x00  AsciiString m_BoneName  (40 bytes, binary field m_BoneName)
    //   +0x28  Bounds3D    m_Bounds    (24 bytes = 2 Vec3: min @ +0x28, max @ +0x34)
    //   +0x40  uint32_t    m_BoneIndex (bone index into Skeleton)
    //   +0x44  end
    struct BoneBinding {
        AsciiString m_BoneName;     // +0x00: Bone name (40 bytes)
        Bounds3D    m_Bounds;       // +0x28: Local AABB (24 bytes)
        int         m_SkeletonIndex; // +0x40: Index into Skeleton; -1 = unbound

        BoneBinding() : m_SkeletonIndex(-1) {}
    };

    // --- Original fields (matching offsets where applicable) ---

    AsciiString m_Name;                          // +0x0C: Mesh name (40 bytes)

    std::vector<BoneBinding> m_BoneBindings;    // +0x34: Bone binding array (12 bytes)

    // +0x40: Geometry submeshes (matches binary vector<Mortar::SmartPtr<Geometry>>)
    std::vector<SmartPtr<Geometry> > m_Geometries;   // +0x40 (12 bytes)

    // +0x4c: Defunct -- SharedEffectProperties subsystem. Shape preserved to
    // keep m_Skeleton at binary offset +0x68 and sizeof(Mesh) at 0x7c.
    SmartPtr<SharedEffectProperties> m_OwnGroup;               // +0x4c (4 bytes)
    std::map<AsciiString, SharedPropsInfo> m_GroupsByName;     // +0x50 (24 bytes)

    // +0x68: Bound skeleton pointer (nullptr if none). Set by BindSkeleton.
    // Matches Mesh::m_Skeleton binary offset 0x68.
    Skeleton* m_Skeleton;                                      // +0x68 (4 bytes)

    // +0x6c..+0x78: Cached EffectProperty* handles (World/View/Proj/WVP).
    // Set by Mesh ctor via GetProperty; used by TrySetMatrix_EffectProp in Draw.
    // Phase 2 will wire the real arena; Phase 1 GetProperty() stub returns NULL.
    EffectProperty* m_WorldProp;  // +0x6c
    EffectProperty* m_ViewProp;   // +0x70
    EffectProperty* m_ProjProp;   // +0x74
    EffectProperty* m_WVPProp;    // +0x78

    // (Binary stores material data via SharedEffectProperties system.
    // Port stores per-geometry diffuse texture on Geometry::m_DiffuseTex.)

    Mesh();
    virtual ~Mesh();

    // vtable[4]: Matches Mesh::Draw (0x00272e98)
    void Draw(const Matrix44& worldTransform) override;

    // Matches Mesh::SetBones (0x001b1340)
    void SetBones(const BoneBinding* bones, unsigned long count);

    // vtable[5]: Matches Mesh::GetBounds (0x00272b48).
    // Binary signature: Bounds3D GetBounds() const (struct-return).
    Bounds3D GetBounds() const override;

    // vtable[8]: Matches Mesh::BindSkeleton (binary @ 0x001b0948)
    // Stores skeleton ptr; resolves m_SkeletonIndex per BoneBinding via FindIndex.
    void BindSkeleton(Skeleton* skeleton) override;

    // Binary @ 0x001b0778 — symmetric to GetBoneVertTransform; reads Skeleton::GetLocal(idx).
    Matrix44 GetBoneLocalTransform(unsigned long idx) const;

    // Matches Mesh::GetBoneVertTransform (0x001b0688)
    // Returns pointer to vert matrix for binding[index], or nullptr if no skeleton bound.
    const Matrix44* GetBoneVertTransform(unsigned long index) const;

    // Matches Mesh::GetBoneWorldTransform (0x001b0700)
    // Returns world matrix for binding[index] through skeleton. Identity fallback when
    // no skeleton bound or bone unbound.
    Matrix44 GetBoneWorldTransform(unsigned long index) const;

    // vtable[9]: Matches Mesh::GetGeometryCount (0x001b1678)
    int GetGeometryCount() const override { return (int)m_Geometries.size(); }

    // vtable[3]: Matches Mesh::GetName (0x001b15e0)
    const AsciiString& GetName() const override { return m_Name; }

    // Binary @ 0x001b225c -- GetGeometry returns SmartPtr<Geometry>.
    // Port uses the same storage type now. The GetGeometryEntry name is retained
    // for source-compatibility with existing call sites (the pre-Phase-5 method
    // returned the now-deleted GeometryEntry struct; signature now returns
    // Geometry* for convenient direct use).
    Geometry* GetGeometryEntry(int idx) const {
        if (idx >= 0 && idx < (int)m_Geometries.size()) return m_Geometries[idx].Get();
        return NULL;
    }

    // SetDiffuseTexture and HasDiffuseTexture have been removed.
    // Per-geometry m_DiffuseTex is assigned directly in MeshManager
    // and Fruit::LoadFruitModels.

    // Subsystem status (post Phase 5):
    //   Geometry         -- PORTED as real class (src/engine/asset/Geometry.{h,cpp}).
    //                       Render body is fixed-function GLES1.x (same as binary) but draws
    //                       from load-cached m_Vbo/m_Ibo/m_Layout rather than walking
    //                       PassBinding::Apply (structural divergence; byte-equivalent GL output).
    //                       v1.6.1 Geometry::Render @0x00264468.
    //                       v1.6.1 Mortar::Geometry::Geometry ctor @0x002641c4.
    //   Geometry_Bada    -- collapsed into Geometry's base; TODO: re-verify v1.6.1 Geometry_Bada address (no separate symbol -- collapsed).
    //   GeometryBinding  -- constructed and wired faithfully in LoadMesh; m_Binding stored on Geometry
    //                       but PassBinding::Apply is not called at draw (structural bypass -- same GL result).
    //                       v1.6.1 Mortar::GeometryBinding::GeometryBinding ctor @0x00263e90.
    //   EffectGroup      -- PORTED (src/engine/asset/Effect.h, moved from here in commit 5bcdf2b);
    //                       AddEffect/MergeProperties have real bodies, but the live render path
    //                       never reaches them because EffectBinding/PassBinding aren't constructed.
    //   EffectBinding / PassBinding / GLFuncParams / MapBinding -- defunct, never constructed.
    //
    // Geometry::Render (binary v1.6.1 @0x00264468): walks m_Binding->GetBindings()[idx].m_PassBindings,
    // calls GeometryBinding_GLES1::PassBinding::Apply (v1.6.1 @0x00263b7c, thunk @0x00111e24) for
    // glVertexPointer/glNormalPointer/glTexCoordPointer etc., then issues glDrawElements via
    // the IIndexStream vtable -- fixed-function GL ES 1.x throughout.
    // Port mirrors all the same fixed-function calls but drives them from load-cached fields
    // rather than re-deriving from the PassBinding records. Mesh::Draw iterates m_Geometries[]
    // and calls Geometry::Render per submesh.
    //
    // Defunct: SharedEffectProperties machinery -- port stores per-geometry
    // diffuse texture on Geometry::m_DiffuseTex; field shape (m_OwnGroup,
    // m_GroupsByName, m_WorldProp, m_ViewProp, m_ProjProp, m_WVPProp) is
    // preserved at binary offsets so sizeof(Mesh) == 0x7c. Binary @:
    //   0x00272c98 -- GetPropertiesGroup(name) const               [shape-preserved]
    //   0x001b1430 -- GetPropertiesGroup(name, defs_begin, defs_end) [shape-preserved]
    //   0x001aab94 -- GetPropertiesGroup<9>(name, defs[9])
    //   0x001b1394 -- SharedPropsInfo::AddTextureMap(name, propName)
    //   0x001b0d0c -- AddGeometry(Mortar::SmartPtr<Geometry>&)     [shape-preserved]
    //   0x0027385c -- GenerateBindings(name, slot, vector<Bone::Binding>&) [empty BX LR]
    //   0x001b08e8 -- RebuildEffectBindings()                      [shape-preserved]
    //   0x002730ac -- Mesh(Mortar::SmartPtr<SharedEffectProperties>&, AsciiString&) [shape-preserved]
    //   0x00193ed8 -- DrawCube(...)    [binary stub, returns colour unchanged]
    //   0x00193edc -- DrawLine(...)    [binary stub, returns first vec unchanged]
    //   0x00193ee0 -- DrawSphere(...)  [binary stub, returns colour unchanged]

    // ---- STUBS (binary) ----

    // Defunct: Mesh(SmartPtr<SharedEffectProperties>, AsciiString const&) -- v1.6.1 binary @ 0x002730ac
    // Shape-preserved: builds m_OwnGroup from defs, caches m_WorldProp/m_ViewProp/m_ProjProp/m_WVPProp.
    Mesh(SmartPtr<SharedEffectProperties> props, AsciiString const& name);

    // Binary @ 0x001b0948 — const-ref BindSkeleton overload (distinct mangled symbol);
    // resolves m_SkeletonIndex per BoneBinding identically to the ptr overload.
    void BindSkeleton(Skeleton const& skeleton);

    // Binary @ 0x001b0d0c -- pushes SmartPtr<Geometry> into m_Geometries.
    void AddGeometry(SmartPtr<Geometry> geom);

    // Defunct: GetPropertiesGroup(AsciiString const&) const -- v1.6.1 binary @ 0x00272c98
    // Shape-preserved: returns ptr-to-SmartPtr in m_GroupsByName (matching binary return type).
    SmartPtr<SharedEffectProperties>* GetPropertiesGroup(AsciiString const& name) const;

    // Defunct: GetPropertiesGroup(AsciiString const&, EffectPropertyDefinition const*, ...) -- v1.6.1 binary @ 0x001b1430
    // Shape-preserved: range-based variant; may insert new group if defs not already present.
    SmartPtr<SharedEffectProperties>* GetPropertiesGroup(AsciiString const& name,
                                                         EffectPropertyDefinition const* begin,
                                                         EffectPropertyDefinition const* end);

    // Defunct: SharedEffectProperties subsystem -- no-op stub; v1.6.1 binary @ 0x001b08e8
    // Port computes MVP via MatrixManager directly.
    void RebuildEffectBindings();

    // Defunct: debug draw primitive -- no-op stub; v1.6.1 binary @ 0x00193ed8
    // Binary itself is a stub (BX LR, returns colour unchanged); port is likewise a no-op.
    void DrawCube(float x, float y, float z, Colour colour, DrawEffectContainer* fx);

    // Defunct: debug draw primitive -- no-op stub; v1.6.1 binary @ 0x00193edc
    // Binary itself is a stub (BX LR, returns first vec unchanged); port is likewise a no-op.
    void DrawLine(_Vector3<float> const& from, _Vector3<float> const& to, float const& width,
                  Colour const& colour, _Vector3<float> const& normal,
                  DrawEffectContainer* fx);

    // Defunct: debug draw primitive -- no-op stub; v1.6.1 binary @ 0x00193ee0
    // Binary itself is a stub (BX LR, returns colour unchanged); port is likewise a no-op.
    void DrawSphere(float radius, Colour colour, DrawEffectContainer* fx);

    // Binary @ 0x00272a3c
    void DrawQuad(Colour colour, SmartPtr<Texture> texture,
                  _Vector3<float> const& pos, _Vector3<float> const& scale, float rotZ,
                  float w, float h, float uOff, float vOff,
                  DrawEffectContainer* fx);

    // Binary @ 0x00240be4 — delegates to 6-arg with uMin=0,uMax=1,vMin=0,vMax=1 (full texture).
    static void DrawQuadUnCached(Colour colour, DrawEffectContainer* fx);

    // Binary @ 0x00240a70 — 4-vert TRIANGLE_STRIP unit quad with UV crop (uMin,uMax,vMin,vMax).
    // ASM-spec v1.6.1 Mesh::DrawQuadUnCached @0x00240a70: U-pair first, then V-pair.
    static void DrawQuadUnCached(Colour colour, float uMin, float uMax, float vMin, float vMax,
                                 DrawEffectContainer* fx);

    // Binary @ 0x00240e34 — forwards to DrawTris with primType=4 (GL_TRIANGLES); outer blend ignored.
    static void DrawTriList(QUADCUSTOMVERTEX const* verts, long count, bool blend = false,
                            DrawEffectContainer* fx = 0, TextureAtlasPage* atlas = 0);

    // Binary @ 0x00240e10 — forwards to DrawTris with primType=5 (GL_TRIANGLE_STRIP); outer blend ignored.
    static void DrawTriStrip(QUADCUSTOMVERTEX const* verts, long count, bool blend = false,
                             DrawEffectContainer* fx = 0, TextureAtlasPage* atlas = 0);

    // Binary @ 0x00240c30 — dispatches to Renderer::DrawTriList or DrawTriStrip by primType.
    // 6th param (atlas) is only touched when fx != NULL (bind pre-draw / unbind post-draw via
    // page->GetTexture()->vtable[0xc]/[0x10]); every port call site passes fx==NULL so that
    // path is unreached — kept for shape fidelity, not exercised.
    static void DrawTris(QUADCUSTOMVERTEX const* verts, long count, int primType,
                         bool blend = false, DrawEffectContainer* fx = 0,
                         TextureAtlasPage* atlas = 0);

    // vtable slot 6 (+0x18): GenerateBindings(Vector) binary @ 0x0027350c
    // Walks m_GroupsByName rb-tree matching channelName/targetName vs uvwChannelName/opacityChannelName
    // statics (lazily interned via __cxa_guard from DAT_002736c8/d8). If the matched EffectProperty*
    // Vec3 target is non-null, builds a Binding and push_backs into out. With EffectProperty subsystem
    // defunct-stubbed in the port, produces zero bindings (correct observable result).
    // Defunct: EffectProperty channel binding -- v1.6.1 binary @ 0x0027350c
    void GenerateBindings(AsciiString const& channelName,
                          AsciiString const& targetName,
                          std::vector<AnimBindings::Vector::Binding>& out) override;

    // vtable slot 7 (+0x1c): GenerateBindings(Bone) v1.6.1 Mortar::Mesh::GenerateBindings @0x0027385c
    // Binary body is a single BX LR (genuinely empty). Port empty body is exact match.
    // Defunct-ish: Mesh emits no bone bindings; v1.6.1 Mortar::Mesh::GenerateBindings @0x0027385c (empty BX LR)
    void GenerateBindings(AsciiString const& channelName,
                          AsciiString const& targetName,
                          std::vector<AnimBindings::Bone::Binding>& out) override;

    // ---- end STUBS ----
};

// Namespace-level alias so callers that use Mortar::BoneBinding directly still compile.
// The binary's canonical name is Mortar::Mesh::BoneBinding.
typedef Mesh::BoneBinding BoneBinding;

#if defined(__bada__)
#include <cstddef>
// Mesh (v1.6.1 Mortar::Mesh @0x0023890c LoadMesh / sizeof from operator new caller):
// m_Name is AsciiString (40B) @ +0x0C; m_BoneBindings vector @ +0x34; sizeof == 0x7C.
static_assert(sizeof(Mortar::Mesh)                                == 0x7C, "sizeof(Mesh) must be 0x7C");
static_assert(__builtin_offsetof(Mortar::Mesh, m_Name)           == 0x0C, "Mesh::m_Name offset (+0x0C)");
static_assert(__builtin_offsetof(Mortar::Mesh, m_BoneBindings)   == 0x34, "Mesh::m_BoneBindings offset (+0x34)");
static_assert(sizeof(Mortar::Mesh::BoneBinding)                   == 0x44, "Mortar::Mesh::BoneBinding size");
static_assert(offsetof(Mortar::Mesh::BoneBinding, m_BoneName)    == 0x00, "BoneBinding::m_BoneName offset");
static_assert(offsetof(Mortar::Mesh::BoneBinding, m_Bounds)      == 0x28, "BoneBinding::m_Bounds offset");
static_assert(offsetof(Mortar::Mesh::BoneBinding, m_SkeletonIndex)== 0x40, "BoneBinding::m_SkeletonIndex offset");
static_assert(sizeof(Mortar::SharedPropsInfo)                     == 0x1c, "SharedPropsInfo size (0x1c = 28 bytes)");
static_assert(offsetof(Mortar::SharedPropsInfo, m_Props)          == 0x00, "SharedPropsInfo::m_Props offset");
static_assert(offsetof(Mortar::SharedPropsInfo, m_TextureMaps)    == 0x04, "SharedPropsInfo::m_TextureMaps offset");
#endif

// Model is declared in Model.h.
class Model;

} // namespace Mortar

#endif
