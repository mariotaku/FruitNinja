# Texture & Asset Loading Functions

## Texture / Asset Loading

### GPUafyTexture (0x001898d8, 42 lines)

```c
void BadaTextureData::GPUafyTexture(TextureHeader* tex, int* texIdOut) {
    if (*texIdOut != -1) UnGPUafyTexture(texIdOut);
    
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);
    *texIdOut = texId;
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
    
    GLenum glFmt, glType;
    TexFmtToGL(tex->format, &glType, &glFmt);
    int w = 1 << tex->widthLog2;
    int h = 1 << tex->heightLog2;
    
    if (tex->format >= 0x0B && tex->format <= 0x0E)
        glCompressedTexImage2D(GL_TEXTURE_2D, 0, glFmt, w, h, 0, dataSize, tex + 12);
    else
        glTexImage2D(GL_TEXTURE_2D, 0, glFmt, w, h, 0, glFmt, glType, tex + 12);
}
```

### TexFmtToGL (0x00189f78, 53 lines)

| Format | GL Internal | GL Type |
|--------|------------|---------|
| 0x00 | GL_RGB | GL_UNSIGNED_BYTE |
| 0x01 | GL_RGBA | GL_UNSIGNED_BYTE |
| 0x10 | GL_RGBA | GL_UNSIGNED_SHORT_4_4_4_4 |
| 0x11 | GL_RGB | GL_UNSIGNED_SHORT_5_6_5 |

### LoadVertexStreamPSP (0x001a7b0c, 112 lines)

| Address | Signature |
|---------|-----------|
| 0x001a7b0c | `SmartPtr<IVertexStream> LoadVertexStreamPSP(ResourceLoader&)` |

See `docs/engine/formats/models.md` for PSP vertex declaration bitfield.

### LoadIndexStreamPSP (0x001a799c, 91 lines)

| Address | Signature |
|---------|-----------|
| 0x001a799c | `SmartPtr<IIndexStream> LoadIndexStreamPSP(ResourceLoader&)` |

### GeometryBinding_Bada::PassBinding::Apply (0x001a39f8, 101 lines)

| Address | Signature |
|---------|-----------|
| 0x001a39f8 | `uint PassBinding::Apply()` — sets up glVertexPointer/NormalPointer/ColorPointer/TexCoordPointer |

---


---

## ResourceLoader — Full HBR0 Parsing Chain

### ResourceLoader struct (0x44 bytes)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | uint | m_ReadPos | Current read position in data buffer |
| +0x04 | AsciiString | m_BasePath | File path prefix |
| +0x2c | vector\<byte\> | m_Data | Raw data buffer (12 bytes) |
| +0x38 | vector\<ResourceLoader\> | m_Children | Sub-resource loaders (12 bytes) |

### ResourceLoader::ReadBytes (0x1b45bc, 18 lines)

```c
void ReadBytes(void* dest, ulong count) {
    if (count > 0) {
        void* src = &m_Data[m_ReadPos];
        memcpy(dest, src, count);
        m_ReadPos += count;
    }
}
```

Simple sequential reader — `m_ReadPos` advances through `m_Data` buffer.

### ResourceLoader::ReadString (0x1b45e0, 20 lines)

```c
AsciiString ReadString() {
    uint16_t len = Read<uint16_t>();
    AsciiString str;
    str.Resize(len);
    ReadBytes(str.GetPtr(), len);
    return str;
}
```

Strings in HBR0 are length-prefixed: `uint16_t length` + `char[length]`.

### ResourceLoader::ReadSubResourceLookup (0x1b46d0, 34 lines)

```c
ResourceLoader* ReadSubResourceLookup() {
    uint32_t index = Read<uint32_t>();  // 1-based index
    if (index > 0 && index - 1 < m_Children.size())
        return &m_Children[index - 1];
    return NULL;
}
```

Child resources are referenced by **1-based index** into the children vector.

### ResourceLoader::Initialize (0x1b4708, 50 lines)

```c
void Initialize(DataReader& reader) {
    Read<uint32_t>();  // skip type/version
    uint32_t childCount = Read<uint32_t>();
    m_Children.reserve(childCount);
    
    for (uint32_t i = 0; i < childCount; i++) {
        uint32_t childSize = Read<uint32_t>();
        vector<byte> childData = reader.ReadByteVector(childSize);
        VectorDataReader childReader(childData);
        ResourceLoader child(childReader, m_BasePath);  // recursive!
        m_Children.push_back(child);
    }
    
    uint32_t typeIdCount = Read<uint32_t>();
    for (uint32_t i = 0; i < typeIdCount; i++)
        Read<uint32_t>();  // skip type IDs
    
    uint32_t rawDataSize = Read<uint32_t>();
    if (rawDataSize > 0)
        m_Data = reader.ReadByteVector(rawDataSize);
}
```

### Loading Chain: .mmd File → Mesh

```
1. MeshManager::Load("models/fruit/apple_single.mmd")
   → LoadMeshInternal(path)
     → RegisterLoader<IVertexStream>(LoadVertexStreamPSP)
     → RegisterLoader<IIndexStream>(LoadIndexStreamPSP)
     → RegisterLoader<Model>(ModelLoaderDelegate)
     → RegisterLoader<Mesh>(MeshLoaderDelegate)
     → ResourceLoader::Load<Model>(FileDataReader(path), basePath)

2. ResourceLoader ctor(FileDataReader, basePath)
   → Initialize(reader)
     → Recursively creates child ResourceLoaders for each nested HBR0 block
     → Stores raw data in m_Data vector

3. Load<Model> dispatches to ModelLoaderDelegate
   → Which reads model structure using ResourceLoader::Read* methods
     → ReadString() for names, texture paths
     → ReadSubResourceLookup() for child references (1-based index)
     → Load<Mesh> for each mesh child
       → Mesh ctor reads material properties, texture maps
       → Load<IVertexStream> → LoadVertexStreamPSP
         → Reads PSP vertex format bitfield
         → Reads vertex count + raw vertex data
       → Load<IIndexStream> → LoadIndexStreamPSP
         → Reads prim type + index format
         → Reads index count + raw index data
```

### HBR0 Binary Layout (on disk)

```
[HBR0 header: "HBR0" + type(u16) + pad(u16) + size(u32)]
[Initialize data:]
  skip_value (u32)
  child_count (u32)
  for each child:
    child_size (u32)
    child_data (child_size bytes) → recursive HBR0
  type_id_count (u32)
  for each type_id:
    type_id (u32)
  raw_data_size (u32)
  raw_data (raw_data_size bytes)
```

For vertex streams, the raw_data contains:
```
skip_count (u8)
skip_data (skip_count × u32)
vertex_decl_bitfield (u32)  → PSP GE format
vertex_count (u32)
vertex_data (vertex_count × stride bytes)
```
