# Mortar::Mesh

<!-- Analysed: 2026-04-11T18:30 -->

Full reverse-engineering of the `Mortar::Mesh` class, nested types, vtable, and all member functions.

## Class Hierarchy

```
__ReferenceCounterData (0x0C)
  └─ ReferenceCounter (0x0C)
    └─ IModelNode (0x0C, virtual)   @ ctor 0x001b1fd8, dtor 0x001b15e8
      └─ Mesh (0x7C)                @ ctor 0x001b0e70
```

`IModelNode` adds no fields beyond the `ReferenceCounter` base — it only adds virtual methods (Draw, GetBounds, GenerateBindings, BindSkeleton, GetGeometryCount, GetGeometry).

---

## Struct Layout (0x7C = 124 bytes)

`operator_new(0x7c)` confirmed in `LoadMesh` at 0x001a7c90.

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| 0x00 | 4 | vtable* | m_vtable | Mesh vtable at 0x001ebde0 |
| 0x04 | 4 | uint32 | m_RefCount | From __ReferenceCounterData |
| 0x08 | 4 | uint32 | m_WeakRefCount | From __ReferenceCounterData |
| 0x0C | 0x28 | AsciiString | m_Name | Mesh name (from resource file) |
| 0x34 | 0x0C | vector\<BoneBinding\> | m_BoneBindings | Bone binding array |
| 0x40 | 0x0C | vector\<SmartPtr\<Geometry\>\> | m_Geometries | Geometry submeshes |
| 0x4C | 4 | SmartPtr\<SharedEffectProperties\> | m_SharedEffectProps | Base effect properties |
| 0x50 | 0x18 | map\<AsciiString, SharedPropsInfo\> | m_PropertiesGroups | Named material property groups |
| 0x68 | 4 | Skeleton* | m_Skeleton | Bound skeleton ptr (NULL if none) |
| 0x6C | 4 | EffectProperty* | m_WorldProp | "World" matrix property |
| 0x70 | 4 | EffectProperty* | m_ViewProp | "SceneCamera.View" matrix property |
| 0x74 | 4 | EffectProperty* | m_ProjectionProp | "SceneCamera.Projection" matrix property |
| 0x78 | 4 | EffectProperty* | m_WVPProp | "WorldViewProjection" matrix property |

**Destruction order** (from ~Mesh @ 0x001b0a5c): map → SmartPtr → vector\<Geometry\> → vector\<BoneBinding\> → AsciiString → ~IModelNode → operator_delete.

### Constructor (0x001b0e70)

1. `IModelNode::IModelNode(this)` — sets base vtable + refcount
2. Overwrite vtable with Mesh vtable
3. Init `m_Name` as empty AsciiString
4. Init vectors, SmartPtr, map (default constructors)
5. `m_Skeleton = NULL` (offset 0x68)
6. Create 4 `EffectPropertyDefinition` entries for matrix properties:
   - "World" (type=3/matrix, count=1)
   - "SceneCamera.View" (type=3/matrix, count=1)
   - "SceneCamera.Projection" (type=3/matrix, count=1)
   - "WorldViewProjection" (type=3/matrix, count=1)
7. If shared props already contain these properties, reuse them; otherwise create new SharedEffectProperties
8. Store EffectProperty pointers at 0x6C–0x78

---

## Vtable (0x001ebde0, 11 entries)

Vtable data at 0x001ebdd8: offset_to_top=0, typeinfo=0x001ebe0c, then function pointers.

| Index | Address | Name | Signature | Notes |
|-------|---------|------|-----------|-------|
| 0 | 0x001b0af8 | ~Mesh (deleting) | `void ~Mesh()` | Calls complete dtor + operator_delete |
| 1 | 0x001b0a5c | ~Mesh (complete) | `void ~Mesh()` | Destroys all fields, calls ~IModelNode |
| 2 | 0x0012e564 | GetRefCounter | `ReferenceCounter& GetRefCounter()` | Inherited from ReferenceCounter |
| 3 | 0x001b15e0 | GetName | `const AsciiString& GetName() const` | Returns `this + 0x0C` |
| 4 | 0x001b0c3c | Draw | `void Draw(const Matrix44& worldMatrix)` | Sets matrix uniforms, renders all geometries |
| 5 | 0x001b07f0 | GetBounds | `Bounds3D GetBounds() const` | Computes AABB from all bone world transforms |
| 6 | 0x001b0d18 | GenerateBindings | `void GenerateBindings(const AsciiString&, const AsciiString&, vector<AnimBindings::Vector::Binding>&)` | Iterates m_PropertiesGroups, finds matching TextureProps |
| 7 | 0x001b15e4 | GenerateBindings (stub) | `void GenerateBindings(...)` | 1-byte stub, no-op |
| 8 | 0x001b0948 | BindSkeleton | `void BindSkeleton(const Skeleton& skel)` | Stores skeleton ptr, resolves bone indices |
| 9 | 0x001b1678 | GetGeometryCount | `uint GetGeometryCount() const` | Returns `m_Geometries.size()` |
| 10 | 0x001b225c | GetGeometry | `SmartPtr<Geometry> GetGeometry(ulong idx) const` | Returns `m_Geometries[idx]` as SmartPtr copy |

### Draw (0x001b0c3c) — Detail

```
Draw(worldMatrix):
  boneCount = m_BoneBindings.size()
  if boneCount == 1:
    vertTransform = GetBoneVertTransform(0)
    finalWorld = vertTransform * worldMatrix
    TrySetMatrix(m_WorldProp, finalWorld)
  else:
    TrySetMatrix(m_WorldProp, worldMatrix)

  TrySetMatrix(m_ViewProp, renderer->viewMatrix)      // at renderer+0x104c
  TrySetMatrix(m_ProjectionProp, renderer->projMatrix) // at renderer+0x0804

  if m_WVPProp != NULL:
    wvp = projMatrix * viewMatrix * worldMatrix
    TrySetMatrix(m_WVPProp, wvp)

  for each geometry in m_Geometries:
    geometry->Render()
```

### BindSkeleton (0x001b0948) — Detail

```
BindSkeleton(skeleton):
  m_Skeleton = &skeleton
  for i in 0..m_BoneBindings.size():
    boneName = m_BoneBindings[i].m_Name
    m_BoneBindings[i].m_SkeletonIndex = skeleton[boneName]  // name lookup
```

### GetBounds (0x001b07f0) — Detail

Computes axis-aligned bounding box by iterating all bones, transforming their min/max bounds by the bone world transform, and taking the overall min/max.

```
GetBounds():
  min = Vec3(FLT_MAX)
  max = Vec3(-FLT_MAX)
  for i in 0..m_BoneBindings.size():
    worldXform = GetBoneWorldTransform(i)
    p1 = worldXform * m_BoneBindings[i].m_BoundsMin
    p2 = worldXform * m_BoneBindings[i].m_BoundsMax
    min = Vec3::Min(min, p1, p2)
    max = Vec3::Max(max, p1, p2)
  return Bounds3D(min, max)
```

---

## Nested Types

### BoneBinding (0x44 = 68 bytes)

Loaded from HBR0 resource. Each entry = 0x44 bytes (confirmed by LoadMesh array stride).

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| 0x00 | 0x28 | AsciiString | m_Name | Bone name string |
| 0x28 | 0x0C | Vec3\<float\> | m_BoundsMin | Local AABB minimum |
| 0x34 | 0x0C | Vec3\<float\> | m_BoundsMax | Local AABB maximum |
| 0x40 | 4 | int32 | m_SkeletonIndex | Index into Skeleton; -1 = unbound |

**Constructor** (0x001aa738): Initializes m_Name as empty AsciiString. BoundsMin/Max and SkeletonIndex are left uninitialized.

**Copy Constructor** (0x001b1c30): Copies AsciiString, Bounds3D, and SkeletonIndex.

**Destructor** (0x001aa5a0): Destroys AsciiString only.

### SharedPropsInfo

Per-material property group, stored in `m_PropertiesGroups` map keyed by material name.

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| 0x00 | 4 | SmartPtr\<SharedEffectProperties\> | m_Props | The effect properties |
| 0x04 | 0x18 | map\<AsciiString, TextureProps\> | m_TextureMaps | Named texture properties |

**Constructor** (0x001b2234): Default-inits SmartPtr + map.

**Destructor** (0x001b207c): Destroys map then SmartPtr.

**AddTextureMap** (0x001b1394): Looks up texture name in map via `operator[]`, resolves the EffectProperty from the property list by building the property name string, stores the property pointer.

### TextureProps (4 bytes)

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| 0x00 | 4 | EffectProperty* | m_Property | Texture property pointer |

**Constructor** (0x001b15d8): Sets to NULL.

---

## Bone Transform Functions

Three functions for accessing bone transforms — all follow the same pattern:
1. If `m_Skeleton != NULL` and `BoneBinding.m_SkeletonIndex >= 0`: look up transform from Skeleton
2. Otherwise: return identity matrix (from static constant)

| Function | Address | Returns | Skeleton Method |
|----------|---------|---------|-----------------|
| GetBoneVertTransform | 0x001b0688 | Matrix44 (0x40) | `Skeleton::GetVertex(index)` |
| GetBoneWorldTransform | 0x001b0700 | Matrix44 (0x40) | `Skeleton::GetWorld(index)` |
| GetBoneLocalTransform | 0x001b0778 | Matrix44 (0x40) | `Skeleton::GetLocal(index)` |

All three return via ARM struct-return (r0=retval ptr, r1=this, r2=boneIndex).

---

## Skeleton Class (0x18 = 24 bytes)

### Layout

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| 0x00 | 12 | `vector<Bone>` | m_Bones | Bone array |
| 0x0C | 4 | `Matrix44*` | m_LocalMatrices | Base of single heap allocation (N × 3 × 64 bytes) |
| 0x10 | 4 | `Matrix44*` | m_WorldMatrices | `m_LocalMatrices + N × 0x40` |
| 0x14 | 4 | `Matrix44*` | m_VertMatrices | `m_LocalMatrices + 2N × 0x40` |

Only `m_LocalMatrices` is freed in the destructor (it is the base of the single allocation).

### Key Methods

| Method | Address | Notes |
|--------|---------|-------|
| Skeleton() | 0x00193874 | Inits vector, zeroes three matrix pointers |
| ~Skeleton() | 0x00193ac8 | Frees matrix buffer (if non-null), destroys vector |
| Swap | 0x001aadf4 | Takes `vector<Bone>&`, calls BuildArrays → swap → BuildAllMatrices |
| BuildArrays | 0x001aa700 | Allocates N×3×0x40 buffer, sets three matrix pointers |
| BuildAllMatrices | 0x001aade4 | Calls BuildLocalMatrices + BuildFinalMatrices |
| BuildLocalMatrices | 0x00193064 | Converts TRS per bone (quat+vec3+mat3) → local Matrix44 |
| BuildFinalMatrices | 0x00192e0c | Computes world (parent chain) + vert (world × bindPose) matrices |
| FindIndex | 0x0019323c | Linear scan: name → bone index, returns 0xFFFFFFFF if not found |
| operator[] | 0x001b1930 | Calls FindIndex |
| GetVertex | 0x001b15d0 | `return m_VertMatrices + index * 0x40` |
| GetWorld | 0x001b15c8 | `return m_WorldMatrices + index * 0x40` |
| GetLocal | 0x001b15c0 | `return m_LocalMatrices + index * 0x40` |

### BuildFinalMatrices Logic

```
for i in 0..boneCount:
    accumulated = localMatrices[i]
    j = i
    while true:
        j = bones[j].m_ParentIndex   // +0x28; -1 = root (no parent)
        if j < 0: break
        accumulated = localMatrices[j] * accumulated  // walk up hierarchy

    worldMatrices[i] = accumulated
    vertMatrices[i]  = accumulated * bones[i].m_BindPoseMat  // +0x2C, float[16]
```

The **vert matrix** (`m_VertMatrices[i]`) is what `GetBoneVertTransform(i)` returns. It equals the bone's accumulated world transform multiplied by the bind-pose matrix stored in the file.

### Integration with Mesh::Draw (single-bone case)

For all 122 .mmd files: `boneCount = 1`, `parentIndex = -1` (root bone).

```
Mesh::Draw(worldMatrix):
    vertTransform = skeleton->GetVertex(binding.m_SkeletonIndex)
                  = worldMat[0] × bone[0].m_BindPoseMat
    finalWorld = vertTransform × worldMatrix
```

Currently port falls back to identity (`m_Skeleton == NULL`), giving `finalWorld = worldMatrix`. Correct result requires implementing the full skeleton load + bind flow.

### Skeleton::Bone Layout (0xAC = 172 bytes in memory)

Confirmed from `ReadType<Skeleton::Bone>` (0x001a7600):

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| 0x00 | 40 | AsciiString | m_Name | Bone name |
| 0x28 | 4 | long | m_ParentIndex | Parent bone index; -1 = root |
| 0x2C | 64 | float[16] | m_BindPoseMat | Bind-pose matrix (used for vert transform) |
| 0x6C | 12 | float[3] | m_LocalTranslation | Local translation |
| 0x78 | 16 | float[4] | m_LocalRotation | Local rotation quaternion |
| 0x88 | 36 | float[9] | m_LocalScale | Local scale/rotation (3×3 matrix) |

Serialized in order: ReadString (name) → Read\<long\> → Read\<float,16\> → Read\<float,3\> → Read\<float,4\> → Read\<float,9\>.

---

## Effect Property Definitions

### Mesh Constructor Properties (4 matrix properties)

Created in Mesh constructor, stored as EffectProperty pointers:

| Field | String Constant | Type | Count | Address |
|-------|----------------|------|-------|---------|
| m_WorldProp (0x6C) | "World" | 3 (Matrix44) | 1 | str @ 0x001C304F |
| m_ViewProp (0x70) | "SceneCamera.View" | 3 (Matrix44) | 1 | str @ 0x001C3055 |
| m_ProjectionProp (0x74) | "SceneCamera.Projection" | 3 (Matrix44) | 1 | str @ 0x001C3038 |
| m_WVPProp (0x78) | "WorldViewProjection" | 3 (Matrix44) | 1 | str @ 0x001C5354 |

### LoadMesh Material Properties (9 definitions)

Created in `LoadMesh` (0x001a7c90), used to build per-material SharedEffectProperties:

| # | String Constant | Type | Count | Address |
|---|----------------|------|-------|---------|
| 1 | "DiffuseMap" | 7 (Texture2D) | 1 | str @ 0x001BCD63 |
| 2 | "UVWOffset" | 5 (Vec3) | 3 | str @ 0x001C387C |
| 3 | "Alpha" | 1 (float) | 1 | str @ 0x001BA1F7 |
| 4 | "Ambience" | 5 (Vec3) | 1 | str @ 0x001C3886 |
| 5 | "Diffuse" | 5 (Vec3) | 1 | str @ 0x001C388F |
| 6 | "SelfIllum" | 5 (Vec3) | 1 | str @ 0x001C3897 |
| 7 | "Specular" | 5 (Vec3) | 1 | str @ 0x001C38A1 |
| 8 | "SpecularStrength" | 1 (float) | 1 | str @ 0x001C38AA |
| 9 | "IsLit" | 2 (bool) | 1 | str @ 0x001C38BB |

### EffectPropertyDefinition Type Enum (inferred)

| Value | Type | Size |
|-------|------|------|
| 1 | float | 4 |
| 2 | bool | 1 |
| 3 | Matrix44 | 64 |
| 5 | Vec3 | 12 |
| 7 | Texture2D (SmartPtr) | 4 |

---

## LoadMesh (0x001a7c90, anonymous namespace)

The main mesh parser, called as a ResourceLoader delegate. ~750 lines of decompiled code.

### Signature
```c
SmartPtr<Mesh> LoadMesh(ResourceLoader& loader);
```

### Flow

```
LoadMesh(loader):
  1. Read mesh name string
  2. Create Mesh(nullSharedProps, name)  // operator_new(0x7c)
  3. Wrap in SmartPtr<Mesh>

  // --- Bone Bindings ---
  4. boneCount = Read<ulong>()
  5. Allocate BoneBinding[boneCount] temp array (each 0x44 bytes)
  6. For each bone:
     - Read name (AsciiString)
     - Read boundsMin (Vec3<float>)
     - Read boundsMax (Vec3<float>)
  7. mesh->SetBones(bones, boneCount)
  8. Free temp array

  // --- Materials ---
  9. materialCount = Read<ulong>()
  10. Create vectors: sharedProps[materialCount], effectGroups[materialCount]
  11. For each material:
      a. Read material name
      b. Read sub-resource for material data:
         - Read Material_Old struct
         - Read texture filename, resolve path via BasePathGet + PathConcatenate
         - Load texture via TextureManager::GetInstance()->Load()
         - Read colour values: diffuse(RGBA), ambience(RGBA), selfIllum(RGBA)
         - Read specularStrength (float)
         - Read sub-resource for additional data
      c. Create 9 EffectPropertyDefinitions (DiffuseMap, UVWOffset, Alpha, ...)
      d. mesh->GetPropertiesGroup<9>(materialName, propertyDefs)
      e. Set material properties: IsLit=false, colours from RGB, specular, texture

  // --- Geometry Bindings ---
  12. geometryCount = Read<ulong>()
  13. For each geometry:
      a. Read sub-resource
      b. Read materialIndex (ushort)
      c. Select SharedEffectProperties from material array
      d. Select EffectGroup (or default)
      e. Load<IIndexStream>() and Load<IVertexStream>()
      f. Create GeometryBinding, add vertex/index streams, set effect group
      g. Create Geometry(binding, sharedProps)
      h. geometry->SetActiveEffect(0)
      i. mesh->AddGeometry(geometry)

  14. Cleanup and return SmartPtr<Mesh>
```

---

## LoadMeshInternal (0x001a8518, MeshManager)

Registers 4 ResourceLoader delegates before loading:

```
LoadMeshInternal(name):
  RegisterLoader<IVertexStream>(LoadVertexStreamCallback)
  RegisterLoader<IIndexStream>(LoadIndexStreamCallback)
  RegisterLoader<Model>(LoadModelCallback)
  RegisterLoader<Mesh>(LoadMeshCallback)
  return ResourceLoader::Load<Model>(name)
```

---

## Non-Virtual Member Functions

| Function | Address | Signature | Notes |
|----------|---------|-----------|-------|
| Mesh() | 0x001b0e70 | `Mesh(SmartPtr<SharedEffectProperties>&, AsciiString&)` | Main constructor |
| Mesh() | 0x001b10d8 | `Mesh(SmartPtr<SharedEffectProperties>&, AsciiString&)` | Duplicate (different compilation unit) |
| Mesh() | 0x000f84c0 | `Mesh(SmartPtr<SharedEffectProperties>&, AsciiString&)` | PLT thunk to 0x001b0e70 |
| ~Mesh() | 0x001b0b8c | `~Mesh()` | Non-deleting variant |
| SetBones | 0x001b1340 | `void SetBones(const BoneBinding*, ulong count)` | Resizes vector, copies bindings |
| AddGeometry | 0x001b0d0c | `void AddGeometry(const SmartPtr<Geometry>&)` | push_back on m_Geometries |
| GetPropertiesGroup | 0x001b0988 | `SharedPropsInfo* GetPropertiesGroup(const AsciiString&) const` | Looks up in m_PropertiesGroups map |
| GetPropertiesGroup | 0x001b1430 | `SmartPtr<SEP>* GetPropertiesGroup(const AsciiString&, const EPD*, const EPD*)` | Find-or-create with property range |
| GetPropertiesGroup\<9\> | 0x001aab94 | `const SharedPropsInfo& GetPropertiesGroup<9>(const AsciiString&, const EPD(&)[9])` | Template wrapper, calls 4-param version |

### SetBones (0x001b1340)

```
SetBones(bones, count):
  m_BoneBindings.resize(count, BoneBinding())
  for i in 0..count:
    m_BoneBindings[i] = bones[i]    // copy each 0x44-byte BoneBinding
```

### GetPropertiesGroup (0x001b1430) — Find-or-Create

```
GetPropertiesGroup(name, propDefsBegin, propDefsEnd):
  existing = GetPropertiesGroup(name)  // simple map lookup
  if existing != NULL:
    // Check if existing has ALL required properties
    while propDefsBegin < propDefsEnd:
      if !existing->props->Contains(propDefsBegin): break
      propDefsBegin += sizeof(EffectPropertyDefinition)  // 0x0C
    if propDefsBegin == propDefsEnd:
      return existing  // reuse

  // Create new SharedPropsInfo entry in map
  entry = m_PropertiesGroups[name]
  newProps = new SharedEffectProperties(propDefsBegin, propDefsEnd, baseProps)
  entry->m_Props = newProps
  return entry
```

---

## Source File Compilation Units

From `_GLOBAL__I_*` symbols:

| Symbol | Address | File |
|--------|---------|------|
| `_GLOBAL__I_Mesh.cpp` | 0x001b14a8 | Mesh.cpp (main Mesh implementation) |
| `_GLOBAL__I_Mesh_Bada.cpp` | 0x001941b4 | Mesh_Bada.cpp (Bada platform-specific) |
| `_GLOBAL__I_MeshManager_Common.cpp` | 0x00192a54 | MeshManager_Common.cpp |
| `_GLOBAL__I_MeshManager_PSP.cpp` | 0x001a8684 | MeshManager_PSP.cpp (LoadMesh, LoadMeshInternal) |

---

## Key Observations

1. **AsciiString is 0x28 (40) bytes** — consistent across Mesh::m_Name and BoneBinding::m_Name. Not a simple std::string wrapper.

2. **EffectPropertyDefinition is 0x0C (12) bytes** — consists of Immutable\<string\> name (4 bytes), type enum (4 bytes), count (4 bytes). The `GetPropertiesGroup<9>` template passes `propDefs + 0x6C` as end (9 × 0x0C = 0x6C).

3. **SharedEffectProperties is 0x20 (32) bytes** — allocated via `operator_new(0x20)`.

4. **Geometry is 0x18 (24) bytes** — allocated via `operator_new(0x18)`.

5. **GeometryBinding is 0x4C (76) bytes** — allocated via `operator_new(0x4c)`.

6. **Material colours** — stored as `ulong` (RGBA uint32). `diffuse |= 0xFF000000` forces full alpha. Converted to Vec3 via `GetColourRGB()` for effect properties.

7. **Single-bone optimization** — In `Draw()`, if there's exactly 1 bone, the bone's vertex transform is pre-multiplied into the world matrix before setting the "World" effect property. Multi-bone meshes pass the raw world matrix.

---

## GetColourRGB (0x001a74bc)

Extracts RGB float components from a packed uint32 colour:

```c
Vec3 GetColourRGB(uint32_t color) {
    float r = (float)(color & 0xFF) / 255.0f;           // byte 0 = R
    float g = (float)((color >> 8) & 0xFF) / 255.0f;    // byte 1 = G
    float b = (float)((color >> 16) & 0xFF) / 255.0f;   // byte 2 = B
    return Vec3(r, g, b);
}
```

DAT_001a74fc = 255.0f (divisor).

---

## LoadModel (0x001a8468)

`SmartPtr<Model> LoadModel(ResourceLoader& loader)`

Called as a registered delegate from `ResourceLoader::Load<Model>`. The root ResourceLoader represents the **Model**, not the Mesh.

```
LoadModel(loader):
  name = ReadString()
  model = new Model(name)                // operator_new(0x58)
  skeleton = Read<Skeleton>(loader)
  model->SwapSkeleton(skeleton)
  meshCount = Read<ulong>()
  for i in 0..meshCount:
    mesh = Load<Mesh>()                  // triggers LoadMesh callback via delegate
    model->AddNode(mesh)
  return model
```

### HBR0 Container Structure for .mmd Files

The HBR0 format does NOT have a separate magic header. The file starts directly with the `Initialize` format: `skip_u32` (often "HBR0" text), `childCount`, then children, typeIds, and rawData.

For a typical .mmd file (e.g. bomb.mmd, 10839 bytes):

```
Root ResourceLoader:
  skip_u32: "HBR0" (0x30524248)
  childCount: 2
  child[0]: 115 bytes — material child
    skip_u32: "HBR0"
    childCount: 1
    grandchild[0]: 50 bytes — texture info
      rawData (34 bytes): u16 mapName + "Map #1" + u16 texPathLen + "textures\fruit_atlas.tex"
    typeIdCount: 1
    rawData (41 bytes): material name + sub-resource index + 4×u32 colors + float specular
  child[1]: 10484 bytes — geometry streams
    skip_u32: "HBR0"
    childCount: 0
    rawData (10468 bytes): index data (pad+flags+count+indices) + vertex data (skipCount+vertDecl+vertCount+verts)
  typeIdCount: 2
  rawData (208 bytes): model name + skeleton data + mesh count + sub-resource indices
```

### Material Child rawData Format

Sequential reads from the material child:

| Read | Type | Field | Notes |
|------|------|-------|-------|
| ReadString | AsciiString | materialName | e.g. "fruit_atlas" |
| ReadSubResourceLookup | u32→child | textureChild | Index into material's children (1-based) |
| Read\<u32\> | uint32 | color0 (diffuse) | ORed with 0xFF000000 for alpha. Set as "Ambience" property |
| Read\<u32\> | uint32 | color1 (ambience) | Set as "Diffuse" property |
| Read\<u32\> | uint32 | color2 | Unused (uStack_224) |
| Read\<u32\> | uint32 | color3 (selfIllum) | Set as "SelfIllum" property |
| Read\<float\> | float | specularStrength | Set as "SpecularStrength" property |
| ReadSubResourceLookup | u32→child | unused | Additional sub-resource (ignored) |

### Texture Grandchild rawData Format

| Read | Type | Field |
|------|------|-------|
| ReadString | AsciiString | textureName | e.g. "Map #1" |
| ReadString | AsciiString | textureRelPath | e.g. "textures\fruit_atlas.tex" |

Path is resolved relative to the .mmd file's directory via `BasePathGet() + PathConcatenate()`.

### Geometry Child rawData Format

Sequential binary data containing index stream followed by vertex stream:

**Index stream** (matches LoadIndexStreamPSP 0x001a799c):
```
u8[2]  padding
u8     flags: bits[7:4]=primType (0x20=STRIP, 0x40=TRIANGLES), bits[3:0]=indexFormat
u32    indexCount
u16[]  indices (indexCount × 2 bytes)
```

**Vertex stream** (matches LoadVertexStreamPSP 0x001a7b0c):
```
u8     skipCount → skip skipCount × 4 bytes
u32    vertDecl (PSP vertex declaration bitfield)
u32    vertCount
u8[]   vertexData (vertCount × stride bytes)
```

---

## Port Implementation Notes

### Mesh class
`Mortar::Mesh` inherits from `IModelNode` (which inherits from `ReferenceCounter`), matching the original `ReferenceCounter → IModelNode → Mesh` chain. `IModelNode` is a pure virtual interface with no data fields; it provides `GetName`, `Draw`, `GetBounds`, `GenerateBindings` (stub), `BindSkeleton`, and `GetGeometryCount`. vtable[10] `GetGeometry(SmartPtr<Geometry>)` is omitted — replaced by `Mesh::GetGeometryEntry(int)` returning `const GeometryEntry*`. `MeshMaterial` struct holds parsed material properties (diffuse, ambience, selfIllum colours, specularStrength, isLit flag, texture).

Multi-geometry and multi-material are fully supported:
- `m_Geometries`: `vector<GeometryEntry>` — each entry has VBO, IBO, vertCount, indexCount, primType, VertexLayout, and materialIndex
- `m_Materials`: `vector<MeshMaterial>` — indexed by per-geometry materialIndex
- `Mesh::Draw` loops all geometries, selects `m_Materials[geom.materialIndex]` per geometry

### 3D Shader — vertex colour integration
The port replicates GL_MODULATE semantics (texture × vertex_color) via GLES2 attributes instead of the original fixed-function pipeline:

- Attribute layout: `a_pos`(0), `a_normal`(1), `a_color`(2), `a_uv`(3)
- `a_color` is RGBA8888 (colorFmt=3): `glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, ...)`
- If no vertex color in stream: `glVertexAttrib4f(2, 1,1,1,1)` (constant white — texture passes through unmodified)
- Fragment shader: `gl_FragColor = texture × v_color × v_light` (matches GL_MODULATE)
- `u_diffuse` uniform removed; material color effects deferred (Tier 4 / Effect property system)

### Sequential parsing (no delegate system)
`LoadMeshInternal` uses direct sequential parsing instead of the original `RegisterLoader<T>` + `Load<T>` delegate dispatch. Functionally equivalent for all known .mmd files. See `mesh-port-status.md` Tier 2 note.

### Skeleton (deferred — Tier 3)
All 122 .mmd files have `skeletonBoneCount=1` and `meshBoneCount=1`. `SkipSkeleton()` correctly skips the one bone's data (reads boneCount=1, skips name + 132 bytes), but the skeleton is discarded rather than stored. `m_Skeleton` stays null; `GetBoneVertTransform(0)` falls back to identity matrix. Since the single-bone optimization in `Mesh::Draw` always fires (`m_BoneBindings.size() == 1`), it computes `identity × worldMatrix = worldMatrix` — visually correct but not faithful to the original. Full implementation requires storing the skeleton and calling `BindSkeleton` after load.

`Skeleton::Bone` in-memory layout (for future reference):
- `+0x00`: AsciiString name (40 bytes)
- `+0x28`: long parentIndex
- `+0x2C`: float[16] — vertex/bind-pose transform matrix
- `+0x6C`: float[3] — local translation
- `+0x78`: float[4] — local rotation quaternion
- `+0x88`: float[9] — local scale/rotation (3×3)

### ResourceLoader
The HBR0 "header" is NOT a separate magic — the entire file is parsed by `ResourceLoader::Initialize()` which treats the first 4 bytes as a skip value. The port reads the whole file and passes it to Initialize.

---

## See Also

- [mesh-port-status.md](mesh-port-status.md) — Port vs binary comparison, tier checklist, remaining gaps
- [texture-mesh-manager.md](texture-mesh-manager.md) — MeshManager (singleton, loading pipeline)
- [rendering-detail.md](rendering-detail.md) — Model::Draw pipeline
- [rendering-pipeline.md](rendering-pipeline.md) — Effect/Geometry/PassBinding 3D rendering path
- [assets.md](assets.md) — LoadVertexStreamPSP, GPUafyTexture
- [formats/models.md](formats/models.md) — HBR0 container format (.mad/.mmd), vertex declaration bitfield
- [utility-types.md](utility-types.md) — ResourceLoader, SmartPtr, Delegate
