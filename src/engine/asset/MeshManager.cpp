#include "asset/MeshManager.h"
#include "asset/Mesh.h"
#include "asset/Geometry.h"
#include "asset/IStreamTypes.h"
#include "asset/TextureManager.h"
#include "asset/SharedEffectProperties.h"
#include "asset/Effect.h"
#include "util/Immutable.h"
#include "util/PathFunctions.h"
#include "util/Endian.h"
#include "render/Renderer.h"
#include "debug/Logger.h"
#include <cstring>
#include <string>
#include <map>
#include <vector>

#if defined(FN_BLOCK_PRELOAD)
#include "resource/ResBlock.h"
#include <set>
#endif

namespace Mortar {

MeshManager* MeshManager::s_instance = nullptr;

// v1.6.1 MeshManager ctor @0x002368cc: zero-initialises m_Models (List ctor does this),
// sets s_instance. Port specific: s_instance assignment has no binary counterpart
// (binary uses a fixed GOT slot @ 0x002d9a28).
MeshManager::MeshManager() {
    s_instance = this;
}

// v1.6.1 MeshManager dtor (D1) @0x002368b8: calls Destroy (-> ReleaseAll -> List::Clear),
// then calls List::Destroy @0x00236c5c to tear down the FreeList if owned.
// For the singleton path m_pFreeList==0, so List::Destroy's FreeList branch is a no-op;
// List<T>::~List() calls Destroy() which handles both paths.
// Port specific: s_instance=nullptr below has no binary counterpart.
MeshManager::~MeshManager() {
    Destroy();
    if (s_instance == this) s_instance = nullptr;
}

void MeshManager::Initialise(int /*capacity*/) {
    // No-op: binary list grows dynamically via operator new per node; no pre-allocation.
}

// v1.6.1 MeshManager::ReleaseAll @0x0023689c -- GOT-thunk tail-call to
// List<SmartPtr<Model>>::Clear @0x00236be0.
// List::Clear gates on m_Active==1, walks the singly-linked node chain, calls
// SmartPtr<Model>::~SmartPtr (refcount drop) per node, operator delete(node) per node,
// then zeros m_Count/m_pHead/m_pTail/m_Active.
void MeshManager::ReleaseAll() {
    m_Models.Clear();
}

// v1.6.1 MeshManager::Load @0x00236874
// Binary mangled: _ZN6Mortar11MeshManager4LoadERKNS_11AsciiStringE -- takes AsciiString const&.
// DIFFERS: port caches in m_Models manually; binary caches in ResourceLoader.
//   v1.6.1 LoadMeshInternal @0x00238644 does NOT touch m_Models (registers loaders +
//   calls ResourceLoader::Load<Model>). The port's Find+Add here is a port invention.
Mortar::SmartPtr<Model> MeshManager::Load(const AsciiString& path) {
    Mortar::List<Mortar::SmartPtr<Model>>::Node* node = m_Models.Head();
    while (node) {
        if (node->value.IsValid() && node->value->m_name == path) {
            return node->value;
        }
        node = node->next;
    }

    Mortar::SmartPtr<Model> model = LoadMeshInternal(path);
    if (model.IsValid()) {
        m_Models.Add(model);
    }
    return model;
}

// ============================================================
// PSP stream and mesh loader free functions (anonymous namespace).
// v1.6.1 addresses:
//   LoadVertexStreamPSP @0x001a7b0c
//   LoadIndexStreamPSP  @0x001a799c
//   LoadModel           @0x00238790
//   LoadMesh            @0x0023890c
// ============================================================

namespace {

static int FmtSize(int fmt) {
    switch (fmt) {
        case 0: return 0;
        case 1: return 1;
        case 2: return 2;
        default: return 4;
    }
}

// LoadVertexStreamPSP @0x001a7b0c
// Parses PSP vertex declaration bitfield + vertex data from rl into an IVertexStream.
// The caller (LoadMesh) has positioned rl's read cursor past the index data so this
// function reads the vertex stream from the current cursor position using DataPtr+ReadCursor.
SmartPtr<IVertexStream> LoadVertexStreamPSP(ResourceLoader& rl)
{
    const uint8_t* data = rl.DataPtr() + rl.m_ReadCursor;
    size_t dataSize = rl.DataSize() - (size_t)rl.m_ReadCursor;

    if (dataSize < 9) return SmartPtr<IVertexStream>();
    size_t pos = 0;
    uint8_t skipCount = data[pos++];
    if (skipCount > 16) return SmartPtr<IVertexStream>();
    pos += skipCount * 4;
    if (pos + 8 > dataSize) return SmartPtr<IVertexStream>();

    uint32_t vertDecl = 0;
    memcpy(&vertDecl, data + pos, 4); pos += 4;
    uint32_t vertCount = 0;
    memcpy(&vertCount, data + pos, 4); pos += 4;
#if defined(FN_BIG_ENDIAN)
    // Port specific: on-disk fields are little-endian (still the binary's format);
    // byteswap after the native load on big-endian targets (Wii).
    vertDecl  = Endian::fnByteSwap32(vertDecl);
    vertCount = Endian::fnByteSwap32(vertCount);
#endif
    if (vertCount == 0 || vertCount > 100000) return SmartPtr<IVertexStream>();

    // PSP vertex declaration layout. Stride is computed per binary
    // LegacyPSPVertexDecl::Stride @ 0x001a741c:
    //   stride = (normalFmt + colorFmt + field13 + field12) * 3
    //          + posFmt * (morphCount + 1)
    //          + texFmt * 2
    //          + weightFmt
    // For bomb/fruit decl=0x120001ff: texFmt=3, weightFmt=7, colorFmt=3,
    // normalFmt=3, posFmt=0, morphCount=0 -> stride = 8+4+12+12+0 = 36.
    //
    // IMPORTANT -- the stride formula's "weight" / "color" naming does
    // not match what the binary actually samples from each slot. User-
    // confirmed via side-by-side: the 4 bytes at offset 8 (the "weight"
    // slot per the formula) are read by glColorPointer as 4-byte RGBA
    // vertex colour. The 12 bytes at offset 12 (the "color" slot per
    // the formula) hold the surface normal direction -- unused by GL
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

    // tex: FmtSize(texFmt) * 2 -- 2-component UV.
    int texBytes = FmtSize(texFmt) * 2;
    layout.texOffset = offset; layout.texSize = texBytes; offset += texBytes;

    // "weight" slot per the binary's stride math. In practice the binary
    // reads these bytes as a 4-byte RGBA vertex colour (fmt=3 -> size=4,
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

    // "color" slot per the binary's stride math -- allocated 12 bytes
    // for fmt=3. Data-wise this holds the surface normal direction; unused
    // by GL when lighting is off.
    int colorSlotBytes = FmtSize(colorFmt) * 3;
    // Stride reserves these bytes but no client array binds them.
    offset += colorSlotBytes;

    // "normal" slot per the formula -- 12 bytes for fmt=3. Data-wise this
    // holds the 3D position (since posFmt=0 below has no dedicated slot).
    int normalBytes = FmtSize(normalFmt) * 3;
    layout.normalOffset = offset; layout.normalSize = normalBytes; offset += normalBytes;

    // Dedicated pos slot per the formula -- 0 bytes when posFmt=0.
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
    if (layout.totalStride == 0) return SmartPtr<IVertexStream>();

    size_t vertDataSize = (size_t)vertCount * layout.totalStride;
    if (pos + vertDataSize > dataSize) return SmartPtr<IVertexStream>();

#if defined(FN_BIG_ENDIAN)
    // Port specific: the on-disk vertex stream is little-endian (binary format
    // unchanged); the Wii GL-on-GX shim (gl_funcsWii.cpp EmitVertex) reads the
    // uploaded buffer's tex/normal/pos slots as native float and the colour
    // slot as 4 raw bytes (endian-neutral). Byteswap only the float slots
    // in-place per vertex before upload; the packed RGBA colour slot is left
    // untouched (byte order doesn't matter for a byte array).
    std::vector<unsigned char> swappedVerts(data + pos, data + pos + vertDataSize);
    for (uint32_t vi = 0; vi < vertCount; ++vi) {
        unsigned char* v = swappedVerts.data() + (size_t)vi * layout.totalStride;
        int floatOffsets[3]   = { layout.texOffset,  layout.normalOffset, layout.posOffset };
        int floatByteSizes[3] = { layout.texSize,    layout.normalSize,   layout.posSize   };
        for (int slot = 0; slot < 3; ++slot) {
            for (int b = 0; b + 4 <= floatByteSizes[slot]; b += 4) {
                float f;
                memcpy(&f, v + floatOffsets[slot] + b, 4);
                f = Endian::fnByteSwapFloat(f);
                memcpy(v + floatOffsets[slot] + b, &f, 4);
            }
        }
    }
    const void* uploadData = swappedVerts.data();
#else
    const void* uploadData = data + pos;
#endif

    IVertexStream* vs = new IVertexStream();
    glGenBuffers(1, &vs->m_Vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vs->m_Vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vertDataSize, uploadData, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    // Port specific: this raw GL_ARRAY_BUFFER churn invalidates the
    // Renderer's ring-VBO binding shadow (same in LoadIndexStreamPSP below).
    if (Renderer* r = Renderer::GetInstance()) r->InvalidateStateCache();
    vs->m_VertCount = (int)vertCount;
    vs->m_Layout = layout;

    // Advance the loader cursor past the vertex data.
    rl.m_ReadCursor += (int32_t)(pos + vertDataSize);

    return SmartPtr<IVertexStream>(vs);
}

// LoadIndexStreamPSP @0x001a799c
// Parses index stream header + index data from rl's current cursor position.
// Returns an IIndexStream; rl cursor is advanced past the consumed bytes.
SmartPtr<IIndexStream> LoadIndexStreamPSP(ResourceLoader& rl)
{
    const uint8_t* data = rl.DataPtr() + rl.m_ReadCursor;
    size_t dataSize = rl.DataSize() - (size_t)rl.m_ReadCursor;

    if (dataSize < 5) return SmartPtr<IIndexStream>();
    size_t pos = 0;
    uint8_t idxFlags = data[pos++];
    // Hi nibble -> Mortar::PrimType -> GL enum (via Geometry::
    // _NativePrimitiveType at 0x001a3ec8). Confirmed empirically against
    // the WebGL model gallery -- a "native TRIANGLE_STRIP" render produces
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
    // we chased -- strip rendering of a triangle-list index buffer.
    GLenum primType;
    switch (idxFlags & 0xF0) {
        case 0x20: primType = GL_TRIANGLES;      break;
        case 0x40: primType = GL_TRIANGLE_STRIP; break;
        default:   primType = GL_TRIANGLES;      break;
    }
    // Low nibble = PSP GE_INDEX_TYPE: 0 none / 1 uint16 / 2 uint32.
    // Binary LoadIndexStreamPSP (0x001a799c) branches on `(nibble - 1)`;
    // nibble==1 -> 2-byte indices, nibble==2 -> 4-byte indices. Every mesh
    // shipped in FruitNinja's Bada asset dump uses nibble=1 (uint16), so
    // `idxCount * 2` below is correct for this title. glDrawElements in
    // Geometry::Render @ 0x001a3ec8 also hardcodes GL_UNSIGNED_SHORT --
    // the uint32 path is never exercised. TODO: wire nibble==2 if a
    // future asset dump needs it.
    if (pos + 4 > dataSize) return SmartPtr<IIndexStream>();
    uint32_t idxCount = 0;
    memcpy(&idxCount, data + pos, 4); pos += 4;
#if defined(FN_BIG_ENDIAN)
    // Port specific: on-disk field is little-endian; byteswap after the native
    // load on big-endian targets (Wii).
    idxCount = Endian::fnByteSwap32(idxCount);
#endif
    if (idxCount == 0 || idxCount > 100000) return SmartPtr<IIndexStream>();
    size_t idxDataSize = (size_t)idxCount * 2;
    if (pos + idxDataSize > dataSize) return SmartPtr<IIndexStream>();

#if defined(FN_BIG_ENDIAN)
    // Port specific: on-disk indices are little-endian uint16; the Wii
    // GL-on-GX shim (gl_funcsWii.cpp glDrawElements) reads the uploaded
    // buffer as native uint16_t, so byteswap each index before upload.
    // FN_READ_ARRAY (Endian.h) is the canonical asm-verify-facing typed-array
    // read macro -- memcpy + per-element byteswap on FN_BIG_ENDIAN. This whole
    // block only compiles on Wii; the LE arm below is untouched (bare pointer,
    // no copy at all).
    std::vector<uint16_t> swappedIdx(idxCount);
    FN_READ_ARRAY(swappedIdx.data(), data + pos, uint16_t, idxCount);
    const void* uploadIdxData = swappedIdx.data();
#else
    const void* uploadIdxData = data + pos;
#endif

    IIndexStream* is = new IIndexStream();
    is->m_PrimType = primType;
    glGenBuffers(1, &is->m_Ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, is->m_Ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)idxDataSize, uploadIdxData, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    if (Renderer* r = Renderer::GetInstance()) r->InvalidateStateCache();
    is->m_IndexCount = (int)idxCount;

    // Advance the loader cursor past the index data.
    rl.m_ReadCursor += (int32_t)(pos + idxDataSize);

    return SmartPtr<IIndexStream>(is);
}

// GetDefaultEffectGroup -- returns a shared empty EffectGroup used when a geometry
// has no matching material entry.
// DIFFERS: v1.6.1 binary @ 0x00237fc4 loads a real default EffectGroup from an embedded
// Effect blob (&DefaultMeshEffect, 0x1873); port returns an empty EffectGroup stub
// because Effect::LoadEffects is not yet ported.
// v1.6.1 GetDefaultEffect @0x00237fc4
static SmartPtr<EffectGroup> GetDefaultEffectGroup() {
    static SmartPtr<EffectGroup> s_Default;
    if (!s_Default.IsValid()) {
        s_Default = new EffectGroup();
    }
    return s_Default;
}

// LoadMesh @0x0023890c
// Reads one Mesh sequentially from rl (the top-level model loader). Called via
// ResourceLoader::Load<Mesh>() dispatch from LoadModel.
// The mesh loader reads: mesh name, boneCount + BoneBindings,
//   materialCount + per-material ReadSubResourceLookup,
//   geometryCount + per-geometry ReadSubResourceLookup + matIndex.
// For each geometry child it dispatches Load<IIndexStream>() and Load<IVertexStream>()
// on the child loader to build the Geometry's GPU data.
SmartPtr<Mesh> LoadMesh(ResourceLoader& rl)
{
    Mesh* mesh = new Mesh();

    // ReadString -> mesh name
    if (rl.m_ReadCursor + 2 > (int32_t)rl.DataSize()) {
        return SmartPtr<Mesh>(mesh);
    }
    AsciiString meshName = rl.ReadString();
    mesh->m_Name = meshName;

    // Read<ulong> -> boneCount + per-bone BoneBinding data
    if (rl.m_ReadCursor + 4 > (int32_t)rl.DataSize()) {
        return SmartPtr<Mesh>(mesh);
    }
    uint32_t boneCount = rl.Read<uint32_t>();
    if (boneCount > 0 && boneCount < 256) {
        std::vector<BoneBinding> bones(boneCount);
        for (uint32_t i = 0; i < boneCount; i++) {
            if (rl.m_ReadCursor + 2 > (int32_t)rl.DataSize()) break;
            AsciiString boneName = rl.ReadString();
            bones[i].m_BoneName = boneName;
            if (rl.m_ReadCursor + 24 <= (int32_t)rl.DataSize()) {
                // Port specific: ReadArray<T> (ResourceLoader.h) is the centralised
                // FN_BIG_ENDIAN-aware typed-array reader -- byteswaps each float on
                // big-endian targets (Wii); zero-overhead memcpy on little-endian.
                // Sibling fix to the bind-pose/TRS swap in ResourceLoader::ReadSkeleton.
                rl.ReadArray<float>(&bones[i].m_Bounds.min.x, 3);
                rl.ReadArray<float>(&bones[i].m_Bounds.max.x, 3);
            }
        }
        mesh->SetBones(bones.data(), (unsigned long)boneCount);
    }

    // Read<ulong> -> materialCount + per-material sub-resource
    if (rl.m_ReadCursor + 4 > (int32_t)rl.DataSize()) {
        return SmartPtr<Mesh>(mesh);
    }
    uint32_t matCount = rl.Read<uint32_t>();

    // Standard 9-def material property set for all LoadMesh materials.
    // v1.6.1 LoadMesh @0x0023890c: these 9 defs are built per-material and passed
    // to GetPropertiesGroup<9> to create or reuse a SharedEffectProperties entry.
    // Type values: Float=1, Bool=2, Vec3=5, Texture2D=7.
    // UVWOffset count=3 (float-component count per binary InitPropertyList spec).
    // Vec3 props (Ambience/Diffuse/SelfIllum/Specular) count=1 each.
    static const EffectPropertyDefinition s_MatDefs[9] = {
        { Immutable("DiffuseMap"),       7u, 1u },  // Type_Texture2D
        { Immutable("UVWOffset"),        5u, 3u },  // Type_Vec3, count=3 (float-component count)
        { Immutable("Alpha"),            1u, 1u },  // Type_Float
        { Immutable("Ambience"),         5u, 1u },  // Type_Vec3
        { Immutable("Diffuse"),          5u, 1u },  // Type_Vec3
        { Immutable("SelfIllum"),        5u, 1u },  // Type_Vec3
        { Immutable("Specular"),         5u, 1u },  // Type_Vec3
        { Immutable("SpecularStrength"), 1u, 1u },  // Type_Float
        { Immutable("IsLit"),            2u, 1u },  // Type_Bool
    };

    // Per-material data collected during the material loop.
    std::vector<SmartPtr<SharedEffectProperties> > matSharedProps;
    std::vector<Mortar::SmartPtr<Mortar::Texture> > matTextures;
    matSharedProps.reserve(matCount);
    matTextures.reserve(matCount);

    for (uint32_t i = 0; i < matCount; i++) {
        // ReadSubResourceLookup -> material child (1-based index into rl.m_Children)
        ResourceLoader* matChild = rl.ReadSubResourceLookup();
        if (!matChild) {
            matTextures.push_back(Mortar::SmartPtr<Mortar::Texture>());
            matSharedProps.push_back(SmartPtr<SharedEffectProperties>());
            continue;
        }

        matChild->ResetReadPos();

        // Read material name (Material_Old @0x0023c750: AsciiString m_Name@0)
        AsciiString matName = matChild->ReadString();

        // ReadSubResourceLookup -> texture grandchild
        Mortar::SmartPtr<Mortar::Texture> loadedTexture;
        AsciiString texName;
        ResourceLoader* texChild = matChild->ReadSubResourceLookup();
        if (texChild) {
            texChild->ResetReadPos();
            texName = texChild->ReadString();        // e.g. "Map #1" (slot name)
            AsciiString texRelPath = texChild->ReadString();  // e.g. "textures\fruit_atlas.tex"

            std::string texPath = texRelPath.CStr();
            for (size_t j = 0; j < texPath.size(); j++)
                if (texPath[j] == '\\') texPath[j] = '/';

            // PathGetParent now returns NO trailing slash (faithful binary behavior).
            // Use PathConcatenate to join correctly regardless of whether basePath is empty.
            AsciiString fullPathStr = PathConcatenate(rl.BasePathGet(), AsciiString(texPath.c_str()));
            loadedTexture = TextureManager::GetInstance().Load(fullPathStr.CStr());
        }

        // Read 4 color u32 + float specular from Material_Old stream.
        // Material_Old layout @0x0023c750: colors[4] (u32 each), specular (float).
        // Binary forces alpha on col0: col0 |= 0xff000000.
        uint32_t col0 = 0, col1 = 0, col2 = 0, col3 = 0;
        float specular = 0.0f;
        if (matChild->m_ReadCursor + 20 <= (int32_t)matChild->DataSize()) {
            col0     = matChild->Read<uint32_t>();
            col1     = matChild->Read<uint32_t>();
            col2     = matChild->Read<uint32_t>();
            col3     = matChild->Read<uint32_t>();
            specular = matChild->Read<float>();
        }
        (void)col3; // col3 not used in binary's LoadMesh path

        // ReadSubResourceLookup -> additional sub-resource (texture anim or similar;
        // ignored by the port as it was in the previous implementation).
        matChild->ReadSubResourceLookup();

        // Build SharedEffectProperties for this material via GetPropertiesGroup<9>.
        // v1.6.1 LoadMesh @0x0023890c: calls Mesh::GetPropertiesGroup(matName, defs, defs+9).
        SmartPtr<SharedEffectProperties>* propPtr =
            mesh->GetPropertiesGroup(matName, s_MatDefs, s_MatDefs + 9);
        SmartPtr<SharedEffectProperties> props;
        if (propPtr) props = *propPtr;

        if (props.IsValid()) {
            EffectPropertyList& list = props->GetList();
            // SetValue<bool> IsLit = false (v1.6.1 LoadMesh: all meshes IsLit=false)
            list.SetValue<bool>("IsLit", false);
            // Binary forces alpha on col0 before GetColourRGB: col0 |= 0xff000000.
            col0 |= 0xff000000u;
            list.SetValue<_Vector3<float>>("Ambience",  GetColourRGB(col0));
            list.SetValue<_Vector3<float>>("Diffuse",   GetColourRGB(col1));
            list.SetValue<_Vector3<float>>("Specular",  GetColourRGB(col2));
            list.SetValue<float>("SpecularStrength", specular);
            // DiffuseMap: set texture handle if available.
            if (loadedTexture.IsValid()) {
                EffectProperty* dmProp = list.GetProperty("DiffuseMap");
                if (dmProp) {
                    EffectTexture2D tex2d;
                    tex2d.id = loadedTexture->GetTexId();
                    dmProp->SetValue(tex2d, 0);
                }
            }
            // AddTextureMap if texture name is non-empty (v1.6.1 LoadMesh @0x0023890c).
            // Binary: if (texName.CStr()[0] != 0) info->AddTextureMap(texName, "DiffuseMap")
            // The binary calls AddTextureMap on the SharedPropsInfo node (not the SmartPtr).
            // Access via mesh->m_GroupsByName (public field).
            if (texName.CStr()[0] != '\0') {
                std::map<AsciiString, SharedPropsInfo>::iterator it =
                    mesh->m_GroupsByName.find(matName);
                if (it != mesh->m_GroupsByName.end()) {
                    it->second.AddTextureMap(texName, AsciiString("DiffuseMap"));
                }
            }
        }

        matTextures.push_back(loadedTexture);
        matSharedProps.push_back(props);
    }

    // Read<ulong> -> geometryCount + per-geometry sub-resource + matIndex
    if (rl.m_ReadCursor + 4 > (int32_t)rl.DataSize()) {
        return SmartPtr<Mesh>(mesh);
    }
    uint32_t geomCount = rl.Read<uint32_t>();

    for (uint32_t i = 0; i < geomCount; i++) {
        // ReadSubResourceLookup -> geometry child (rawData = index+vertex streams)
        ResourceLoader* geomChild = rl.ReadSubResourceLookup();

        // Read<u16> matIndex -- from geomChild (the geometry sub-resource loader).
        // Binary @0x2390bc: Read<u16> operand = geomChild, advancing geomChild's cursor.
        // v1.6.1 LoadMesh @0x0023890c confirmed: matIndex comes from geomChild, NOT parent rl.
        uint16_t matIndex = 0;
        if (geomChild && geomChild->m_ReadCursor + 2 <= (int32_t)geomChild->DataSize()) {
            matIndex = geomChild->Read<uint16_t>();
        }

        // Get EffectGroup for this geometry's material.
        // v1.6.1 LoadMesh @0x0023890c: effectGroup = (matIdx < groups.size() && groups[matIdx])
        //   ? groups[matIdx] : GetDefaultEffect().
        // Port: groups are stored via GetPropertiesGroup; EffectGroup not yet built per-material
        // (requires Effect::LoadEffects which is not yet ported). Use default for all.
        // DIFFERS: binary builds per-material EffectGroup via Effect::LoadEffects; port uses a
        // shared empty EffectGroup stub. v1.6.1 LoadMesh @0x0023890c
        SmartPtr<EffectGroup> effectGroup = GetDefaultEffectGroup();

        // Get SharedEffectProperties for this geometry's material.
        SmartPtr<SharedEffectProperties> sharedProps;
        if (matIndex < (uint16_t)matSharedProps.size() && matSharedProps[matIndex].IsValid()) {
            sharedProps = matSharedProps[matIndex];
        } else {
            sharedProps = mesh->m_OwnGroup;
        }

        // Load IIndexStream then IVertexStream from the geometry child.
        // v1.6.1 LoadMesh @0x0023890c: calls Load<IIndexStream>() then Load<IVertexStream>().
        SmartPtr<IIndexStream> ib;
        SmartPtr<IVertexStream> vb;
        if (geomChild) {
            ib = geomChild->Load<IIndexStream>();
            vb = geomChild->Load<IVertexStream>();
        }

        // Construct GeometryBinding and wire it with the streams and effect group.
        // v1.6.1 LoadMesh @0x0023890c: new GeometryBinding; EffectGroupSet; IndexStreamSet; VertexStreamAdd.
        SmartPtr<GeometryBinding> binding(new GeometryBinding());
        binding->EffectGroupSet(effectGroup);
        if (ib.IsValid()) {
            binding->IndexStreamSet(ib, std::string());
        }
        if (vb.IsValid()) {
            binding->VertexStreamAdd(vb);
        }

        // Construct Geometry with binding + sharedProps.
        // v1.6.1 LoadMesh @0x0023890c: new Geometry(binding, sharedProps); SetActiveEffect(0).
        Mortar::SmartPtr<Mortar::Geometry> g(new Mortar::Geometry(binding, sharedProps));
        g->SetActiveEffect(0);
        g->m_MaterialIndex = (int)matIndex;

        // Assign diffuse texture from material index (port render path).
        // DIFFERS: structural -- binary renders via EffectProperty "DiffuseMap" in the PassBinding chain
        // (v1.6.1 Geometry::Render @0x00264468); port reads m_DiffuseTex directly (same GL result,
        // both are fixed-function GLES1.x -- NOT a GLES2 shader path).
        if (matIndex < (uint16_t)matTextures.size()) {
            g->m_DiffuseTex = matTextures[matIndex];
        }

        // Copy GL handles from streams into Geometry's port-specific fields.
        // This preserves the existing draw path (Geometry::Render reads m_Vbo/m_Ibo/m_Layout).
        // The GeometryBinding above holds SmartPtrs to the stream objects; their dtors will
        // call glDeleteBuffers when released unless we zero their handles after copying.
        if (ib.IsValid()) {
            g->m_Ibo        = ib->m_Ibo;
            g->m_IndexCount = ib->m_IndexCount;
            g->m_PrimType   = ib->m_PrimType;
            ib->m_Ibo = 0;  // transfer ownership: prevent double-free on stream dtor
        }
        if (vb.IsValid()) {
            g->m_Vbo       = vb->m_Vbo;
            g->m_VertCount  = vb->m_VertCount;
            g->m_Layout    = vb->m_Layout;
            vb->m_Vbo = 0;  // transfer ownership: prevent double-free on stream dtor
        }

        if (g->m_Vbo || g->m_Ibo) {
            mesh->AddGeometry(g);
        } else {
            LOG_WARN("MeshManager", "mesh '%s' geom[%u]: no GPU data", mesh->m_Name.c_str(), i);
        }
    }

    if (mesh->m_Geometries.empty()) {
        LOG_WARN("MeshManager", "mesh '%s': no geometries loaded", mesh->m_Name.c_str());
    }

    return SmartPtr<Mesh>(mesh);
}

// LoadModel @0x00238790
// Reads a Model from rl. Reads model name, skeleton, meshCount, then dispatches
// Load<Mesh>() for each mesh (sequential reads on the same rl).
SmartPtr<Model> LoadModel(ResourceLoader& rl)
{
    if (rl.DataSize() == 0 && rl.ChildCount() == 0) {
        return SmartPtr<Model>();
    }

    Model* model = new Model();

    rl.ResetReadPos();

    // ReadString -> model name (from stream; not used as m_name in port convention)
    AsciiString modelName = rl.ReadString();
    // m_name set by MeshManager::Load callers (the path string); modelName is the
    // binary-embedded name which callers don't use. Set from the loader's base path
    // as a fallback; MeshManager::LoadMeshInternal callers set it to the full path.
    // port convention: m_name = full load path (set in MeshManager::Load via m_name = apath).
    // Here we store the model name from the stream for reference; Load() overwrites it.
    model->m_name = modelName;

    // Read<Skeleton>: parse skeleton and bind to all meshes via UpdateBoneLinks
    rl.ReadSkeleton(model->m_skeleton);

    // meshCount: number of Mesh sub-resources that follow
    if (rl.m_ReadCursor + 4 > (int32_t)rl.DataSize()) {
        LOG_ERROR("MeshManager", "LoadModel: truncated before meshCount");
        delete model;
        return SmartPtr<Model>();
    }
    uint32_t meshCount = rl.Read<uint32_t>();
    if (meshCount == 0 || meshCount > 64) {
        LOG_ERROR("MeshManager", "LoadModel: bad meshCount=%u", meshCount);
        delete model;
        return SmartPtr<Model>();
    }

    // Dispatch Load<Mesh>() for each mesh -- reads sequentially from the same rl.
    // v1.6.1 LoadModel @0x00238790: loop calls Load<Mesh>().
    for (uint32_t mi = 0; mi < meshCount; mi++) {
        SmartPtr<Mesh> mesh = rl.Load<Mesh>();
        if (mesh.IsValid()) {
            model->AddNode(mesh);
        }
    }

    if (model->m_nodes.empty()) {
        LOG_ERROR("MeshManager", "LoadModel: no meshes loaded");
        delete model;
        return SmartPtr<Model>();
    }

    // Matches Model::SwapSkeleton -> UpdateBoneLinks (0x001aaba8, 0x00193010):
    // Skeleton was parsed above; now that all meshes are loaded, bind it to each mesh.
    if (model->m_skeleton.IsValid()) {
        model->UpdateBoneLinks();
    }

    return SmartPtr<Model>(model);
}

}  // anonymous namespace

// v1.6.1 MeshManager::LoadMeshInternal @0x00238644
// Thin dispatcher: registers the four loaders, then calls ResourceLoader::Load<Model>(path).
// DIFFERS: port caches in m_Models manually (in Load above); binary caches in ResourceLoader.
// DIFFERS: model->m_name set to path in LoadModel (port) vs model name from stream (binary).
Mortar::SmartPtr<Model> MeshManager::LoadMeshInternal(const AsciiString& path) {
    // Register the four stream/mesh/model loaders.
    // v1.6.1 @0x00238644: RegisterLoader<IVertexStream>(Delegate1(&LoadVertexStreamPSP))
    ResourceLoader::RegisterLoader<IVertexStream>(
        Delegate1<SmartPtr<IVertexStream>, ResourceLoader&>::MakeFree(&LoadVertexStreamPSP));
    // v1.6.1 @0x00238644: RegisterLoader<IIndexStream>(Delegate1(&LoadIndexStreamPSP))
    ResourceLoader::RegisterLoader<IIndexStream>(
        Delegate1<SmartPtr<IIndexStream>, ResourceLoader&>::MakeFree(&LoadIndexStreamPSP));
    // v1.6.1 @0x00238644: RegisterLoader<Model>(Delegate1(&LoadModel)) @0x00238790
    ResourceLoader::RegisterLoader<Model>(
        Delegate1<SmartPtr<Model>, ResourceLoader&>::MakeFree(&LoadModel));
    // v1.6.1 @0x00238644: RegisterLoader<Mesh>(Delegate1(&LoadMesh)) @0x0023890c
    ResourceLoader::RegisterLoader<Mesh>(
        Delegate1<SmartPtr<Mesh>, ResourceLoader&>::MakeFree(&LoadMesh));

#if defined(FN_BLOCK_PRELOAD)
    // Task #36 Stage 1 -- fail-loud instrumentation (log-only; no preload yet,
    // see tmp/wii/loader-blueprint.md section 6/7). Fires once per unique
    // path so a Dolphin run's log enumerates the per-block mesh set without
    // per-frame spam. MeshManager::Load's cache check above already skips
    // this function entirely on a hit, so every call here is a real disk load.
    {
        static std::set<std::string> s_LoggedPaths;
        if (s_LoggedPaths.insert(std::string(path.CStr())).second) {
            LOG_INFO("BlockLoad", "[BlockLoad] block=%s loading %s (MESH)",
                     fn::wii::GetCurrentBlockName(), path.CStr());
        }
    }
#endif

    // Open the file and dispatch Load<Model> via the loader machinery.
    // v1.6.1 @0x00238644: return ResourceLoader::Load<Model>(path) @0x0023e80c
    ResourceLoader loader(path);

    // v1.6.1 @0x00238644: return ResourceLoader::Load<Model>(path) @0x0023e80c
    SmartPtr<Model> model = loader.Load<Model>();
    if (model.IsValid()) {
        // Port convention: m_name = full load path (used by MeshManager::Find/Load cache).
        // Binary sets m_name from stream name; port convention overrides it to the path
        // so the cache lookup in MeshManager::Load works correctly.
        // DIFFERS: binary m_name = stream-embedded model name; port m_name = load path.
        model->m_name = path;
    } else {
        LOG_ERROR("MeshManager", "failed to load '%s'", path.CStr());
    }
    return model;
}

} // namespace Mortar

namespace Mortar {

// v1.6.1 MeshManager::Destroy @0x002368a0 -- 4-byte veneer: tail-calls ReleaseAll (-> List::Clear).
void MeshManager::Destroy() {
    ReleaseAll();
}

// v1.6.1 MeshManager::Find(AsciiString const&) @0x0023695c
// Iterate node chain; compare node->value->m_name (Model::m_name) against `name`
// via AsciiString::operator==. Return first matching SmartPtr<Model>, or empty on miss.
Mortar::SmartPtr<Model> MeshManager::Find(AsciiString const& name) const {
    Mortar::List<Mortar::SmartPtr<Model>>::Node* node = m_Models.Head();
    while (node) {
        if (node->value.IsValid() && node->value->m_name == name) {
            return node->value;
        }
        node = node->next;
    }
    return Mortar::SmartPtr<Model>();
}

// v1.6.1 MeshManager::Find(SmartPtr<Model>) @0x002369c0
// Iterate node chain comparing each node->value pointer identity against `model`.
// Return first matching SmartPtr<Model>, or empty on miss.
Mortar::SmartPtr<Model> MeshManager::Find(SmartPtr<Model> const& model) const {
    Mortar::List<Mortar::SmartPtr<Model>>::Node* node = m_Models.Head();
    while (node) {
        if (node->value.Get() == model.Get()) {
            return node->value;
        }
        node = node->next;
    }
    return Mortar::SmartPtr<Model>();
}

// v1.6.1 MeshManager::InitialiseInternal @0x00238198 -- empty in the binary (bare 'bx lr').
void MeshManager::InitialiseInternal() {
}

// v1.6.1 MeshManager::Release(SmartPtr<Model>) @0x00236908
// Calls List<SmartPtr<Model>>::Remove to find the matching node by pointer identity,
// unlink it, call ~SmartPtr<Model> (refcount drop), and free the node.
void MeshManager::Release(SmartPtr<Model> model) {
    if (model.IsValid()) {
        m_Models.Remove(model);
    }
}

}  // namespace Mortar
