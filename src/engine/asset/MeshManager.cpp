#include "asset/MeshManager.h"
#include "asset/TextureManager.h"
#include <cstdio>
#include <cstring>
#include <functional>

// Analysed: 2026-04-11T16:00

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

// Matches GetColourRGB (0x001a74bc)
// Extracts RGB floats from uint32: byte0=R, byte1=G, byte2=B
static Vec3 GetColourRGB(uint32_t color) {
    float r = (float)(color & 0xFF) / 255.0f;
    float g = (float)((color >> 8) & 0xFF) / 255.0f;
    float b = (float)((color >> 16) & 0xFF) / 255.0f;
    return Vec3(r, g, b);
}

static int FmtSize(int fmt) {
    switch (fmt) {
        case 0: return 0;
        case 1: return 1;
        case 2: return 2;
        default: return 4;
    }
}

static bool ParseVertexStream(const uint8_t* data, size_t dataSize, Mesh& mesh) {
    if (dataSize < 9) return false;
    size_t pos = 0;
    uint8_t skipCount = data[pos++];
    if (skipCount > 16) return false;
    pos += skipCount * 4;
    if (pos + 8 > dataSize) return false;

    uint32_t vertDecl = 0;
    memcpy(&vertDecl, data + pos, 4); pos += 4;
    uint32_t vertCount = 0;
    memcpy(&vertCount, data + pos, 4); pos += 4;
    if (vertCount == 0 || vertCount > 100000) return false;

    int texFmt    = (vertDecl >> 0) & 0x3;
    int colorFmt  = (vertDecl >> 5) & 0x3;
    int normalFmt = (vertDecl >> 7) & 0x3;
    int posFmt    = (vertDecl >> 9) & 0x3;
    if (posFmt == 0) posFmt = 3;

    int offset = 0;
    VertexLayout layout;
    memset(&layout, 0, sizeof(layout));

    int texBytes = FmtSize(texFmt) * 2;
    layout.texOffset = offset; layout.texSize = texBytes; offset += texBytes;
    int colorBytes = (colorFmt == 1 || colorFmt == 2) ? 2 : (colorFmt == 3 ? 4 : 0);
    layout.colorOffset = offset; layout.colorSize = colorBytes; offset += colorBytes;
    int normalBytes = FmtSize(normalFmt) * 3;
    layout.normalOffset = offset; layout.normalSize = normalBytes; offset += normalBytes;
    int posBytes = FmtSize(posFmt) * 3;
    layout.posOffset = offset; layout.posSize = posBytes; offset += posBytes;
    layout.totalStride = offset;
    if (layout.totalStride == 0) return false;

    size_t vertDataSize = (size_t)vertCount * layout.totalStride;
    if (pos + vertDataSize > dataSize) return false;

    glGenBuffers(1, &mesh.m_VBO);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.m_VBO);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vertDataSize, data + pos, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    mesh.m_VertexCount = (int)vertCount;
    mesh.m_VertexStride = layout.totalStride;
    mesh.m_Layout = layout;
    return true;
}

static bool ParseIndexStream(const uint8_t* data, size_t dataSize, Mesh& mesh, size_t& consumed) {
    consumed = 0;
    if (dataSize < 7) return false;
    size_t pos = 2; // skip padding
    uint8_t idxFlags = data[pos++];
    switch (idxFlags & 0xF0) {
        case 0x20: mesh.m_PrimType = GL_TRIANGLE_STRIP; break;
        case 0x40: mesh.m_PrimType = GL_TRIANGLES; break;
        default:   mesh.m_PrimType = GL_TRIANGLE_STRIP; break;
    }
    if (pos + 4 > dataSize) return false;
    uint32_t idxCount = 0;
    memcpy(&idxCount, data + pos, 4); pos += 4;
    if (idxCount == 0 || idxCount > 100000) return false;
    size_t idxDataSize = (size_t)idxCount * 2;
    if (pos + idxDataSize > dataSize) return false;

    glGenBuffers(1, &mesh.m_IBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.m_IBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)idxDataSize, data + pos, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    mesh.m_IndexCount = (int)idxCount;
    consumed = pos + idxDataSize;
    return true;
}

// Matches LoadMeshInternal (0x001a8518) + LoadModel (0x001a8468) + LoadMesh (0x001a7c90)
//
// Original flow: LoadMeshInternal registers loaders for IVertexStream, IIndexStream,
// Model, and Mesh types, then calls ResourceLoader::Load<Model>(). The recursive
// delegate system creates nested ResourceLoaders for each sub-resource type.
//
// Port: We parse the HBR0 container directly, extracting:
//   - Material data from children (material name, colors, texture refs)
//   - Geometry data from children (index + vertex streams)
// The root rawData contains model-level metadata (name, skeleton, mesh count).
SmartPtr<Model> MeshManager::LoadMeshInternal(const char* path) {
    ResourceLoader loader(path);
    if (loader.DataSize() == 0 && loader.ChildCount() == 0) {
        fprintf(stderr, "MeshManager: failed to load '%s'\n", path);
        return SmartPtr<Model>();
    }

    Model* model = new Model();
    model->m_Name = path;
    Mesh* mesh = new Mesh();

    // --- Parse material data from children ---
    // Material children have rawData starting with a ReadString (material name)
    // and contain a grandchild with texture path info.
    // Material child rawData: name, sub-resource-index(u32), 4×u32 colors, float specular
    for (size_t ci = 0; ci < loader.ChildCount(); ci++) {
        ResourceLoader& child = loader.m_Children[ci];
        // Material children have grandchildren (texture info) and rawData > 20 bytes
        if (child.ChildCount() > 0 && child.DataSize() > 20) {
            child.ResetReadPos();

            // Read material name
            AsciiString matName = child.ReadString();
            mesh->m_Material.m_Name = matName.CStr();

            // ReadSubResourceLookup → texture grandchild
            ResourceLoader* texChild = child.ReadSubResourceLookup();
            if (texChild) {
                AsciiString texName = texChild->ReadString();
                AsciiString texRelPath = texChild->ReadString();

                std::string texPath = texRelPath.CStr();
                for (size_t j = 0; j < texPath.size(); j++)
                    if (texPath[j] == '\\') texPath[j] = '/';
                std::string fullPath = std::string(loader.BasePathGet().CStr()) + texPath;
                mesh->m_Material.m_Texture = TextureManager::GetInstance().Load(fullPath.c_str());
                mesh->m_DiffuseTexture = mesh->m_Material.m_Texture;
            }

            // Read 4 color u32 + 1 float specular (matches LoadMesh material loop)
            if (child.m_ReadPos + 20 <= child.DataSize()) {
                uint32_t color0 = child.Read<uint32_t>();
                uint32_t color1 = child.Read<uint32_t>();
                uint32_t color2 = child.Read<uint32_t>(); // unused
                uint32_t color3 = child.Read<uint32_t>();
                float specular  = child.Read<float>();

                color0 |= 0xFF000000; // force alpha (matches binary)

                mesh->m_Material.m_Diffuse = GetColourRGB(color0);
                mesh->m_Material.m_Ambience = GetColourRGB(color1);
                mesh->m_Material.m_SelfIllum = GetColourRGB(color3);
                mesh->m_Material.m_SpecularStrength = specular;
                mesh->m_Material.m_IsLit = false;
            }
            break; // use first material
        }
    }

    // --- Parse geometry data from children ---
    // Geometry children have no grandchildren and large rawData (index + vertex streams)
    bool hasGeometry = false;
    bool hasIndices = false;

    std::function<void(ResourceLoader&)> searchGeometry =
        [&](ResourceLoader& node) {
        if (hasGeometry && hasIndices) return;
        if (node.DataSize() > 12) {
            const uint8_t* d = node.DataPtr();
            size_t ds = node.DataSize();
            size_t consumed = 0;
            if (!hasIndices && ParseIndexStream(d, ds, *mesh, consumed)) {
                hasIndices = true;
                if (!hasGeometry && consumed < ds) {
                    if (ParseVertexStream(d + consumed, ds - consumed, *mesh))
                        hasGeometry = true;
                }
            }
            if (!hasGeometry && ParseVertexStream(d, ds, *mesh))
                hasGeometry = true;
        }
        for (size_t i = 0; i < node.ChildCount(); i++)
            searchGeometry(node.m_Children[i]);
    };

    for (size_t ci = 0; ci < loader.ChildCount(); ci++)
        searchGeometry(loader.m_Children[ci]);

    // --- Parse bone bindings from root rawData ---
    // Root rawData contains model-level data: name, skeleton (bone count + bone data),
    // mesh count, and sub-resource indices. We extract bone bindings from the skeleton.
    if (loader.DataSize() > 10) {
        loader.ResetReadPos();
        AsciiString modelName = loader.ReadString();
        mesh->m_Name = modelName.CStr();

        // Read skeleton: bone count + per-bone (name + 2×Vec3 bounds)
        if (loader.m_ReadPos + 4 <= loader.DataSize()) {
            uint32_t boneCount = loader.Read<uint32_t>();
            if (boneCount > 0 && boneCount < 256) {
                std::vector<BoneBinding> bones(boneCount);
                for (uint32_t i = 0; i < boneCount && loader.m_ReadPos + 6 <= loader.DataSize(); i++) {
                    AsciiString boneName = loader.ReadString();
                    bones[i].m_Name = boneName.CStr();
                    if (loader.m_ReadPos + 24 <= loader.DataSize()) {
                        loader.ReadBytes(&bones[i].m_BoundsMin, sizeof(Vec3));
                        loader.ReadBytes(&bones[i].m_BoundsMax, sizeof(Vec3));
                    }
                }
                mesh->SetBones(bones.data(), (int)boneCount);
            }
        }
    }

    if (!hasGeometry || !hasIndices) {
        printf("[MeshManager] '%s': INCOMPLETE — geometry=%d indices=%d\n",
               path, hasGeometry, hasIndices);
    }

    SmartPtr<Mesh> meshPtr(mesh);
    model->m_Meshes.push_back(meshPtr);
    return SmartPtr<Model>(model);
}

} // namespace Mortar
