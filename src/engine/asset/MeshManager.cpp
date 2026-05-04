#include "asset/MeshManager.h"
#include "asset/TextureManager.h"
#include <cstdio>
#include <cstring>

// Analysed: 2026-04-12T00:00

namespace Mortar {

MeshManager* MeshManager::s_instance = nullptr;

MeshManager::MeshManager() {
    s_instance = this;
}

MeshManager::~MeshManager() {
    ReleaseAll();
    if (s_instance == this) s_instance = nullptr;
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

// Matches LoadVertexStreamPSP (0x001a7b0c, 112 lines)
// Parses PSP vertex declaration bitfield + vertex data into a GeometryEntry VBO.
// Returns true on success.
static bool ParseVertexStream(const uint8_t* data, size_t dataSize, GeometryEntry& geom) {
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

    // PSP vertex declaration layout. Stride is computed per binary
    // LegacyPSPVertexDecl::Stride @ 0x001a741c:
    //   stride = (normalFmt + colorFmt + field13 + field12) * 3
    //          + posFmt * (morphCount + 1)
    //          + texFmt * 2
    //          + weightFmt
    // For bomb/fruit decl=0x120001ff: texFmt=3, weightFmt=7, colorFmt=3,
    // normalFmt=3, posFmt=0, morphCount=0 → stride = 8+4+12+12+0 = 36.
    //
    // IMPORTANT — the stride formula's "weight" / "color" naming does
    // not match what the binary actually samples from each slot. User-
    // confirmed via side-by-side: the 4 bytes at offset 8 (the "weight"
    // slot per the formula) are read by glColorPointer as 4-byte RGBA
    // vertex colour. The 12 bytes at offset 12 (the "color" slot per
    // the formula) hold the surface normal direction — unused by GL
    // when IsLit=false (the common case). The 12 bytes at offset 24
    // (the "normal" slot per the formula) hold the actual 3D position,
    // which is what glVertexPointer binds when posFmt=0.
    //
    // So the data-stream semantics are:
    //   [0..7]   tex UV           (2 floats)
    //   [8..11]  RGBA vertex colour (4 bytes, read by glColorPointer)
    //   [12..23] surface normal   (3 floats, consumed by lighting or
    //                              ignored by unlit rendering)
    //   [24..35] 3D position      (3 floats, rebound to attribute 0
    //                              since posFmt=0 has no dedicated slot)
    //
    // The port's earlier d279483 commit mis-concluded that the offset-8
    // bytes were "weight NaN filler" and disabled the color attribute;
    // that hid the pale-blue-gray vertex tint the binary applies via
    // GL_MODULATE and over-brightened every mesh.
    int texFmt     = (vertDecl >> 0) & 0x3;
    int weightFmt  = (vertDecl >> 2) & 0x7;
    int colorFmt   = (vertDecl >> 5) & 0x3;
    int normalFmt  = (vertDecl >> 7) & 0x3;
    int posFmt     = (vertDecl >> 9) & 0x3;
    int morphCount = (vertDecl >> 13) & 0x7;

    int offset = 0;
    VertexLayout layout;
    memset(&layout, 0, sizeof(layout));

    // tex: FmtSize(texFmt) * 2 — 2-component UV.
    int texBytes = FmtSize(texFmt) * 2;
    layout.texOffset = offset; layout.texSize = texBytes; offset += texBytes;

    // "weight" slot per the binary's stride math. In practice the binary
    // reads these bytes as a 4-byte RGBA vertex colour (fmt=3 → size=4,
    // type=GL_UNSIGNED_BYTE) modulated with the texture sample. Set up
    // the color attribute here so DrawGeometry enables GL_COLOR_ARRAY
    // at this offset.
    int weightBytes = FmtSize(weightFmt);
    if (weightFmt == 7 && colorFmt == 3) {
        // Canonical PSP colour-in-weight-slot layout (4-byte RGBA).
        layout.colorOffset = offset;
        layout.colorSize   = 4;
        layout.colorFmt    = 3;   // tells DrawGeometry this is RGBA8888
    }
    offset += weightBytes;

    // "color" slot per the binary's stride math — allocated 12 bytes
    // for fmt=3. Data-wise this holds the surface normal direction; unused
    // by GL when lighting is off.
    int colorSlotBytes = FmtSize(colorFmt) * 3;
    // Stride reserves these bytes but no client array binds them.
    offset += colorSlotBytes;

    // "normal" slot per the formula — 12 bytes for fmt=3. Data-wise this
    // holds the 3D position (since posFmt=0 below has no dedicated slot).
    int normalBytes = FmtSize(normalFmt) * 3;
    layout.normalOffset = offset; layout.normalSize = normalBytes; offset += normalBytes;

    // Dedicated pos slot per the formula — 0 bytes when posFmt=0.
    int posBytes = FmtSize(posFmt) * (morphCount + 1);
    layout.posOffset = offset; layout.posSize = posBytes; offset += posBytes;

    // If no dedicated pos slot, rebind position to the normal slot and
    // mark normal as unused (it's really the 3D position in the stream).
    if (posBytes == 0 && normalBytes >= 12) {
        layout.posOffset  = layout.normalOffset;
        layout.posSize    = layout.normalSize;
        layout.normalSize = 0;
    }

    layout.totalStride = offset;
    if (layout.totalStride == 0) return false;

    size_t vertDataSize = (size_t)vertCount * layout.totalStride;
    if (pos + vertDataSize > dataSize) return false;

    glGenBuffers(1, &geom.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, geom.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vertDataSize, data + pos, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    geom.vertCount = (int)vertCount;
    geom.layout = layout;
    return true;
}

// Matches LoadIndexStreamPSP (0x001a799c, ~40 lines)
// Parses index stream header + index data into a GeometryEntry IBO.
// Returns true on success; sets consumed to bytes read from data.
static bool ParseIndexStream(const uint8_t* data, size_t dataSize,
                             GeometryEntry& geom, size_t& consumed) {
    consumed = 0;
    if (dataSize < 7) return false;
    size_t pos = 2; // skip 2-byte padding
    uint8_t idxFlags = data[pos++];
    // Hi nibble -> Mortar::PrimType -> GL enum (via Geometry::
    // _NativePrimitiveType at 0x00141ed8). Confirmed empirically against
    // the WebGL model gallery — a "native TRIANGLE_STRIP" render produces
    // visible triangle artefacts on every mesh, while "as TRIANGLES"
    // renders every fruit and bomb correctly. The binary's switch:
    //   0x20 -> PrimType 3 -> case 3 -> GL value 4 = GL_TRIANGLES
    //   0x30 -> PrimType 5 -> case 5 -> GL value 6 = GL_TRIANGLE_FAN
    //   0x40 -> PrimType 2 -> case 2 -> GL value 3 = GL_LINE_STRIP
    //   0x50 -> PrimType 1 -> case 1 -> GL value 1 = GL_LINES
    //   0x60 -> PrimType 0 -> GL_POINTS (default fall-through).
    // Every Bada .mmd ships flag=0x21, so only the TRIANGLES path is
    // actually exercised; the others are here for completeness. The old
    // port mapped 0x20 -> GL_TRIANGLE_STRIP which explained all of the
    // "mirror through fuse hole" / "triangle holes on fruit" artefacts
    // we chased — strip rendering of a triangle-list index buffer.
    switch (idxFlags & 0xF0) {
        case 0x20: geom.primType = GL_TRIANGLES;      break;
        case 0x40: geom.primType = GL_TRIANGLE_STRIP; break;
        default:   geom.primType = GL_TRIANGLES;      break;
    }
    // Low nibble = PSP GE_INDEX_TYPE: 0 none / 1 uint16 / 2 uint32.
    // Binary LoadIndexStreamPSP (0x001a799c) branches on `(nibble - 1)`;
    // nibble==1 → 2-byte indices, nibble==2 → 4-byte indices. Every mesh
    // shipped in FruitNinja's Bada asset dump uses nibble=1 (uint16), so
    // `idxCount * 2` below is correct for this title. glDrawElements in
    // Geometry::Render @ 0x001a3ec8 also hardcodes GL_UNSIGNED_SHORT —
    // the uint32 path is never exercised. TODO: wire nibble==2 if a
    // future asset dump needs it.
    if (pos + 4 > dataSize) return false;
    uint32_t idxCount = 0;
    memcpy(&idxCount, data + pos, 4); pos += 4;
    if (idxCount == 0 || idxCount > 100000) return false;
    size_t idxDataSize = (size_t)idxCount * 2;
    if (pos + idxDataSize > dataSize) return false;

    glGenBuffers(1, &geom.ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geom.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)idxDataSize, data + pos, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    geom.indexCount = (int)idxCount;
    consumed = pos + idxDataSize;
    return true;
}

// Matches LoadMeshInternal (0x001a8518) + LoadModel (0x001a8468) + LoadMesh (0x001a7c90)
//
// The root ResourceLoader maps to both the Model and Mesh scope:
//   LoadModel reads: model name, Read<Skeleton> (skipped), meshCount
//   LoadMesh reads (sequentially from same loader): mesh name, boneCount + bones,
//     materialCount + per-material ReadSubResourceLookup, geometryCount + per-geometry
//     ReadSubResourceLookup + Read<u16> matIndex
//   children[0..N]: material and geometry sub-resources referenced by 1-based index
//
// Material child rawData: matName(ReadString), texIdx(ReadSubResourceLookup),
//   4×u32 colors, float specular, unused ReadSubResourceLookup
// Texture grandchild rawData: texMapName(ReadString), texRelPath(ReadString)
// Geometry child rawData: index stream bytes || vertex stream bytes (sequential)
SmartPtr<Model> MeshManager::LoadMeshInternal(const char* path) {
    ResourceLoader loader(path);
    if (loader.DataSize() == 0 && loader.ChildCount() == 0) {
        fprintf(stderr, "MeshManager: failed to load '%s'\n", path);
        return SmartPtr<Model>();
    }

    Model* model = new Model();
    model->m_Name = path;

    loader.ResetReadPos();

    // --- LoadModel portion ---
    // ReadString → model name (stored in Model, mesh name read again below)
    AsciiString modelName = loader.ReadString();

    // Read<Skeleton> (0x001a8468): parse skeleton and bind to all meshes via UpdateBoneLinks
    loader.ReadSkeleton(model->m_Skeleton);

    // meshCount: number of Mesh sub-resources that follow
    if (loader.m_ReadPos + 4 > loader.DataSize()) {
        fprintf(stderr, "MeshManager: '%s': truncated before meshCount\n", path);
        delete model;
        return SmartPtr<Model>();
    }
    uint32_t meshCount = loader.Read<uint32_t>();
    if (meshCount == 0 || meshCount > 64) {
        fprintf(stderr, "MeshManager: '%s': bad meshCount=%u\n", path, meshCount);
        delete model;
        return SmartPtr<Model>();
    }

    // --- LoadMesh portion (one per mesh, sequential on same loader) ---
    for (uint32_t mi = 0; mi < meshCount; mi++) {
        Mesh* mesh = new Mesh();

        // ReadString → mesh name
        if (loader.m_ReadPos + 2 > loader.DataSize()) break;
        AsciiString meshName = loader.ReadString();
        mesh->m_Name = meshName.CStr();

        // Read<ulong> → boneCount + per-bone BoneBinding data
        if (loader.m_ReadPos + 4 > loader.DataSize()) break;
        uint32_t boneCount = loader.Read<uint32_t>();
        if (boneCount > 0 && boneCount < 256) {
            std::vector<BoneBinding> bones(boneCount);
            for (uint32_t i = 0; i < boneCount; i++) {
                if (loader.m_ReadPos + 2 > loader.DataSize()) break;
                AsciiString boneName = loader.ReadString();
                bones[i].m_Name = boneName.CStr();
                if (loader.m_ReadPos + 24 <= loader.DataSize()) {
                    loader.ReadBytes(&bones[i].m_BoundsMin, sizeof(Vec3));
                    loader.ReadBytes(&bones[i].m_BoundsMax, sizeof(Vec3));
                }
            }
            mesh->SetBones(bones.data(), (unsigned long)boneCount);
        }

        // Read<ulong> → materialCount + per-material sub-resource
        if (loader.m_ReadPos + 4 > loader.DataSize()) break;
        uint32_t matCount = loader.Read<uint32_t>();

        for (uint32_t i = 0; i < matCount; i++) {
            // ReadSubResourceLookup → material child (1-based index into loader.m_Children)
            ResourceLoader* matChild = loader.ReadSubResourceLookup();
            if (!matChild) continue;

            MeshMaterial mat;
            matChild->ResetReadPos();

            // Read material name (Material_Old = just AsciiString in rawData)
            AsciiString matName = matChild->ReadString();
            mat.m_Name = matName.CStr();

            // ReadSubResourceLookup → texture grandchild
            ResourceLoader* texChild = matChild->ReadSubResourceLookup();
            if (texChild) {
                texChild->ResetReadPos();
                AsciiString texName = texChild->ReadString();    // e.g. "Map #1" (unused)
                AsciiString texRelPath = texChild->ReadString();  // e.g. "textures\fruit_atlas.tex"

                std::string texPath = texRelPath.CStr();
                for (size_t j = 0; j < texPath.size(); j++)
                    if (texPath[j] == '\\') texPath[j] = '/';

                std::string fullPath = std::string(loader.BasePathGet().CStr()) + texPath;
                mat.m_Texture = TextureManager::GetInstance().Load(fullPath.c_str());
            }

            // Read 4 color u32 + float specular (matches LoadMesh material loop)
            if (matChild->m_ReadPos + 20 <= matChild->DataSize()) {
                uint32_t color0 = matChild->Read<uint32_t>(); // Ambience property
                uint32_t color1 = matChild->Read<uint32_t>(); // Diffuse property
                (void)matChild->Read<uint32_t>(); // color2 / uStack_224 — unused in binary
                uint32_t color3 = matChild->Read<uint32_t>(); // SelfIllum property
                float specular  = matChild->Read<float>();

                color0 |= 0xFF000000; // force full alpha (matches binary)

                mat.m_Diffuse = GetColourRGB(color0);
                mat.m_Ambience = GetColourRGB(color1);
                mat.m_SelfIllum = GetColourRGB(color3);
                mat.m_SpecularStrength = specular;
                mat.m_IsLit = false;
            }

            // ReadSubResourceLookup → additional sub-resource (unused in port)
            matChild->ReadSubResourceLookup();

            mesh->m_Materials.push_back(mat);
        }

        // Read<ulong> → geometryCount + per-geometry sub-resource + matIndex
        if (loader.m_ReadPos + 4 > loader.DataSize()) {
            model->m_Meshes.push_back(SmartPtr<Mesh>(mesh));
            continue;
        }
        uint32_t geomCount = loader.Read<uint32_t>();

        for (uint32_t i = 0; i < geomCount; i++) {
            // ReadSubResourceLookup → geometry child (rawData = index+vertex streams)
            ResourceLoader* geomChild = loader.ReadSubResourceLookup();

            // Read<u16> matIndex — from mesh loader (not geomChild), matches LoadMesh binary
            uint16_t matIndex = 0;
            if (loader.m_ReadPos + 2 <= loader.DataSize()) {
                matIndex = loader.Read<uint16_t>();
            }

            GeometryEntry geom;
            geom.materialIndex = (int)matIndex;

            if (geomChild) {
                const uint8_t* d = geomChild->DataPtr();
                size_t ds = geomChild->DataSize();
                size_t consumed = 0;
                if (ParseIndexStream(d, ds, geom, consumed)) {
                    if (consumed < ds) {
                        ParseVertexStream(d + consumed, ds - consumed, geom);
                    }
                }
            }

            if (geom.vbo || geom.ibo) {
                mesh->m_Geometries.push_back(geom);
            } else {
                printf("[MeshManager] '%s' mesh[%u] geom[%u]: no GPU data\n", path, mi, i);
            }
        }

        if (mesh->m_Geometries.empty()) {
            printf("[MeshManager] '%s' mesh[%u]: no geometries loaded\n", path, mi);
        }

        model->m_Meshes.push_back(SmartPtr<Mesh>(mesh));
    }

    if (model->m_Meshes.empty()) {
        fprintf(stderr, "MeshManager: '%s': no meshes loaded\n", path);
        delete model;
        return SmartPtr<Model>();
    }

    // Matches Model::SwapSkeleton → UpdateBoneLinks (0x001aaba8, 0x00193010):
    // Skeleton was parsed above; now that all meshes are loaded, bind it to each mesh.
    if (model->m_Skeleton.IsValid()) {
        model->UpdateBoneLinks();
    }

    return SmartPtr<Model>(model);
}

} // namespace Mortar
