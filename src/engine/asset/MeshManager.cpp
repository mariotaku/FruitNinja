#include "asset/MeshManager.h"
#include "asset/TextureManager.h"
#include <cstdio>
#include <cstring>
#include <functional>

// Analysed: 2026-04-11T14:00

namespace Mortar {

MeshManager* MeshManager::s_instance = NULL;

MeshManager::MeshManager() {
    s_instance = this;
}

MeshManager::~MeshManager() {
    ReleaseAll();
    if (s_instance == this) s_instance = NULL;
}

void MeshManager::Initialise(int capacity) {
    m_Models.reserve(capacity);
}

void MeshManager::ReleaseAll() {
    m_Models.clear();
}

SmartPtr<Model> MeshManager::Load(const char* path) {
    // Check if already loaded
    for (int i = 0; i < m_Models.size(); i++) {
        if (m_Models[i].IsValid() && m_Models[i]->m_Name == path) {
            return m_Models[i];
        }
    }

    SmartPtr<Model> model = LoadMeshInternal(path);
    if (model.IsValid()) {
        m_Models.push_back(model);
    }
    return model;
}

// Helper: compute element size from PSP format code
static int FmtSize(int fmt) {
    switch (fmt) {
        case 0: return 0;
        case 1: return 1;
        case 2: return 2;
        default: return 4;
    }
}

// Matches LoadMeshInternal (0x001a8518) + LoadMesh (0x001a7c90)
// The HBR0 container typically has:
//   root: children=[material_info, stream_data], rawData=mesh_metadata
//   stream_data child: rawData=index_data + vertex_data (sequential)
SmartPtr<Model> MeshManager::LoadMeshInternal(const char* path) {
    ResourceLoader loader(path);
    if (loader.DataSize() == 0 && loader.ChildCount() == 0) {
        fprintf(stderr, "MeshManager: failed to load '%s'\n", path);
        return SmartPtr<Model>();
    }

    Model* model = new Model();
    model->m_Name = path;

    Mesh* mesh = new Mesh();

    // Search all nodes for vertex and index data
    bool hasGeometry = false;
    bool hasIndices = false;

    std::function<void(ResourceLoader&)> searchNode =
        [&](ResourceLoader& node) {
        // Try to parse combined index+vertex data from this node
        if ((!hasGeometry || !hasIndices) && node.DataSize() > 12) {
            const uint8_t* data = node.DataPtr();
            size_t dataSize = node.DataSize();

            // Try index stream first (common ordering in .mmd files)
            if (!hasIndices) {
                size_t consumed = 0;
                if (TryParseIndexStream(data, dataSize, *mesh, consumed)) {
                    hasIndices = true;
                    // Try vertex stream from remaining data
                    if (!hasGeometry && consumed < dataSize) {
                        if (TryParseVertexStream(data + consumed, dataSize - consumed, *mesh)) {
                            hasGeometry = true;
                        }
                    }
                }
            }

            // Try vertex stream from start if not found yet
            if (!hasGeometry && node.DataSize() > 8) {
                if (TryParseVertexStream(data, dataSize, *mesh)) {
                    hasGeometry = true;
                }
            }
        }
        // Recurse into children
        for (size_t i = 0; i < node.ChildCount(); i++) {
            searchNode(node.m_Children[i]);
        }
    };

    // Search root and all children
    searchNode(loader);

    if (!hasGeometry || !hasIndices) {
        printf("[MeshManager] '%s': INCOMPLETE — geometry=%d indices=%d\n",
               path, hasGeometry, hasIndices);
    } else {
        printf("[MeshManager] '%s': OK — verts=%d idx=%d stride=%d vbo=%u ibo=%u\n",
               path, mesh->m_VertexCount, mesh->m_IndexCount,
               mesh->m_VertexStride, mesh->m_VBO, mesh->m_IBO);
    }

    SmartPtr<Mesh> meshPtr(mesh);
    model->m_Meshes.push_back(meshPtr);
    return SmartPtr<Model>(model);
}

// Matches LoadVertexStreamPSP (0x001a7b0c)
bool MeshManager::TryParseVertexStream(const uint8_t* data, size_t dataSize, Mesh& mesh) {
    if (dataSize < 9) return false;

    size_t pos = 0;

    // Skip count (u8) + skip data
    uint8_t skipCount = data[pos++];
    if (skipCount > 16) return false; // sanity check
    pos += skipCount * 4;

    if (pos + 8 > dataSize) return false;

    // Vertex declaration bitfield (u32)
    uint32_t vertDecl = 0;
    memcpy(&vertDecl, data + pos, 4);
    pos += 4;

    // Vertex count (u32)
    uint32_t vertCount = 0;
    memcpy(&vertCount, data + pos, 4);
    pos += 4;

    if (vertCount == 0 || vertCount > 100000) return false;

    // Parse vertex declaration
    int texFmt    = (vertDecl >> 0) & 0x3;
    int colorFmt  = (vertDecl >> 5) & 0x3;
    int normalFmt = (vertDecl >> 7) & 0x3;
    int posFmt    = (vertDecl >> 9) & 0x3;
    if (posFmt == 0) posFmt = 3; // default to float

    // Compute stride and offsets (PSP order: tex, color, normal, position)
    int offset = 0;
    VertexLayout layout;
    memset(&layout, 0, sizeof(layout));

    // Texcoord
    int texBytes = FmtSize(texFmt) * 2;
    layout.texOffset = offset;
    layout.texSize = texBytes;
    offset += texBytes;

    // Color
    int colorBytes = 0;
    if (colorFmt == 1 || colorFmt == 2) colorBytes = 2;
    else if (colorFmt == 3) colorBytes = 4;
    layout.colorOffset = offset;
    layout.colorSize = colorBytes;
    offset += colorBytes;

    // Normal
    int normalBytes = FmtSize(normalFmt) * 3;
    layout.normalOffset = offset;
    layout.normalSize = normalBytes;
    offset += normalBytes;

    // Position
    int posBytes = FmtSize(posFmt) * 3;
    layout.posOffset = offset;
    layout.posSize = posBytes;
    offset += posBytes;

    layout.totalStride = offset;

    if (layout.totalStride == 0) return false;

    size_t vertDataSize = (size_t)vertCount * layout.totalStride;
    if (pos + vertDataSize > dataSize) return false;

    // Upload vertex data to GPU
    glGenBuffers(1, &mesh.m_VBO);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.m_VBO);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vertDataSize,
                 data + pos, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    mesh.m_VertexCount = (int)vertCount;
    mesh.m_VertexStride = layout.totalStride;
    mesh.m_Layout = layout;

    return true;
}

// Matches LoadIndexStreamPSP (0x001a799c)
// Returns true and sets `consumed` to bytes used if successful
bool MeshManager::TryParseIndexStream(const uint8_t* data, size_t dataSize,
                                       Mesh& mesh, size_t& consumed) {
    consumed = 0;
    if (dataSize < 7) return false;

    size_t pos = 0;

    // Skip 2 padding bytes
    pos += 2;

    // Index flags byte
    uint8_t idxFlags = data[pos++];
    uint8_t primBits = idxFlags & 0xF0;

    switch (primBits) {
        case 0x20: mesh.m_PrimType = GL_TRIANGLE_STRIP; break;
        case 0x40: mesh.m_PrimType = GL_TRIANGLES; break;
        default:   mesh.m_PrimType = GL_TRIANGLE_STRIP; break;
    }

    if (pos + 4 > dataSize) return false;

    // Index count (u32)
    uint32_t idxCount = 0;
    memcpy(&idxCount, data + pos, 4);
    pos += 4;

    if (idxCount == 0 || idxCount > 100000) return false;

    size_t idxDataSize = (size_t)idxCount * 2; // uint16 indices
    if (pos + idxDataSize > dataSize) return false;

    // Upload index data to GPU
    glGenBuffers(1, &mesh.m_IBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.m_IBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)idxDataSize,
                 data + pos, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    mesh.m_IndexCount = (int)idxCount;
    consumed = pos + idxDataSize;

    return true;
}

} // namespace Mortar
