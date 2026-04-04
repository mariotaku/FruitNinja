#ifndef MORTAR_ASSET_MESH_H
#define MORTAR_ASSET_MESH_H

#include "util/ReferenceCounter.h"
#include "util/SmartPtr.h"
#include "asset/Texture.h"
#include "render/gl_funcs.h"
#include "math/Matrix44.h"
#include <vector>
#include <cstdint>
#include <string>

namespace Mortar {

// Simplified port of the original Mesh (~0x7C bytes)
// Skips full Effect/GeometryBinding system; uses Renderer's GLES2 shaders directly
class MortarMesh : public ReferenceCounter {
public:
    std::string m_Name;

    // Vertex data (interleaved, PSP-legacy format parsed)
    GLuint m_VBO;
    GLuint m_IBO;
    int m_VertexCount;
    int m_IndexCount;
    int m_VertexStride;
    GLenum m_PrimType;

    // Texture reference
    SmartPtr<Texture> m_DiffuseTexture;
    std::string m_TexturePath; // relative path from .mmd

    // Vertex attribute layout (from PSP vertex declaration)
    struct VertexLayout {
        int posOffset;    int posSize;     // 3 floats typically
        int normalOffset; int normalSize;  // 3 floats or 3 shorts
        int colorOffset;  int colorSize;   // 4 bytes RGBA
        int texOffset;    int texSize;     // 2 floats
        int totalStride;
    };
    VertexLayout m_Layout;

    MortarMesh();
    virtual ~MortarMesh();

    // Draw this mesh with a given world transform
    // Uses Renderer's 3D shader
    void Draw(const Matrix44& worldTransform);
};

// Matches original Model with vector<SmartPtr<Mesh>>
// Ref: docs/engine/rendering-detail.md — Model::Draw (0x001930e0)
class Model : public ReferenceCounter {
public:
    std::string m_Name;
    std::vector<SmartPtr<MortarMesh>> m_Meshes;

    Model();
    virtual ~Model();

    // Draw all meshes with optional depth-sorting for multi-mesh models
    // Matches 0x001930e0
    void Draw(const Matrix44& transform);
};

} // namespace Mortar

#endif
