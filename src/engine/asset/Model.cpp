#include "asset/Model.h"
#include "asset/Mesh.h"
#include "render/MatrixManager.h"

namespace Mortar {

// Binary @ 0x0019326c
Model::Model() {}

// Binary @ 0x0019326c
Model::Model(AsciiString const& name) {
    m_Name = name.c_str();
}

// Binary @ 0x00193b3c / 0x00193bac
Model::~Model() {
    m_Meshes.clear();
}

// Binary @ 0x001aaba8 — Skeleton::Swap then UpdateBoneLinks.
// Calls Skeleton::Swap(Skeleton&) (0x001a89c4): swaps all four arrays
// without rebuilding matrices, then rebinds each mesh.
void Model::SwapSkeleton(Skeleton& skel) {
    m_Skeleton.Swap(skel);
    UpdateBoneLinks();
}

// Binary @ 0x00193010 — BindSkeleton(vtable[8]) on each mesh.
void Model::UpdateBoneLinks() {
    for (int i = 0; i < (int)m_Meshes.size(); i++) {
        if (m_Meshes[i].IsValid()) {
            m_Meshes[i]->BindSkeleton(&m_Skeleton);
        }
    }
}

// Binary @ 0x0019346c — push_back then BindSkeleton.
void Model::AddNode(SmartPtr<Mesh> mesh) {
    m_Meshes.push_back(mesh);
    if (mesh.IsValid()) mesh->BindSkeleton(&m_Skeleton);
}

// Binary @ 0x001933f8 — unchecked array access.
Mortar::SmartPtr<Mesh> Model::GetNode(unsigned long index) const {
    return m_Meshes[index];
}

// Binary @ 0x00193414 — linear scan by std::string name; null on miss.
Mortar::SmartPtr<Mesh> Model::GetNode(const std::string& name) const {
    for (int i = 0; i < (int)m_Meshes.size(); i++) {
        if (m_Meshes[i].IsValid() && m_Meshes[i]->m_Name == name) {
            return m_Meshes[i];
        }
    }
    return Mortar::SmartPtr<Mesh>();
}

// Binary @ 0x00193414 — AsciiString overload; delegates to std::string variant.
Mortar::SmartPtr<Mesh> Model::GetNode(AsciiString const& name) const {
    return GetNode(std::string(name.c_str()));
}

// AlphaSortNode — qsort entry struct used by Model::Draw (binary @ 0x001935a0).
// File-scope: GCC 4.4 rejects local structs as template arguments.
struct AlphaSortNode {
    Mesh* mesh;
    float key;  // perspective-divided clip-space z: z'/w'
};

// Matches Mortar::AlphaSortNode::compare (binary @ 0x001935a0).
static int AlphaSortNode_compare(const void* a, const void* b) {
    float diff = ((const AlphaSortNode*)b)->key
               - ((const AlphaSortNode*)a)->key;
    if (diff > 0.0f) return  1;
    if (diff < 0.0f) return -1;
    return 0;
}

// Binary @ 0x001930e0 — single-mesh fast path + multi-mesh depth-sort back-to-front.
// Single mesh: m_Meshes.front()->Draw(transform) via vtable slot 4.
// Multi-mesh: compute MVP, compute per-mesh clip-z key, qsort descending, draw in order.
void Model::Draw(const Matrix44& transform) {
    int meshCount = (int)m_Meshes.size();
    if (meshCount == 0) return;

    if (meshCount == 1) {
        m_Meshes[0]->Draw(transform);
        return;
    }

    // Multi-mesh path (binary 0x001930e0):
    //   localProj = transform * projTop
    //   mvp       = localProj * viewTop   (row-vector convention: T * P * V)
    MatrixManager& mm = MatrixManager::GetInstance();
    const Matrix44& projTop = mm.GetProjectionStack().m_Current;
    const Matrix44& viewTop = mm.GetViewStack().m_Current;
    Matrix44 localProj = transform * projTop;
    Matrix44 mvp       = localProj * viewTop;

    std::vector<AlphaSortNode> sorted(meshCount);
    for (int i = 0; i < meshCount; i++) {
        sorted[i].mesh = m_Meshes[i].Get();

        // Per-mesh sort key (binary 0x001930e0):
        //   b = mesh->GetBounds()       (vtable +0x14, slot 5)
        //   c = (b.min + b.max) * 0.5f  (Bounds3D::Center @ 0x001936d0)
        //   z' = c dot col2 + col2.row3
        //   w' = c dot col3 + col3.row3
        //   key = z'/w'
        // Matrix44 column-major: m[col*4 + row].
        // Col 2 (z): m[8..11]; col 3 (w): m[12..15].
        Bounds3D b = m_Meshes[i]->GetBounds();
        Vec3 c;
        c.x = (b.min.x + b.max.x) * 0.5f;
        c.y = (b.min.y + b.max.y) * 0.5f;
        c.z = (b.min.z + b.max.z) * 0.5f;

        float zp = c.x * mvp.m[8]  + c.y * mvp.m[9]  + c.z * mvp.m[10] + mvp.m[11];
        float wp = c.x * mvp.m[12] + c.y * mvp.m[13] + c.z * mvp.m[14] + mvp.m[15];
        sorted[i].key = (wp != 0.0f) ? (zp / wp) : 0.0f;
    }

    std::qsort(&sorted[0], (size_t)meshCount, sizeof(AlphaSortNode), AlphaSortNode_compare);

    for (int i = 0; i < meshCount; i++) {
        sorted[i].mesh->Draw(transform);
    }
}

// Binary @ 0x00192fa8 — union of per-mesh bounds; seed from mesh[0].
// Binary has no empty-guard (UB on empty vector); port adds the guard.
Bounds3D Model::GetBounds() const {
    if (m_Meshes.empty()) return Bounds3D();
    Bounds3D acc = m_Meshes[0]->GetBounds();
    for (int i = 1; i < (int)m_Meshes.size(); i++) {
        Bounds3D b = m_Meshes[i]->GetBounds();
        if (b.min.x < acc.min.x) acc.min.x = b.min.x;
        if (b.min.y < acc.min.y) acc.min.y = b.min.y;
        if (b.min.z < acc.min.z) acc.min.z = b.min.z;
        if (b.max.x > acc.max.x) acc.max.x = b.max.x;
        if (b.max.y > acc.max.y) acc.max.y = b.max.y;
        if (b.max.z > acc.max.z) acc.max.z = b.max.z;
    }
    return acc;
}

// Binary @ 0x00192f04 — m_Meshes.size().
int Model::NodeCount() const {
    return (int)m_Meshes.size();
}

// Binary @ 0x0019335c — walk meshes; per-geom Geometry calls are binary stubs
// on both sides (Geometry::EffectGroupSet @ 0x001a00f8, Geometry::SetActiveEffect
// are BX LR stubs). RebuildEffectBindings is Defunct on port side.
// Net effect identical to binary: no observable side effects.
void Model::SetEffectGroup(Mortar::SmartPtr<EffectGroup> /*effectGroup*/) {
    for (int i = 0; i < (int)m_Meshes.size(); i++) {
        Mesh* mesh = m_Meshes[i].Get();
        if (!mesh) continue;
        // Defunct: Geometry::EffectGroupSet / SetActiveEffect are binary stubs;
        // Geometry class not ported (replaced by GeometryEntry).
        // Defunct: RebuildEffectBindings -- no-op stub; binary @ 0x001b08e8
        mesh->RebuildEffectBindings();
    }
}

} // namespace Mortar
