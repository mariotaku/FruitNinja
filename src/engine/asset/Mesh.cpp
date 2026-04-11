#include "asset/Mesh.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/DisplayManager.h"

// Analysed: 2026-04-11T12:00

namespace Mortar {

// --- Mesh ---

Mesh::Mesh() {}

Mesh::~Mesh() {
    for (int i = 0; i < (int)m_Geometries.size(); i++) {
        if (m_Geometries[i].vbo) { glDeleteBuffers(1, &m_Geometries[i].vbo); }
        if (m_Geometries[i].ibo) { glDeleteBuffers(1, &m_Geometries[i].ibo); }
    }
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

void Mesh::SetDiffuseTexture(const SmartPtr<Texture>& tex) {
    for (int i = 0; i < (int)m_Materials.size(); i++) {
        if (!m_Materials[i].m_Texture.IsValid()) {
            m_Materials[i].m_Texture = tex;
        }
    }
}

bool Mesh::HasDiffuseTexture() const {
    for (int i = 0; i < (int)m_Materials.size(); i++) {
        if (m_Materials[i].m_Texture.IsValid()) return true;
    }
    return false;
}

// Draw a single geometry entry with its bound material.
// Factored out of Draw() to avoid repetition in the geometry loop.
static void DrawGeometry(Renderer* renderer, const GeometryEntry& geom,
                         const MeshMaterial& mat, const Matrix44& mvp,
                         const Matrix44& world) {
    if (!geom.vbo || geom.vertCount == 0) return;

    GLuint texId = mat.m_Texture.IsValid() ? mat.m_Texture->m_TexId : 0;
    renderer->setup_3d_shader(texId, mvp.ptr(), world.ptr(), 1.0f);

    glBindBuffer(GL_ARRAY_BUFFER, geom.vbo);
    if (geom.ibo) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geom.ibo);
    }

    const VertexLayout& L = geom.layout;

    // Position (attribute 0)
    if (L.posSize > 0) {
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                              L.totalStride, (void*)(intptr_t)L.posOffset);
    }

    // Normal (attribute 1)
    if (L.normalSize > 0) {
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                              L.totalStride, (void*)(intptr_t)L.normalOffset);
    } else {
        glDisableVertexAttribArray(1);
        glVertexAttrib3f(1, 0.0f, 0.0f, 1.0f); // default normal
    }

    // Vertex color (attribute 2) — GL_MODULATE: texture × vertex_color
    // Matches PassBinding::Apply (0x001a39f8) GL_MODULATE semantics.
    // If no color data in stream: constant white so texture is unmodified.
    if (L.colorSize > 0 && L.colorFmt == 3) {
        // RGBA8888 — 4 bytes per vertex, normalized to [0,1]
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                              L.totalStride, (void*)(intptr_t)L.colorOffset);
    } else {
        // No vertex color data or unsupported 16-bit format: use constant white
        glDisableVertexAttribArray(2);
        glVertexAttrib4f(2, 1.0f, 1.0f, 1.0f, 1.0f);
    }

    // Texcoord (attribute 3)
    if (L.texSize > 0) {
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE,
                              L.totalStride, (void*)(intptr_t)L.texOffset);
    } else {
        glDisableVertexAttribArray(3);
        glVertexAttrib2f(3, 0.0f, 0.0f);
    }

    // Draw
    if (geom.ibo && geom.indexCount > 0) {
        glDrawElements(geom.primType, geom.indexCount, GL_UNSIGNED_SHORT, (void*)0);
    } else {
        glDrawArrays(geom.primType, 0, geom.vertCount);
    }

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(3);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

// Matches Mesh::Draw (0x001b0c3c)
// Original behavior:
//   if boneCount == 1: finalWorld = boneVertTransform * worldMatrix
//   else: finalWorld = worldMatrix
//   Set World, View, Proj, WVP effect properties
//   Render all geometries (each with its own material)
// Port: uses Renderer::setup_3d_shader() for GLES2; no skeleton system yet
void Mesh::Draw(const Matrix44& worldTransform) {
    if (m_Geometries.empty()) return;

    Renderer* renderer = Renderer::GetInstance();
    if (!renderer) return;

    // Port specific: compute MVP via MatrixManager (replaces Effect property system)
    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().SetCurrentMatrix(worldTransform);
    Matrix44 mvp = mm.GetMVP();

    // Render all geometry entries, each with its own material
    for (int i = 0; i < (int)m_Geometries.size(); i++) {
        const GeometryEntry& geom = m_Geometries[i];
        int matIdx = geom.materialIndex;

        // Fallback: use first material if index out of range
        const MeshMaterial& mat = (matIdx >= 0 && matIdx < (int)m_Materials.size())
                                  ? m_Materials[matIdx]
                                  : (m_Materials.empty() ? MeshMaterial() : m_Materials[0]);

        DrawGeometry(renderer, geom, mat, mvp, worldTransform);
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
