#ifndef MORTAR_ASSET_MESH_H
#define MORTAR_ASSET_MESH_H

// Analysed: 2026-04-11T12:00

#include "util/ReferenceCounter.h"
#include "util/SmartPtr.h"
#include "asset/Texture.h"
#include "render/gl_funcs.h"
#include "math/Matrix44.h"
#include "math/Vec3.h"
#include <vector>
#include <cstdint>
#include <string>

namespace Mortar {

// Matches original Mortar::Mesh::BoneBinding (0x44 = 68 bytes)
// Ref: docs/engine/mesh.md
struct BoneBinding {
    std::string m_Name;         // +0x00: Bone name (port: std::string instead of AsciiString)
    Vec3 m_BoundsMin;           // +0x28 equiv: Local AABB minimum
    Vec3 m_BoundsMax;           // +0x34 equiv: Local AABB maximum
    int m_SkeletonIndex;        // +0x40: Index into Skeleton; -1 = unbound

    BoneBinding() : m_SkeletonIndex(-1) {}
};

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
    SmartPtr<Texture> m_Texture;    // DiffuseMap texture
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
// Inherits: ReferenceCounter → IModelNode → Mesh
// Port specific: skips Effect/Geometry/SharedEffectProperties system,
// uses direct GLES2 calls via Renderer::setup_3d_shader()
// Ref: docs/engine/mesh.md — struct layout, vtable, Draw behavior
class Mesh : public ReferenceCounter {
public:
    // --- Original fields (matching offsets where applicable) ---

    std::string m_Name;                         // +0x0C equiv: Mesh name

    std::vector<BoneBinding> m_BoneBindings;    // +0x34 equiv: Bone binding array

    // +0x40 equiv: Geometry submeshes (original: vector<SmartPtr<Geometry>>)
    // Port: each entry has its own VBO/IBO pair + material index
    std::vector<GeometryEntry> m_Geometries;

    // Material array (original: map<AsciiString, SharedPropsInfo> + SharedEffectProperties)
    // Port: parallel array indexed by GeometryEntry::materialIndex
    std::vector<MeshMaterial> m_Materials;

    Mesh();
    virtual ~Mesh();

    // Matches Mesh::Draw (0x001b0c3c)
    void Draw(const Matrix44& worldTransform);

    // Matches Mesh::SetBones (0x001b1340)
    void SetBones(const BoneBinding* bones, int count);

    // Matches Mesh::GetBounds (0x001b07f0)
    void GetBounds(Vec3& outMin, Vec3& outMax) const;

    // Matches Mesh::GetGeometryCount (0x001b1678)
    int GetGeometryCount() const { return (int)m_Geometries.size(); }

    // Matches Mesh::GetName (0x001b15e0)
    const std::string& GetName() const { return m_Name; }

    // Port helper: assign texture to all materials that have none.
    // Used by Fruit.cpp to assign fruit_atlas when loaded externally.
    void SetDiffuseTexture(const SmartPtr<Texture>& tex);

    // Port helper: true if any material has a valid texture.
    bool HasDiffuseTexture() const;
};

// Matches original Model (0x58 bytes) with vector<SmartPtr<Mesh>>
// Ref: docs/engine/rendering-detail.md — Model::Draw (0x001930e0)
class Model : public ReferenceCounter {
public:
    std::string m_Name;
    std::vector<SmartPtr<Mesh>> m_Meshes;

    Model();
    virtual ~Model();

    // Draw all meshes with optional depth-sorting for multi-mesh models
    // Matches 0x001930e0
    void Draw(const Matrix44& transform);
};

} // namespace Mortar

#endif
