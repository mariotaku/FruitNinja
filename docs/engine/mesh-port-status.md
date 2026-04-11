# Mesh / MeshManager Port Status

<!-- Analysed: 2026-04-12T00:00 -->

Comparison of the port's Mesh/MeshManager implementation against the original binary.

## ResourceLoader

| Aspect | Binary | Port | Status |
|--------|--------|------|--------|
| Initialize (0x001b4708) | Reads from DataReader sequentially: skip_u32, childCount, children, typeIds, rawData | Same format, reads from uint8_t buffer | ✅ Match |
| File open | FileDataReader wraps file handle | Reads entire file to buffer, passes to Initialize | ✅ Match (different mechanism, same result) |
| ReadString (0x001b45e0) | u16 len + chars | Same | ✅ Match |
| ReadSubResourceLookup (0x001b46d0) | u32 1-based index into children | Same | ✅ Match |
| Read\<T\> | Sequential from data | Same | ✅ Match |

## MeshManager

| Aspect | Binary | Port | Status |
|--------|--------|------|--------|
| Singleton | GetInstance via Meyers singleton | Static pointer, set in constructor | ✅ Functional match |
| Load cache | Checks existing before loading | Same | ✅ Match |
| LoadMeshInternal (0x001a8518) | Registers 4 delegates (IVertexStream, IIndexStream, Model, Mesh), calls Load\<Model\> | Direct parsing without delegate system | ⚠️ Simplified — same data extracted |
| LoadModel (0x001a8468) | ReadString name, Read\<Skeleton\>, meshCount, Load\<Mesh\> per mesh | Same: ReadString name, ReadSkeleton (full parse + Swap), meshCount | ✅ Match |
| LoadMesh (0x001a7c90) | Full sequential parse: name, bones, materials (with textures+colors), geometries | Same sequential parse: name, bones, matCount+ReadSubResourceLookup, geomCount+ReadSubResourceLookup+Read\<u16\> matIndex | ✅ Match |

### LoadMesh Detail

| Step | Binary | Port | Status |
|------|--------|------|--------|
| Mesh name | ReadString from mesh ResourceLoader | Read from root rawData | ✅ Match |
| Bone bindings | Read count + per-bone (name, Vec3 min, Vec3 max) + SetBones | Same from root rawData | ✅ Match |
| Skeleton | Read\<Skeleton\>: boneCount + per-bone (name, parentIdx, bindPoseMat, localTRS) + Skeleton::Swap | ResourceLoader::ReadSkeleton → Skeleton::Swap → BuildAllMatrices | ✅ Match |
| Material name | ReadSubResourceLookup → child, ReadString | Reads from material child | ✅ Match |
| Texture loading | ReadSubResourceLookup → grandchild, ReadString path, TextureManager::Load | Same from grandchild | ✅ Match |
| Material colours | Read 4× u32 + float specular, GetColourRGB, set on Effect properties | Read same fields, stored in MeshMaterial struct | ✅ Match (data extracted but used differently) |
| IsLit = false | SetValue\<bool\>(false) on "IsLit" EffectProperty | Stored as m_Material.m_IsLit = false | ✅ Match |
| Geometry parsing | ReadSubResourceLookup per geometry, Read\<u16\> matIndex, Load\<IIndexStream\> + Load\<IVertexStream\> via delegates | ReadSubResourceLookup per geometry, Read\<u16\> matIndex, ParseIndexStream + ParseVertexStream | ✅ Match |
| Per-geometry material | matIndex selects SharedEffectProperties from material array | geom.materialIndex selects from m_Materials vector (bounds-checked) | ✅ Match |
| UpdateBoneLinks | Model::SwapSkeleton → UpdateBoneLinks → BindSkeleton per mesh | Same: after mesh loop, UpdateBoneLinks → BindSkeleton per mesh | ✅ Match |

## Mesh Class

| Aspect | Binary (0x7C bytes) | Port | Status |
|--------|---------------------|------|--------|
| Inheritance | ReferenceCounter → IModelNode → Mesh | ReferenceCounter → IModelNode → Mesh | ✅ Match |
| m_Name (0x0C) | AsciiString (0x28 bytes) | std::string | ✅ Functional match |
| m_BoneBindings (0x34) | vector\<BoneBinding\> | Same; BoneBinding includes m_SkeletonIndex | ✅ Match |
| m_Geometries (0x40) | vector\<SmartPtr\<Geometry\>\> | vector\<GeometryEntry\> (VBO/IBO/layout/matIndex per entry) | ✅ Functional match — multi-geometry supported |
| m_SharedEffectProps (0x4C) | SmartPtr\<SharedEffectProperties\> | Not implemented | ❌ Missing (Effect property system deferred) |
| m_PropertiesGroups (0x50) | map\<AsciiString, SharedPropsInfo\> | Not implemented | ❌ Missing (Effect property system deferred) |
| m_Skeleton (0x68) | Skeleton* | Skeleton* m_Skeleton | ✅ Match |
| Effect properties (0x6C–0x78) | 4× EffectProperty* (World, View, Proj, WVP) | MVP computed via MatrixManager | ✅ Replaced — same math, different mechanism |
| Material data | Via Effect property system | MeshMaterial struct (diffuse/ambience/selfIllum colours, specular, isLit) | ⚠️ Simplified — data correct, not routed via Effect system |

## Mesh::Draw (0x001b0c3c)

| Step | Binary | Port | Status |
|------|--------|------|--------|
| Single-bone optimization | If 1 bone: finalWorld = GetBoneVertTransform(0) × worldMatrix | Same: if boneCount==1: finalWorld = vertMat × worldMatrix; fallback to worldMatrix if no skeleton | ✅ Match |
| Set World matrix | TrySetMatrix(m_WorldProp, matrix) | MatrixManager world stack | ✅ Functional match |
| Set View/Proj matrices | TrySetMatrix from renderer offsets | MatrixManager view/proj stacks | ✅ Functional match |
| Set WVP matrix | TrySetMatrix(m_WVPProp, proj × view × world) | Computed as MVP in MatrixManager | ✅ Functional match |
| Render geometries | Loop m_Geometries, call Geometry::Render per geometry | Loop GeometryEntries, DrawGeometry with per-entry VBO/IBO + material | ✅ Match |
| Vertex colours | glColorPointer GL_MODULATE (texture × vertex_color) | a_color attribute (RGBA8888); fragment: texture × v_color | ✅ Match — GL_MODULATE semantics |
| Material diffuse | Via SharedEffectProperties + EffectPropertyList | MeshMaterial struct passed as uniforms | ⚠️ Simplified — values correct, system different |

## Model::Draw (0x001930e0)

| Aspect | Binary | Port | Status |
|--------|--------|------|--------|
| Single mesh | Direct draw | Same | ✅ Match |
| Multi-mesh | Depth-sort back-to-front by view-space Z | Same algorithm | ✅ Match |

## Remaining Gaps

### 1. Delegate-based loading *(deferred)*
The original uses `RegisterLoader<T>` + `Load<T>` for recursive resource parsing. The port uses direct sequential parsing. Functionally equivalent — same data extracted in same order. No behavioral difference for any known .mmd file.

### 2. ~~Multi-material / multi-geometry~~ *(resolved — 2026-04-11)*
Port supports `vector<GeometryEntry>` and `vector<MeshMaterial>` with per-geometry `materialIndex` from `Read<u16>`. Full multi-material meshes are supported.

### 3. ~~Skeleton system~~ *(resolved — 2026-04-12)*
`Skeleton` class fully implemented: `BuildLocalMatrices` (TRS → Matrix44 per bone) + `BuildFinalMatrices` (world = parent chain; vert = world × bindPose). `ResourceLoader::ReadSkeleton` replaces `SkipSkeleton`. `Model::UpdateBoneLinks` binds skeleton to all meshes. `Mesh::Draw` implements the single-bone path faithfully.

### 4. Effect property system
Original routes all rendering through SharedEffectProperties/EffectPropertyList with 9 named material properties (DiffuseMap, UVWOffset, Alpha, Ambience, Diffuse, SelfIllum, Specular, SpecularStrength, IsLit) and 4 matrix properties (World, View, Proj, WVP). Port stores material data in `MeshMaterial` struct and passes uniforms directly to the 3D shader.

### 5. ~~Vertex colours~~ *(resolved — 2026-04-11)*
Port replicates GL_MODULATE via GLES2 attribute `a_color` (attribute 2). RGBA8888 vertex colours are passed normalized; fragment shader multiplies `texture × v_color`. No vertex color data: constant white via `glVertexAttrib4f`.

### 6. ~~IModelNode base class~~ *(resolved — 2026-04-12)*
`Mesh` now inherits `ReferenceCounter → IModelNode → Mesh`. `IModelNode` is a pure virtual interface providing `GetName`, `Draw`, `GetBounds`, `GenerateBindings` (stub), `BindSkeleton`, `GetGeometryCount`. vtable[10] `GetGeometry(SmartPtr<Geometry>)` is replaced by `Mesh::GetGeometryEntry(int)` returning `const GeometryEntry*`.

## TODO Checklist

Ordered by dependency — complete items higher up before those that depend on them.

### Tier 1: No dependencies (can do in any order)

- [x] **Fix vertex colour integration in 3D shader** *(2026-04-11)*
  - Added `a_color` (attribute 2) to 3D vertex/fragment shaders
  - Fragment: `texture × v_color × v_light` (GL_MODULATE semantics)
  - For RGBA8888 (colorFmt=3): `GL_UNSIGNED_BYTE` normalized per-vertex
  - No color data: constant white via `glVertexAttrib4f` so texture is unmodified
  - Removed `u_diffuse` uniform; attribute layout: pos=0, normal=1, color=2, uv=3

- [x] **Proper LoadMesh sequential parsing** *(2026-04-11)*
  - Sequential read: model name → ReadSkeleton → meshCount → per-mesh (name, bones, matCount, geomCount)
  - ReadSubResourceLookup correctly references material/geometry children by 1-based index
  - Root loader serves as both Model and Mesh context (single-pass sequential read)
  - `ResourceLoader::SkipSkeleton()` added initially; superseded by `ReadSkeleton()` in Tier 3

- [x] **Multi-geometry per mesh** *(2026-04-11)*
  - `GeometryEntry` struct: VBO, IBO, vertCount, indexCount, primType, layout, materialIndex
  - `Mesh::m_Geometries` (vector<GeometryEntry>) replaces single VBO/IBO fields
  - `Mesh::m_Materials` (vector<MeshMaterial>) replaces single m_Material
  - `Mesh::Draw` loops over all geometries, binds per-geometry material
  - `Mesh::SetDiffuseTexture` / `HasDiffuseTexture` replace direct m_DiffuseTexture access

### Tier 2: Depends on Tier 1

- [x] **Multi-material support** *(2026-04-11)*
  - `GeometryEntry.materialIndex` set from `Read<u16>` in geometry loop
  - `Mesh::m_Materials` is `vector<MeshMaterial>` indexed per-geometry
  - `Mesh::Draw` selects `m_Materials[geom.materialIndex]` with bounds-checked fallback
  - Multi-material meshes are fully supported

- [ ] **Delegate-based resource loading** *(deferred — no behavioral impact)*
  - Currently: direct sequential parsing in LoadMeshInternal
  - Original: `RegisterLoader<T>` + `Load<T>` dispatch (IVertexStream, IIndexStream, Model, Mesh)
  - Both produce identical output for all known .mmd files
  - Deferring: architectural refactor only; adds complexity without functional benefit
  - Ref: LoadMeshInternal (0x001a8518), ResourceLoader::Load (0x001acb2c)

### Tier 3: Depends on Tier 2

- [x] **Skeleton system** *(2026-04-12)*
  - `Skeleton` class: `vector<Bone>`, three `vector<Matrix44>` (local/world/vert)
  - `Skeleton::Bone`: name, parentIndex, bindPoseMat[16], localTranslation[3], localRotation[4], localScale[9]
  - `Skeleton::Swap(bones)`: builds arrays → swaps bones → BuildLocalMatrices + BuildFinalMatrices
  - `BuildLocalMatrices`: quaternion → R, transpose → Rt, mat3 → S mat44, `local = (S * Rt) * T`
  - `BuildFinalMatrices`: world = parent-chain accumulation; vert = world × bindPoseMat
  - `Skeleton::FindIndex`: linear scan, returns 0xFFFFFFFF if not found
  - `ResourceLoader::ReadSkeleton(outSkeleton)`: replaces SkipSkeleton, calls `outSkeleton.Swap(bones)`
  - `Model::m_Skeleton` (Skeleton value member), `SwapSkeleton`, `UpdateBoneLinks`
  - `Mesh::m_Skeleton` (Skeleton*), `BindSkeleton`, `GetBoneVertTransform`
  - `MeshManager`: calls `ReadSkeleton` then `UpdateBoneLinks` after mesh load

- [x] **Single-bone Draw optimization** *(2026-04-12)*
  - `if (m_BoneBindings.size() == 1)`: `finalWorld = GetBoneVertTransform(0) * worldMatrix`
  - `GetBoneVertTransform(i)` returns `m_Skeleton->GetVertex(m_BoneBindings[i].m_SkeletonIndex)`
  - Falls back to `worldMatrix` if no skeleton bound (visually equivalent for static bind-pose bones)
  - Ref: Mesh::Draw (0x001b0c3c); GetBoneVertTransform (0x001b0688)

### Tier 4: Full fidelity (optional)

- [x] **IModelNode base class** *(2026-04-12)*
  - `IModelNode : public ReferenceCounter` — pure virtual interface (no data fields)
  - Virtuals: `GetName`, `Draw`, `GetBounds`, `GenerateBindings` (stub), `BindSkeleton`, `GetGeometryCount`
  - `Mesh : public IModelNode` — all interface methods declared `override`
  - vtable[10] `GetGeometry(SmartPtr<Geometry>)` — omitted; replaced by `Mesh::GetGeometryEntry(int)` returning `const GeometryEntry*`
  - Ref: IModelNode ctor (0x001b1fd8), vtable (0x001ebde0, 11 entries)

- [ ] **Effect property system**
  - Currently: material data stored in MeshMaterial struct, passed as shader uniform
  - Goal: full SharedEffectProperties / EffectPropertyList with 9 material + 4 matrix properties
  - Depends on: multi-material, skeleton
  - Needed only if: exact rendering fidelity required beyond simple texture × diffuse
  - Ref: Mesh constructor (0x001b0e70), GetPropertiesGroup (0x001b1430), SharedPropsInfo, TextureProps

---

## Key Binary References

| Function | Address | Lines | Purpose |
|----------|---------|-------|---------|
| LoadMesh | 0x001a7c90 | 423 | Core mesh parser (anonymous namespace) |
| LoadMeshInternal | 0x001a8518 | ~50 | Register loaders + Load\<Model\> |
| LoadModel | 0x001a8468 | ~30 | Read model name, skeleton, mesh count |
| GetColourRGB | 0x001a74bc | 8 | Extract RGB floats from u32 colour |
| LoadVertexStreamPSP | 0x001a7b0c | 112 | Parse PSP vertex declaration + data |
| LoadIndexStreamPSP | 0x001a799c | ~40 | Parse index stream flags + data |
| Mesh::Draw | 0x001b0c3c | ~60 | Set matrices, render geometries |
| Mesh::SetBones | 0x001b1340 | ~15 | Resize + copy bone bindings |
| Mesh::BindSkeleton | 0x001b0948 | ~20 | Resolve bone indices from skeleton |
| Mesh::GetBounds | 0x001b07f0 | ~40 | AABB from bone world transforms |
| Model::Draw | 0x001930e0 | 79 | Single/multi-mesh depth-sorted draw |
| ResourceLoader::Initialize | 0x001b4708 | 50 | Recursive HBR0 container parsing |
