#include "asset/Mesh.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/DisplayManager.h"

namespace Mortar {

MortarMesh::MortarMesh()
    : m_VBO(0)
    , m_IBO(0)
    , m_VertexCount(0)
    , m_IndexCount(0)
    , m_VertexStride(0)
    , m_PrimType(GL_TRIANGLES)
{
    memset(&m_Layout, 0, sizeof(m_Layout));
}

MortarMesh::~MortarMesh() {
    if (m_VBO) { glDeleteBuffers(1, &m_VBO); m_VBO = 0; }
    if (m_IBO) { glDeleteBuffers(1, &m_IBO); m_IBO = 0; }
}

void MortarMesh::Draw(const Matrix44& worldTransform) {
    if (!m_VBO || m_VertexCount == 0) return;

    // Bind diffuse texture if available
    if (m_DiffuseTexture.IsValid()) {
        m_DiffuseTexture->Set();
    }

    // Set world matrix on the MatrixManager world stack
    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().SetCurrentMatrix(worldTransform);

    // Get combined MVP
    Matrix44 mvp = mm.GetMVP();

    // Get light direction from DisplayManager
    DisplayManager& dm = DisplayManager::GetInstance();
    Vec3 lightDir = dm.m_lightDirection;

    // Use the 3D shader path
    // The existing Renderer draw_mesh expects raw buffers, but we need to draw
    // using our VBO/IBO with the correct vertex layout.
    // For now, use GL directly matching the 3D shader layout.
    // TODO: Refactor to use Renderer's 3D shader program directly

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

    // Color (attribute 2)
    if (m_Layout.colorSize > 0) {
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                              m_Layout.totalStride, (void*)(intptr_t)m_Layout.colorOffset);
    }

    // TexCoord (attribute 3)
    if (m_Layout.texSize > 0) {
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE,
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
    glDisableVertexAttribArray(3);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    if (m_DiffuseTexture.IsValid()) {
        m_DiffuseTexture->UnSet();
    }
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
        MortarMesh* mesh;
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
