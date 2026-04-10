# TextureManager, MeshManager & AnimationManager

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

### LoadLocalisedTexture (0x0010a758)

Convenience function used by game code to load textures from the `textures/` directory:

```c
SmartPtr<Texture> LoadLocalisedTexture(const char* name) {
    // ARM struct-return: r0=retval ptr, r1=name
    char path[512];
    snprintf(path, 0x200, "textures/%s", name);

    if (!File::Exists(path))
        return SmartPtr<Texture>(NULL);

    return TextureManager::GetInstance().Load(path);
}
```

Format string at 0x001b9458: `"textures/%s"`. No locale prefix in this build despite the name.

### Key Functions

| Function | Address | Convention | Notes |
|----------|---------|------------|-------|
| TextureManager() | 0x00188dbc | __thiscall | Constructor — inits map |
| TextureManager() | 0x00188dc8 | __thiscall | Constructor variant |
| ~TextureManager | 0x00188e6c | __thiscall | Destroys map |
| ~TextureManager | 0x00188e78 | __thiscall | Variant |
| Destroy | 0x00188db8 | __thiscall | Empty no-op |
| GetInstance | 0x00188dec | __stdcall (static) | Meyers singleton |
| Load(char*) | 0x00188efc | __stdcall (ARM hidden ret) | r0=retval, r1=this, r2=filename. Find-or-load pattern |
| Find(ulong) | 0x00188e84 | __stdcall (ARM hidden ret) | r0=retval, r1=this, r2=hash |
| Find(char*) | 0x00188ec8 | __stdcall (ARM hidden ret) | Hashes name then calls Find(ulong) |
| Add(ulong, SmartPtr) | 0x00188ee4 | __thiscall | Insert into map via operator[] |
| Add(char*, SmartPtr) | 0x00188f60 | __thiscall | Hashes name then calls Add(ulong) |
| LoadIndependent | 0x00188dd4 | __stdcall | Returns null SmartPtr (stub) |
| InitialiseInternal | 0x001a73d0 | __thiscall | Empty stub |
| Texture::Load | 0x00189dd4 | __stdcall (ARM hidden ret) | Static factory: creates Texture2DFromFile_Bada |
| LoadLocalisedTexture | 0x0010a758 | __stdcall (ARM hidden ret) | Loads with locale prefix |
| LoadTexture | 0x00121378 | __stdcall | Loads with optional locale |

### Texture2DFromFile_Bada (32 bytes)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x10 | int | width | From .tex header bytes 4-5 |
| +0x14 | int | height | From .tex header bytes 6-7 |

Constructor at 0x00189c1c also checks `DisplayManager::GetTextureOverloadPrefix()` for HD texture overrides.

---

## MeshManager (20 bytes)

<!-- Analysed: 2026-04-10T14:00 -->

Mesh/model cache. Inherits from `List<SmartPtr<Model>>`. Accessed via `GetInstance()` in the binary. Port uses a static singleton initialized in GameInitialise step 8.

### Port Notes

- **Singleton**: `Mortar::MeshManager::s_instance` — set in constructor, accessed via `GetInstance()`
- **Texture loading**: MeshManager loads geometry only (HBR0 vertex/index streams). Textures are NOT embedded in .mmd files — they must be assigned externally after `Load()`.
  - Fruit: assigns `fruit_atlas.tex` (at `models/fruit/textures/`, NOT `textures/`)
  - Bomb: assigns `bomb_explode.tex` (via TextureManager)
- **MortarMesh::Draw**: calls `Renderer::GetInstance()->setup_3d_shader()` for the GL shader program. Without this, nothing renders (no `glUseProgram` active).

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

| Function | Address | Convention | Notes |
|----------|---------|------------|-------|
| MeshManager() | 0x00192a0c | __thiscall | Calls List::List() + clear() |
| MeshManager() | 0x00192a30 | __thiscall | Variant |
| ~MeshManager | 0x001929c4 | __thiscall | Calls Destroy + ~List |
| ~MeshManager | 0x001929e8 | __thiscall | Variant |
| Destroy | 0x001929bc | __thiscall | Calls ReleaseAll() |
| Load | 0x001929a0 | __stdcall (ARM hidden ret) | r0=retval, r1=this, r2=name |
| Initialise | 0x001929ac | __thiscall | Calls InitialiseInternal (empty stub) |
| ReleaseAll | 0x001929b4 | __thiscall | Calls List::clear |
| InitialiseInternal | 0x001a74b8 | __thiscall | Empty stub |
| LoadMeshInternal | 0x001a8518 | __stdcall (ARM hidden ret) | Registers ResourceLoader delegates, calls Load\<Model\> |
| LoadMesh | 0x001a7c90 | __thiscall | Large parser (~1800 bytes) |
| LoadModel | 0x001a8468 | __thiscall | Reads skeleton + mesh nodes |

### LoadMesh Details

`LoadMesh` at 0x001a7c90 is a substantial function that:
1. Parses bone bindings from HBR0 container
2. Loads materials and textures (via `TextureManager::GetInstance()->Load()`)
3. Sets up 9 named effect property definitions
4. Creates geometry bindings with vertex/index streams

---

## AnimationManager (singleton, 20 bytes — was 1-byte stub)

Animation cache singleton. Inherits from `List<Animation*>`. Same base layout as MeshManager.

### Struct Layout

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| +0x00 | 4 | pointer | m_items | Animation* array |
| +0x04 | 4 | uint | m_count | |
| +0x08 | 4 | uint | m_capacity | |
| +0x0C | 4 | uint | field_0xc | |
| +0x10 | 2 | short | m_flags | |
| +0x12 | 2 | short | field_0x12 | |

### Singleton

Constructed in `_GLOBAL__I_AnimationManager.cpp` (0x00192590) as a static variable.

### Key Functions

| Function | Address | Convention | Notes |
|----------|---------|------------|-------|
| AnimationManager() | 0x0019251c | __thiscall | Calls List\<Animation*\>::List |
| AnimationManager() | 0x00192528 | __thiscall | Variant |
| ~AnimationManager | 0x00192548 | __thiscall | Calls Destroy then ~List |
| ~AnimationManager | 0x0019256c | __thiscall | Variant |
| Destroy | 0x00192514 | __thiscall | Calls ReleaseAll |
| ReleaseAll | 0x00192510 | __thiscall | Empty stub (no-op) |
| Load | 0x00192590 | __thiscall | Calls LoadAnimInternal, creates AnimationState, wraps in SmartPtr |
| LoadAnimInternal | 0x001ad590 | __stdcall | Registers AnimationList loader, opens ResourceLoader, calls Load\<AnimationList\> |

### Load Flow

```
AnimationManager::Load(name)
  → LoadAnimInternal(name)
    → RegisterLoader<AnimationList>(callback)
    → ResourceLoader::Load<AnimationList>(name)
  → creates AnimationState wrapping AnimationList
  → returns SmartPtr<AnimationState>
```

---

## ARM Struct-Return Convention

Several functions across all three managers use ARM's struct-return calling convention:
- `r0` = hidden pointer to return value (SmartPtr)
- `r1` = `this` pointer
- `r2+` = regular parameters

Ghidra shows these as `__stdcall` with the hidden retval pointer. This is correct — NOT `__thiscall`. Affects: Load, Find, Texture::Load, LoadLocalisedTexture, LoadMeshInternal, LoadAnimInternal.

---

## See Also

- [Asset formats](formats/) — .tex, .mad/.mmd file format details
- [Asset functions](assets.md) — GPUafyTexture, LoadVertexStreamPSP
- [Rendering detail](rendering-detail.md) — Model::Draw pipeline
- [Utility types](utility-types.md) — ResourceLoader, SmartPtr, Delegate
