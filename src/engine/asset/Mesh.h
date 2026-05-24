#ifndef MORTAR_ASSET_MESH_H
#define MORTAR_ASSET_MESH_H

// Analysed: 2026-04-11T12:00

#include "util/SmartPtr.h"
#include "util/AsciiString.h"
#include "asset/IModelNode.h"
#include "asset/Texture.h"
#include "math/Colour.h"
#include "render/gl_funcs.h"
#include <vector>
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

// EffectGroup — 0x38 bytes.
// Binary ctor @ 0x001a2c10 (templated_ctor<Iter>), D2 @ 0x001a1a70.
// Layout:
//   +0x00  ReferenceCounter base (12 bytes)
//   +0x0C  std::vector<EffectPropertyDefinition_Bada>  m_MergedDefs
//   +0x18  std::vector<SmartPtr<Effect> >              m_Effects
//   +0x24..+0x37  4-byte align pad + 2x Event1<EffectGroup&>
//                 (not modelled; uint8_t pad keeps binary-shape sizeof
//                 if asm-verify ever cares).
//
// AddEffect (binary @ 0x001a0274): sorted-vector-set insert into m_Effects
// via std::lower_bound + EffectLessThanCompare; on duplicate name, calls
// `this->MergeProperties(effect->Properties())` which folds the new
// effect's property defs into m_MergedDefs.
//
// Live render path is fully bypassed in the port (Renderer uses GLES2
// shaders directly, not Effect/EffectGroup multi-pass dispatch). Both
// AddEffect and MergeProperties are reachable in code shape but never
// fire at runtime — every binary caller chains through stubs.
class EffectGroup : public ReferenceCounter {
public:
    std::vector<EffectPropertyDefinition_Bada>  m_MergedDefs;   // +0x0C
    std::vector<SmartPtr<Effect> >              m_Effects;      // +0x18
    // +0x24..+0x37: 4-byte align pad + two Event1<EffectGroup&> slots.
    // Not modelled; gap preserved for binary-shape sizeof.
    uint8_t _events_gap[20];

    EffectGroup() {}

    // Binary @ 0x001a0274 — sorted-set insert by name; merge on dup.
    void AddEffect(const SmartPtr<Effect>& effect);

    // Binary @ 0x001a2030 — fold each property def in `props` into
    // m_MergedDefs at its lower_bound position. Returns 1 on success,
    // 0 if any def conflicts with an existing same-named entry.
    int MergeProperties(const std::vector<EffectPropertyDefinition_Bada>& props);
};

// Forward declarations for defunct/stub types referenced by binary API.
class DrawEffectContainer;
class EffectPropertyDefinition;
class SharedEffectProperties;
class Geometry;

// Vertex attribute layout (from PSP vertex declaration)
// Port specific: replaces the original Effect/Geometry/GeometryBinding system
struct VertexLayout {
    int posOffset;    int posSize;     // 3 floats typically
    int normalOffset; int normalSize;  // 3 floats or 3 shorts
    int colorOffset;  int colorSize;   // 0, 2, or 4 bytes
    int colorFmt;                      // 0=none, 1=BGR5650, 2=ABGR5551, 3=RGBA8888
    int texOffset;    int texSize;     // 2 floats
    int totalStride;
};

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

// One GPU-resident geometry sub-mesh (VBO + IBO pair with material index)
// Replaces the original Geometry/GeometryBinding system per entry in m_Geometries
// Ref: LoadMesh geometry loop (0x001a7c90 lines 320+)
struct GeometryEntry {
    GLuint vbo;
    GLuint ibo;
    int vertCount;
    int indexCount;
    GLenum primType;
    VertexLayout layout;
    int materialIndex;  // index into Mesh::m_Materials

    GeometryEntry()
        : vbo(0), ibo(0), vertCount(0), indexCount(0)
        , primType(GL_TRIANGLES), materialIndex(0)
    {
        memset(&layout, 0, sizeof(layout));
    }
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
    // Ref: docs/engine/mesh.md
    struct BoneBinding {
        std::string m_Name;         // +0x00: Bone name (port: std::string instead of AsciiString)
        Vec3 m_BoundsMin;           // +0x28 equiv: Local AABB minimum
        Vec3 m_BoundsMax;           // +0x34 equiv: Local AABB maximum
        int m_SkeletonIndex;        // +0x40: Index into Skeleton; -1 = unbound

        BoneBinding() : m_SkeletonIndex(-1) {}
    };

    // --- Original fields (matching offsets where applicable) ---

    std::string m_Name;                         // +0x0C equiv: Mesh name

    std::vector<BoneBinding> m_BoneBindings;    // +0x34 equiv: Bone binding array

    // +0x40 equiv: Geometry submeshes (original: vector<Mortar::SmartPtr<Geometry>>)
    // Port: each entry has its own VBO/IBO pair + material index
    std::vector<GeometryEntry> m_Geometries;

    // Material array (original: map<AsciiString, SharedPropsInfo> + SharedEffectProperties)
    // Port: parallel array indexed by GeometryEntry::materialIndex
    std::vector<MeshMaterial> m_Materials;

    // +0x68: Bound skeleton pointer (nullptr if none). Set by BindSkeleton.
    // Matches Mesh::m_Skeleton (0x001b0c3c offset 0x68)
    Skeleton* m_Skeleton;

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

    // DIFFERS: binary GetGeometry @ 0x001b225c returns Mortar::SmartPtr<Geometry>;
    // port returns const GeometryEntry* because GeometryEntry has trivial value
    // semantics (no Geometry refcounted object).
    // Port specific: access GeometryEntry by index (replaces vtable[10] GetGeometry).
    const GeometryEntry* GetGeometryEntry(int idx) const {
        if (idx >= 0 && idx < (int)m_Geometries.size()) return &m_Geometries[idx];
        return nullptr;
    }

    // Port helper: assign texture to all materials that have none.
    // Used by Fruit.cpp to assign fruit_atlas when loaded externally.
    void SetDiffuseTexture(const Mortar::SmartPtr<Texture>& tex);

    // Port helper: true if any material has a valid texture.
    bool HasDiffuseTexture() const;

    // Defunct: Mortar::Geometry / Geometry_Bada / GeometryBinding /
    //          GeometryBinding_Bada / EffectGroup / EffectBinding / PassBinding
    //          stack -- replaced by flat GeometryEntry { vbo, ibo, primType,
    //          layout, materialIndex } + MeshMaterial in the port.
    //   Geometry         binary @ 0x001a3c50 ctor, 0x001a3e98 Render (non-virtual)
    //   Geometry_Bada    binary @ 0x001a4ba8 ctor, 0x001a40bc D1
    //   GeometryBinding* binary @ 0x001a3990..0x001a40c0
    //
    // Geometry::Render() is hard-Bada: glMatrixMode / glPushMatrix /
    // glLoadMatrixf / glDrawArrays via fixed-pipeline GL ES 1.x. Port uses
    // ES 2.0 shaders + Renderer::setup_3d_shader, so the entire effect-binding
    // multi-pass machinery (EffectGroup -> EffectBinding[] -> PassBinding[])
    // is bypassed. Port walks m_Geometries[] directly in Mesh::Draw.
    //
    // Defunct: SharedEffectProperties machinery -- port stores parsed values
    // directly in MeshMaterial (no per-property name lookup needed); binary @:
    //   0x001b0988 -- GetPropertiesGroup(name) const
    //   0x001b1430 -- GetPropertiesGroup(name, defs_begin, defs_end)
    //   0x001aab94 -- GetPropertiesGroup<9>(name, defs[9])
    //   0x001b1394 -- SharedPropsInfo::AddTextureMap(name, propName)
    //   0x001b0d0c -- AddGeometry(Mortar::SmartPtr<Geometry>&)  (port appends in LoadMesh directly)
    //   0x001b15e4 -- GenerateBindings(name, slot, vector<Bone::Binding>&) [empty BX LR]
    //   0x001b08e8 -- RebuildEffectBindings()  [port computes MVP via MatrixManager directly]
    //   0x001b10d8 -- Mesh(Mortar::SmartPtr<SharedEffectProperties>&, AsciiString&)  [2-arg ctor; port has default ctor only]
    //   0x00193ed8 -- DrawCube(...)    [binary stub, returns colour unchanged]
    //   0x00193edc -- DrawLine(...)    [binary stub, returns first vec unchanged]
    //   0x00193ee0 -- DrawSphere(...)  [binary stub, returns colour unchanged]

    // ---- STUBS (binary) ----

    // STUB: Mesh(SmartPtr<SharedEffectProperties> const&, AsciiString const&) -- binary @ 0x001b10d8 (TODO RE)
    Mesh(SmartPtr<SharedEffectProperties> const& props, AsciiString const& name);

    // STUB: BindSkeleton(Skeleton const&) -- binary @ 0x001b0948 (TODO RE)
    // Binary signature takes const-ref; port's existing BindSkeleton(Skeleton*) has mismatched mangling.
    void BindSkeleton(Skeleton const& skeleton);

    // STUB: AddGeometry(SmartPtr<Geometry> const&) -- binary @ 0x001b0d0c (TODO RE)
    // Defunct: port appends GeometryEntry directly in LoadMesh.
    void AddGeometry(SmartPtr<Geometry> const& geom);

    // STUB: GetPropertiesGroup(AsciiString const&) const -- binary @ 0x001b0988 (TODO RE)
    // Defunct: port stores material props directly in MeshMaterial.
    SharedEffectProperties* GetPropertiesGroup(AsciiString const& name) const;

    // STUB: GetPropertiesGroup(AsciiString const&, EffectPropertyDefinition const*, EffectPropertyDefinition const*) -- binary @ 0x001b1430 (TODO RE)
    // Defunct: range-based variant; port stores material props directly.
    SharedEffectProperties* GetPropertiesGroup(AsciiString const& name,
                                               EffectPropertyDefinition const* begin,
                                               EffectPropertyDefinition const* end);

    // STUB: RebuildEffectBindings() -- binary @ 0x001b08e8 (TODO RE)
    // Defunct: port computes MVP via MatrixManager directly.
    void RebuildEffectBindings();

    // STUB: DrawCube(float, float, float, Colour, DrawEffectContainer*) -- binary @ 0x00193ed8 (TODO RE)
    // Binary is a stub (returns colour unchanged); port is likewise a no-op.
    void DrawCube(float x, float y, float z, Colour colour, DrawEffectContainer* fx);

    // STUB: DrawLine(Vec3 const&, Vec3 const&, float const&, Colour const&, Vec3 const&, DrawEffectContainer*) -- binary @ 0x00193edc (TODO RE)
    // Binary is a stub (returns first vec unchanged); port is likewise a no-op.
    void DrawLine(Vec3 const& from, Vec3 const& to, float const& width,
                  Colour const& colour, Vec3 const& normal,
                  DrawEffectContainer* fx);

    // STUB: DrawSphere(float, Colour, DrawEffectContainer*) -- binary @ 0x00193ee0 (TODO RE)
    // Binary is a stub (returns colour unchanged); port is likewise a no-op.
    void DrawSphere(float radius, Colour colour, DrawEffectContainer* fx);

    // Binary @ 0x001b09b0
    void DrawQuad(Colour colour, SmartPtr<Texture> texture,
                  Vec3 const& pos, Vec3 const& scale, float rotZ,
                  float w, float h, float uOff, float vOff,
                  DrawEffectContainer* fx);

    // Binary @ 0x00194180 — delegates to 6-arg with u0=0,v0=1,u1=0,v1=1 (full texture).
    void DrawQuadUnCached(Colour colour, DrawEffectContainer* fx);

    // Binary @ 0x00194060 — 4-vert TRIANGLE_STRIP unit quad with UV crop [u0,v0]..[u1,v1].
    void DrawQuadUnCached(Colour colour, float u0, float v0, float u1, float v1,
                          DrawEffectContainer* fx);

    // Binary @ 0x0019404c — forwards to DrawTris with primType=GL_TRIANGLES; outer blend ignored.
    void DrawTriList(QUADCUSTOMVERTEX const* verts, long count, bool blend,
                     DrawEffectContainer* fx);

    // Binary @ 0x00194038 — forwards to DrawTris with primType=GL_TRIANGLE_STRIP; outer blend ignored.
    void DrawTriStrip(QUADCUSTOMVERTEX const* verts, long count, bool blend,
                      DrawEffectContainer* fx);

    // Binary @ 0x00193f5c — dispatches to Renderer::DrawTriList or DrawTriStrip by primType.
    void DrawTris(QUADCUSTOMVERTEX const* verts, long count, int primType, bool blend,
                  DrawEffectContainer* fx);

    // STUB-DEFERRED: GenerateBindings(AsciiString const&, AsciiString const&, vector<AnimBindings::Vector::Binding>&) -- binary @ 0x001b0d18
    // AnimBindings::Vector::Binding -- nested-typedef edge case; deferred until AnimBindings is fully ported.

    // ---- end STUBS ----
};

// Namespace-level alias so callers that use Mortar::BoneBinding directly still compile.
// The binary's canonical name is Mortar::Mesh::BoneBinding.
typedef Mesh::BoneBinding BoneBinding;

// Matches original Model (0x58 bytes) with vector<Mortar::SmartPtr<Mesh>>
// Ref: docs/engine/rendering-detail.md — Model::Draw (0x001930e0)
class Model : public Mortar::ReferenceCounter {
public:
    std::string m_Name;
    std::vector<Mortar::SmartPtr<Mesh>> m_Meshes;

    // +0x40: stored skeleton (Skeleton struct, not pointer)
    // Matches model+0x40 from SwapSkeleton (0x001aaba8)
    Skeleton m_Skeleton;

    Model();
    virtual ~Model();

    // Binary @ 0x0019346c — push mesh, then bind skeleton if valid.
    // STUB: by-value overload matches binary signature; kept as sole AddNode.
    void AddNode(SmartPtr<Mesh> mesh);

    // Binary @ 0x001933f8 — unchecked array access (matches binary).
    Mortar::SmartPtr<Mesh> GetNode(unsigned long index) const;

    // Binary @ 0x001933b8 — linear scan by name; dead code (no live callers).
    Mortar::SmartPtr<Mesh> GetNode(const std::string& name) const;

    // Draw all meshes with optional depth-sorting for multi-mesh models
    // Matches 0x001930e0
    void Draw(const Matrix44& transform);

    // Matches Model::SwapSkeleton (0x001aaba8)
    // Calls Skeleton::Swap(Skeleton&) (0x001a89c4) to swap all arrays
    // from skel into m_Skeleton (no matrix rebuild), then calls UpdateBoneLinks.
    void SwapSkeleton(Skeleton& skel);

    // Matches Model::UpdateBoneLinks (0x00193010)
    // Calls BindSkeleton on each mesh with the model's skeleton.
    void UpdateBoneLinks();

    // ---- STUBS (binary) ----
    // STUB: Model(AsciiString const&) -- binary @ 0x???? (TODO RE)
    Model(AsciiString const& name);

    // STUB: Draw(Matrix44 const&) const -- binary @ 0x???? (TODO RE)
    void Draw(Matrix44 const& transform) const;

    // STUB: GetBounds() const -- binary @ 0x???? (TODO RE)
    Bounds3D GetBounds() const;

    // STUB: GetNode(AsciiString const&) const -- binary @ 0x???? (TODO RE)
    SmartPtr<Mesh> GetNode(AsciiString const& name) const;

    // STUB: NodeCount() const -- binary @ 0x???? (TODO RE)
    int NodeCount() const;

    // STUB: SetEffectGroup(SmartPtr<EffectGroup>) -- binary @ 0x???? (TODO RE)
    // Defunct: EffectGroup not ported; stub preserves call-graph shape.
    void SetEffectGroup(SmartPtr<EffectGroup> effectGroup);

    // STUB-DEFERRED: GenerateBindings(AsciiString const&, AsciiString const&, vector<AnimBindings::Bone::Binding>&) const
    // AnimBindings nested-typedef edge case
    // STUB-DEFERRED: GenerateBindings(AsciiString const&, AsciiString const&, vector<AnimBindings::Vector::Binding>&) const
    // AnimBindings nested-typedef edge case
    // ---- end STUBS ----
};

} // namespace Mortar

#endif
