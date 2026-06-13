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
#include <string>
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
    Vec3 min;  // +0x00
    Vec3 max;  // +0x0C

    Bounds3D() : min(), max() {}
    Bounds3D(const Vec3& mn, const Vec3& mx) : min(mn), max(mx) {}
};

// Forward declarations for defunct/stub types referenced by binary API.
class DrawEffectContainer;

// SharedPropsInfo -- value-type stored in Mesh::m_GroupsByName.
// sizeof = 0x1c (28 bytes). Binary layout confirmed by AddTextureMap RE.
struct SharedPropsInfo {
    SmartPtr<SharedEffectProperties>           m_Group;    // +0x00, 4 bytes
    std::map<AsciiString, TextureProps>        m_TexMaps; // +0x04, 24 bytes
    // total: 4 + 24 = 28 = 0x1c
};

// VertexLayout is declared in Geometry.h (included above) so it can be
// embedded by value in Geometry. Mesh.h transitively provides it.

// Material properties parsed from .mmd file
// Extracted from LoadMesh material loop (0x001a7c90)
// Original stored via EffectPropertyList/SharedEffectProperties;
// port stores directly for GLES2 shader use
struct MeshMaterial {
    std::string m_Name;             // Material name (e.g. "fruit_atlas")
    Mortar::SmartPtr<Texture> m_Texture;    // DiffuseMap texture
    Vec3 m_Diffuse;                 // GetColourRGB(color0) — set as "Ambience" property
    Vec3 m_Ambience;                // GetColourRGB(color1) — set as "Diffuse" property
    Vec3 m_SelfIllum;               // GetColourRGB(color3) — set as "SelfIllum" property
    float m_SpecularStrength;       // Specular strength float
    bool m_IsLit;                   // IsLit flag (always false in LoadMesh)

    MeshMaterial() : m_SpecularStrength(0.0f), m_IsLit(false) {}
};


// Matches original Mortar::Mesh (0x7C = 124 bytes)
// Inherits: Mortar::ReferenceCounter → IModelNode → Mesh
// Port specific: skips Effect/Geometry/SharedEffectProperties system,
// uses direct GLES2 calls via Renderer::setup_3d_shader()
// Ref: docs/engine/mesh.md — struct layout, vtable, Draw behavior
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

    std::string m_Name;                         // +0x0C equiv: Mesh name

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

    // Port-specific: material array for GLES2 rendering; no binary equivalent.
    // (Binary uses m_GroupsByName + SharedEffectProperties for per-material data.)
    // Indexed by Geometry::m_MaterialIndex.
    std::vector<MeshMaterial> m_Materials;  // +0x7c (port-specific)

    Mesh();
    virtual ~Mesh();

    // vtable[4]: Matches Mesh::Draw (0x001b0c3c)
    void Draw(const Matrix44& worldTransform) override;

    // Matches Mesh::SetBones (0x001b1340)
    void SetBones(const BoneBinding* bones, unsigned long count);

    // vtable[5]: Matches Mesh::GetBounds (0x001b07f0).
    // Binary signature: Bounds3D GetBounds() const (struct-return).
    Bounds3D GetBounds() const override;

    // vtable[8]: Matches Mesh::BindSkeleton (0x001b0948)
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
    const std::string& GetName() const override { return m_Name; }

    // Binary @ 0x001b225c -- GetGeometry returns SmartPtr<Geometry>.
    // Port uses the same storage type now. The GetGeometryEntry name is retained
    // for source-compatibility with existing call sites (the pre-Phase-5 method
    // returned the now-deleted GeometryEntry struct; signature now returns
    // Geometry* for convenient direct use).
    Geometry* GetGeometryEntry(int idx) const {
        if (idx >= 0 && idx < (int)m_Geometries.size()) return m_Geometries[idx].Get();
        return NULL;
    }

    // Port helper: assign texture to all materials that have none.
    // Used by Fruit.cpp to assign fruit_atlas when loaded externally.
    void SetDiffuseTexture(const Mortar::SmartPtr<Texture>& tex);

    // Port helper: true if any material has a valid texture.
    bool HasDiffuseTexture() const;

    // Subsystem status (post Phase 5):
    //   Geometry         -- PORTED as real class (src/engine/asset/Geometry.{h,cpp});
    //                       Render body uses the port's GLES2 path instead of binary's
    //                       fixed-pipeline GL ES 1.x.
    //                       Binary @ 0x001a3c50 ctor, 0x001a3e98 Render (non-virtual).
    //   Geometry_Bada    -- collapsed into Geometry's base; binary @ 0x001a4ba8.
    //   GeometryBinding  -- defunct stub (declaration in Geometry.h); binary @ 0x001a3990..0x001a40c0.
    //                       SmartPtr<GeometryBinding> is stored on Geometry but never dereferenced
    //                       because the port's GLES2 Render doesn't use the per-pass binding records.
    //   EffectGroup      -- PORTED (src/engine/asset/Effect.h, moved from here in commit 5bcdf2b);
    //                       AddEffect/MergeProperties have real bodies, but the live render path
    //                       never reaches them because EffectBinding/PassBinding aren't constructed.
    //   EffectBinding / PassBinding / GLFuncParams / MapBinding -- defunct, never constructed.
    //
    // Geometry::Render in the binary is hard-Bada: glMatrixMode / glPushMatrix /
    // glLoadMatrixf / glDrawArrays via fixed-pipeline GL ES 1.x. Port uses ES 2.0
    // shaders + Renderer::setup_3d_shader directly inside the ported Geometry::Render,
    // bypassing the EffectGroup -> EffectBinding[] -> PassBinding[] multi-pass
    // machinery. Mesh::Draw iterates m_Geometries[] and calls Geometry::Render per
    // submesh.
    //
    // Defunct: SharedEffectProperties machinery -- port stores parsed values
    // directly in MeshMaterial; field shape (m_OwnGroup, m_GroupsByName, m_WorldProp,
    // m_ViewProp, m_ProjProp, m_WVPProp) is preserved at binary offsets so
    // sizeof(Mesh) == 0x7c + sizeof(m_Materials port extension). Binary @:
    //   0x001b0988 -- GetPropertiesGroup(name) const               [shape-preserved]
    //   0x001b1430 -- GetPropertiesGroup(name, defs_begin, defs_end) [shape-preserved]
    //   0x001aab94 -- GetPropertiesGroup<9>(name, defs[9])
    //   0x001b1394 -- SharedPropsInfo::AddTextureMap(name, propName)
    //   0x001b0d0c -- AddGeometry(Mortar::SmartPtr<Geometry>&)     [shape-preserved]
    //   0x001b15e4 -- GenerateBindings(name, slot, vector<Bone::Binding>&) [empty BX LR]
    //   0x001b08e8 -- RebuildEffectBindings()                      [shape-preserved]
    //   0x001b10d8 -- Mesh(Mortar::SmartPtr<SharedEffectProperties>&, AsciiString&) [shape-preserved]
    //   0x00193ed8 -- DrawCube(...)    [binary stub, returns colour unchanged]
    //   0x00193edc -- DrawLine(...)    [binary stub, returns first vec unchanged]
    //   0x00193ee0 -- DrawSphere(...)  [binary stub, returns colour unchanged]

    // ---- STUBS (binary) ----

    // Defunct: Mesh(SmartPtr<SharedEffectProperties> const&, AsciiString const&) -- binary @ 0x001b10d8
    // Shape-preserved: builds m_OwnGroup from defs, caches m_WorldProp/m_ViewProp/m_ProjProp/m_WVPProp.
    Mesh(SmartPtr<SharedEffectProperties> const& props, AsciiString const& name);

    // Binary @ 0x001b0948 — const-ref BindSkeleton overload (distinct mangled symbol);
    // resolves m_SkeletonIndex per BoneBinding identically to the ptr overload.
    void BindSkeleton(Skeleton const& skeleton);

    // Binary @ 0x001b0d0c -- pushes SmartPtr<Geometry> into m_Geometries.
    void AddGeometry(SmartPtr<Geometry> const& geom);

    // Defunct: GetPropertiesGroup(AsciiString const&) const -- binary @ 0x001b0988
    // Shape-preserved: returns ptr-to-SmartPtr in m_GroupsByName (matching binary return type).
    SmartPtr<SharedEffectProperties>* GetPropertiesGroup(AsciiString const& name) const;

    // Defunct: GetPropertiesGroup(AsciiString const&, EffectPropertyDefinition const*, ...) -- binary @ 0x001b1430
    // Shape-preserved: range-based variant; may insert new group if defs not already present.
    SmartPtr<SharedEffectProperties>* GetPropertiesGroup(AsciiString const& name,
                                                         EffectPropertyDefinition const* begin,
                                                         EffectPropertyDefinition const* end);

    // Defunct: SharedEffectProperties subsystem -- no-op stub; binary @ 0x001b08e8
    // Port computes MVP via MatrixManager directly.
    void RebuildEffectBindings();

    // Defunct: debug draw primitive -- no-op stub; binary @ 0x00193ed8
    // Binary itself is a stub (BX LR, returns colour unchanged); port is likewise a no-op.
    void DrawCube(float x, float y, float z, Colour colour, DrawEffectContainer* fx);

    // Defunct: debug draw primitive -- no-op stub; binary @ 0x00193edc
    // Binary itself is a stub (BX LR, returns first vec unchanged); port is likewise a no-op.
    void DrawLine(Vec3 const& from, Vec3 const& to, float const& width,
                  Colour const& colour, Vec3 const& normal,
                  DrawEffectContainer* fx);

    // Defunct: debug draw primitive -- no-op stub; binary @ 0x00193ee0
    // Binary itself is a stub (BX LR, returns colour unchanged); port is likewise a no-op.
    void DrawSphere(float radius, Colour colour, DrawEffectContainer* fx);

    // Binary @ 0x001b09b0
    void DrawQuad(Colour colour, SmartPtr<Texture> texture,
                  Vec3 const& pos, Vec3 const& scale, float rotZ,
                  float w, float h, float uOff, float vOff,
                  DrawEffectContainer* fx);

    // Binary @ 0x00194180 — delegates to 6-arg with u0=0,v0=1,u1=0,v1=1 (full texture).
    static void DrawQuadUnCached(Colour colour, DrawEffectContainer* fx);

    // Binary @ 0x00194060 — 4-vert TRIANGLE_STRIP unit quad with UV crop [u0,v0]..[u1,v1].
    static void DrawQuadUnCached(Colour colour, float u0, float v0, float u1, float v1,
                                 DrawEffectContainer* fx);

    // Binary @ 0x0019404c — forwards to DrawTris with primType=GL_TRIANGLES; outer blend ignored.
    static void DrawTriList(QUADCUSTOMVERTEX const* verts, long count, bool blend,
                            DrawEffectContainer* fx);

    // Binary @ 0x00194038 — forwards to DrawTris with primType=GL_TRIANGLE_STRIP; outer blend ignored.
    static void DrawTriStrip(QUADCUSTOMVERTEX const* verts, long count, bool blend,
                             DrawEffectContainer* fx);

    // Binary @ 0x00193f5c — dispatches to Renderer::DrawTriList or DrawTriStrip by primType.
    static void DrawTris(QUADCUSTOMVERTEX const* verts, long count, int primType, bool blend,
                         DrawEffectContainer* fx);

    // vtable slot 6 (+0x18): GenerateBindings(Vector) @ 0x0027350c
    // Walks m_GroupsByName rb-tree matching channelName/targetName vs uvwChannelName/opacityChannelName
    // statics. If the matched EffectProperty* Vec3 target is non-null, builds a Binding and push_backs
    // into out. With EffectProperty subsystem defunct-stubbed in the port, produces zero bindings.
    // Defunct: EffectProperty channel binding -- binary @ 0x0027350c
    void GenerateBindings(AsciiString const& channelName,
                          AsciiString const& targetName,
                          std::vector<AnimBindings::Vector::Binding>& out) override;

    // vtable slot 7 (+0x1c): GenerateBindings(Bone) @ 0x0027385c (empty BX LR in binary)
    // Bone bindings are never produced by Mesh; binary body is BX LR.
    // Defunct-ish: Mesh emits no bone bindings; binary @ 0x0027385c (empty BX LR)
    void GenerateBindings(AsciiString const& channelName,
                          AsciiString const& targetName,
                          std::vector<AnimBindings::Bone::Binding>& out) override;

    // ---- end STUBS ----
};

// Namespace-level alias so callers that use Mortar::BoneBinding directly still compile.
// The binary's canonical name is Mortar::Mesh::BoneBinding.
typedef Mesh::BoneBinding BoneBinding;

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
#include <cstddef>
static_assert(sizeof(Mortar::Mesh::BoneBinding)                   == 0x44, "Mortar::Mesh::BoneBinding size");
static_assert(offsetof(Mortar::Mesh::BoneBinding, m_BoneName)    == 0x00, "BoneBinding::m_BoneName offset");
static_assert(offsetof(Mortar::Mesh::BoneBinding, m_Bounds)      == 0x28, "BoneBinding::m_Bounds offset");
static_assert(offsetof(Mortar::Mesh::BoneBinding, m_SkeletonIndex)== 0x40, "BoneBinding::m_SkeletonIndex offset");
#endif

// Model is declared in Model.h.
class Model;

} // namespace Mortar

#endif
