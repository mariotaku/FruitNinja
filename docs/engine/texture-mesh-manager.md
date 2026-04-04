# TextureManager & MeshManager

## TextureManager (singleton, 24 bytes)

Texture cache singleton. A single `std::map<ulong, WeakPtr<Texture>>` keyed by `StringHash(filename)`. Textures are loaded once and reused via weak references.

### Struct Layout

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| +0x00 | 24 | std::map\<ulong, WeakPtr\<Texture\>\> | m_textures | Texture cache keyed by StringHash |

### Singleton

`GetInstance()` at 0x00188dec uses `__cxa_guard_acquire` (Meyers' singleton with static local).

### Loading Flow

```
LoadLocalisedTexture(name)
  → snprintf localised path
  → File::Exists check
  → TextureManager::GetInstance()
  → Load(this, path)
    → StringHash(path)
    → Find(hash) in map
    → if miss: Texture::Load(path)
      → creates Texture2DFromFile_Bada(filename, 0xFFFFFFFF)
      → wraps in SmartPtr
    → Add(hash, smartPtr) to map
    → return SmartPtr<Texture>
```

### Key Functions

| Function | Address | Notes |
|----------|---------|-------|
| TextureManager() | 0x00188dbc | Constructor — inits map |
| ~TextureManager | 0x00188e6c | Destroys map |
| GetInstance | 0x00188dec | Meyers singleton |
| Load | 0x00188efc | Load texture by path, returns SmartPtr (ARM struct-return: r0=retval, r1=this, r2=filename) |
| Find | 0x00188e84 | Lookup by StringHash, returns SmartPtr |
| Add | 0x00188ee4 | Insert into map via operator[] |
| Texture::Load | 0x00189dd4 | Static factory: creates Texture2DFromFile_Bada |
| LoadLocalisedTexture | 0x0010a758 | Loads with locale prefix |
| LoadTexture | 0x00121378 | Loads with optional locale |

### Texture2DFromFile_Bada (32 bytes)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x10 | int | width | From .tex header bytes 4-5 |
| +0x14 | int | height | From .tex header bytes 6-7 |

Constructor at 0x00189c1c also checks `DisplayManager::GetTextureOverloadPrefix()` for HD texture overrides.

---

## MeshManager (singleton, 20 bytes — was 1-byte stub)

Mesh/model cache singleton. Inherits from `List<SmartPtr<Model>>`.

### Struct Layout

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| +0x00 | 4 | pointer | m_items | SmartPtr\<Model\> array |
| +0x04 | 4 | int | m_count | |
| +0x08 | 4 | int | m_capacity | |
| +0x0C | 4 | int | field_0xc | |
| +0x10 | 2 | ushort | m_flags | |
| +0x12 | 2 | ushort | field_0x12 | |

### Key Functions

| Function | Address | Notes |
|----------|---------|-------|
| MeshManager() | 0x00192a0c | Constructor — calls List::List() + clear() |
| ~MeshManager | 0x001929c4 | Calls Destroy + ~List |
| Destroy | 0x001929bc | Calls ReleaseAll() |
| MeshManager::Load | 0x001929a0 | Load model by name, returns SmartPtr (struct-return) |
| LoadMeshInternal | 0x001a8518 | Registers ResourceLoader delegates for vertex/index/model/mesh, calls ResourceLoader::Load\<Model\> |
| LoadMesh | 0x001a7c90 | Large parser (~1800 bytes): bones, materials, textures, effect properties, geometry bindings |
| LoadModel | 0x001a8468 | Reads skeleton + mesh nodes |

### LoadMesh Details

`LoadMesh` at 0x001a7c90 is a substantial function that:
1. Parses bone bindings from HBR0 container
2. Loads materials and textures (via `TextureManager::GetInstance()->Load()`)
3. Sets up 9 named effect property definitions
4. Creates geometry bindings with vertex/index streams

### ARM Struct-Return Convention

Several functions in both managers use ARM's struct-return calling convention where:
- `r0` = hidden pointer to return value (SmartPtr)
- `r1` = `this` pointer
- `r2+` = regular parameters

This is NOT `__thiscall` — Ghidra shows these as `__stdcall` with the hidden retval pointer. Affects: Load, Find, Texture::Load, LoadLocalisedTexture, LoadMeshInternal.

---

## See Also

- [Asset formats](formats/) — .tex, .mad/.mmd file format details
- [Asset functions](assets.md) — GPUafyTexture, LoadVertexStreamPSP
- [Rendering detail](rendering-detail.md) — Model::Draw pipeline
