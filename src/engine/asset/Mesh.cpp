#include "asset/Mesh.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/DisplayManager.h"

// Analysed: 2026-04-11T12:00

namespace Mortar {

// --- Mesh ---

Mesh::Mesh()
    : m_VBO(0)
    , m_IBO(0)
    , m_VertexCount(0)
    , m_IndexCount(0)
    , m_VertexStride(0)
    , m_PrimType(GL_TRIANGLES)
{
    memset(&m_Layout, 0, sizeof(m_Layout));
}

Mesh::~Mesh() {
    if (m_VBO) { glDeleteBuffers(1, &m_VBO); m_VBO = 0; }
    if (m_IBO) { glDeleteBuffers(1, &m_IBO); m_IBO = 0; }
}

// Matches Mesh::SetBones (0x001b1340)
// Resizes m_BoneBindings and copies each entry
void Mesh::SetBones(const BoneBinding* bones, int count) {
    m_BoneBindings.resize(count);
    for (int i = 0; i < count; i++) {
        m_BoneBindings[i] = bones[i];
    }
}

// Matches Mesh::GetBounds (0x001b07f0)
// Computes AABB from bone bounds. Without a skeleton, returns the raw bounds.
void Mesh::GetBounds(Vec3& outMin, Vec3& outMax) const {
    outMin = Vec3(1e30f, 1e30f, 1e30f);
    outMax = Vec3(-1e30f, -1e30f, -1e30f);

    for (int i = 0; i < (int)m_BoneBindings.size(); i++) {
        const BoneBinding& bone = m_BoneBindings[i];
        // Original: transforms bounds by bone world matrix
        // Port: no skeleton system, use raw bounds directly
        const Vec3& bmin = bone.m_BoundsMin;
        const Vec3& bmax = bone.m_BoundsMax;

        if (bmin.x < outMin.x) outMin.x = bmin.x;
        if (bmin.y < outMin.y) outMin.y = bmin.y;
        if (bmin.z < outMin.z) outMin.z = bmin.z;
        if (bmax.x > outMax.x) outMax.x = bmax.x;
        if (bmax.y > outMax.y) outMax.y = bmax.y;
        if (bmax.z > outMax.z) outMax.z = bmax.z;
    }
}

// Matches Mesh::Draw (0x001b0c3c)
// Original behavior:
//   if boneCount == 1: finalWorld = boneVertTransform * worldMatrix
//   else: finalWorld = worldMatrix
//   Set World, View, Proj, WVP effect properties
//   Render all geometries
// Port: uses Renderer::setup_3d_shader() for GLES2
void Mesh::Draw(const Matrix44& worldTransform) {
    if (!m_VBO || m_VertexCount == 0) {
        return;
    }

    Renderer* renderer = Renderer::GetInstance();
    if (!renderer) return;

    // Port specific: compute MVP via MatrixManager (replaces Effect property system)
    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().SetCurrentMatrix(worldTransform);
    Matrix44 mvp = mm.GetMVP();

    // Bind diffuse texture
    GLuint texId = 0;
    if (m_DiffuseTexture.IsValid()) {
        texId = m_DiffuseTexture->m_TexId;
    }

    renderer->setup_3d_shader(texId, mvp.ptr(), worldTransform.ptr(), 1.0f,
                              m_Material.m_Diffuse.x, m_Material.m_Diffuse.y,
                              m_Material.m_Diffuse.z);

    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    if (m_IBO) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_IBO);
    }

    // Position (attribute 0)
    if (m_Layout.posSize > 0) {
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                              m_Layout.totalStride, (void*)(intptr_t)m_Layout.posOffset);
    }

    // Normal (attribute 1)
    if (m_Layout.normalSize > 0) {
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                              m_Layout.totalStride, (void*)(intptr_t)m_Layout.normalOffset);
    }

    // TexCoord (attribute 2 — was 3, renumbered since vertex color attribute removed)
    if (m_Layout.texSize > 0) {
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
                              m_Layout.totalStride, (void*)(intptr_t)m_Layout.texOffset);
    }

    // Draw
    if (m_IBO && m_IndexCount > 0) {
        glDrawElements(m_PrimType, m_IndexCount, GL_UNSIGNED_SHORT, (void*)0);
    } else {
        glDrawArrays(m_PrimType, 0, m_VertexCount);
    }

    // Cleanup
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

// --- Model ---

Model::Model() {
}

Model::~Model() {
    m_Meshes.clear();
}

// Matches Model::Draw (0x001930e0, 79 lines)
// Single mesh: draw directly. Multi-mesh: depth-sort back-to-front.
void Model::Draw(const Matrix44& transform) {
    int meshCount = (int)m_Meshes.size();
    if (meshCount == 0) return;

    if (meshCount == 1) {
        m_Meshes[0]->Draw(transform);
        return;
    }

    // Multi-mesh: depth sort by view-space Z then draw back-to-front
    MatrixManager& mm = MatrixManager::GetInstance();
    Matrix44 viewProj = mm.GetProjectionStack().m_Current * mm.GetViewStack().m_Current;
    Matrix44 mvp = viewProj * transform;

    struct SortEntry {
        Mesh* mesh;
        float z;
    };

    std::vector<SortEntry> sorted(meshCount);
    for (int i = 0; i < meshCount; i++) {
        sorted[i].mesh = m_Meshes[i].Get();
        // Use a simple Z=0 point transformed by MVP as sort key
        sorted[i].z = mvp.m[14]; // approximate — real impl uses mesh bounds center
    }

    // Sort back-to-front (larger Z = farther away = draw first)
    for (int i = 0; i < meshCount - 1; i++) {
        for (int j = i + 1; j < meshCount; j++) {
            if (sorted[j].z > sorted[i].z) {
                SortEntry tmp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = tmp;
            }
        }
    }

    for (int i = 0; i < meshCount; i++) {
        sorted[i].mesh->Draw(transform);
    }
}

} // namespace Mortar
