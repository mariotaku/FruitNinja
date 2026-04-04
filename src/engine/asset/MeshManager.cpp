#include "asset/MeshManager.h"
#include "asset/TextureManager.h"
#include <cstdio>
#include <cstring>

namespace Mortar {

MeshManager::MeshManager() {
}

MeshManager::~MeshManager() {
    ReleaseAll();
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

// Matches LoadMeshInternal (0x001a8518) + LoadMesh (0x001a7c90)
SmartPtr<Model> MeshManager::LoadMeshInternal(const char* path) {
    ResourceLoader loader(path);
    if (loader.DataSize() == 0 && loader.ChildCount() == 0) {
        fprintf(stderr, "MeshManager: failed to load '%s'\n", path);
        return SmartPtr<Model>();
    }

    Model* model = new Model();
    model->m_Name = path;

    // Parse mesh data from ResourceLoader
    // The HBR0 structure has children for vertex streams, index streams,
    // material info, and texture paths
    MortarMesh* mesh = new MortarMesh();

    // Try to extract texture path from the resource data
    // In the original, the mesh loader reads string properties for texture names
    ResourceLoader texPathLoader = loader;
    texPathLoader.ResetReadPos();

    // Scan children for vertex and index data
    bool hasGeometry = false;

    for (size_t i = 0; i < loader.ChildCount(); i++) {
        ResourceLoader& child = loader.m_Children[i];

        // Try loading as vertex stream
        if (!hasGeometry && child.DataSize() > 8) {
            child.ResetReadPos();

            // Check if this child contains vertex/index data
            // The format has: skip_count(u8), skip_data, vert_decl(u32), vert_count(u32)
            if (LoadVertexStream(child, *mesh)) {
                hasGeometry = true;
            }
        }

        // Check sub-children for index streams
        for (size_t j = 0; j < child.ChildCount(); j++) {
            ResourceLoader& subChild = child.m_Children[j];
            if (subChild.DataSize() > 4) {
                subChild.ResetReadPos();
                LoadIndexStream(subChild, *mesh);
            }
        }
    }

    // If no children parsed, try the raw data directly (flat .mmd structure)
    if (!hasGeometry && loader.DataSize() > 16) {
        loader.ResetReadPos();

        // Try to find index data first, then vertex data
        // Index format: flags_byte, pad, pad, index_count(u32), indices...
        // Vertex format: skip_count(u8), skip_data, vert_decl(u32), vert_count(u32), verts...

        // Simple heuristic: scan for recognizable patterns
        LoadIndexStream(loader, *mesh);
        loader.ResetReadPos();
        LoadVertexStream(loader, *mesh);
    }

    SmartPtr<MortarMesh> meshPtr(mesh);
    model->m_Meshes.push_back(meshPtr);
    return SmartPtr<Model>(model);
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

// Matches LoadVertexStreamPSP (0x001a7b0c)
bool MeshManager::LoadVertexStream(ResourceLoader& loader, MortarMesh& mesh) {
    if (loader.DataSize() < 9) return false;

    const uint8_t* data = loader.DataPtr();
    size_t pos = 0;

    // Skip count (u8) + skip data
    uint8_t skipCount = data[pos++];
    pos += skipCount * 4;

    if (pos + 8 > loader.DataSize()) return false;

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
    MortarMesh::VertexLayout layout;
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
    if (pos + vertDataSize > loader.DataSize()) return false;

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
bool MeshManager::LoadIndexStream(ResourceLoader& loader, MortarMesh& mesh) {
    if (loader.DataSize() < 7) return false;

    const uint8_t* data = loader.DataPtr();
    size_t pos = 0;

    // Skip 2 padding bytes
    pos += 2;

    // Index flags byte
    uint8_t idxFlags = data[pos++];
    uint8_t primBits = idxFlags & 0xF0;

    switch (primBits) {
        case 0x40: mesh.m_PrimType = GL_TRIANGLES; break;
        default:   mesh.m_PrimType = GL_TRIANGLE_STRIP; break;
    }

    if (pos + 4 > loader.DataSize()) return false;

    // Index count (u32)
    uint32_t idxCount = 0;
    memcpy(&idxCount, data + pos, 4);
    pos += 4;

    if (idxCount == 0 || idxCount > 100000) return false;

    size_t idxDataSize = (size_t)idxCount * 2; // uint16 indices
    if (pos + idxDataSize > loader.DataSize()) return false;

    // Upload index data to GPU
    glGenBuffers(1, &mesh.m_IBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.m_IBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)idxDataSize,
                 data + pos, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    mesh.m_IndexCount = (int)idxCount;

    return true;
}

} // namespace Mortar
