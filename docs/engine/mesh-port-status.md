# Mesh / MeshManager Port Status

<!-- Analysed: 2026-04-11T18:00 -->

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
| LoadModel (0x001a8468) | ReadString name, Read\<Skeleton\>, meshCount, Load\<Mesh\> per mesh | Parses root rawData for model name + bone data | ⚠️ Partial — skeleton read is approximate |
| LoadMesh (0x001a7c90) | Full sequential parse: name, bones, materials (with textures+colors), geometries | Material parsed from children; geometry via brute-force search | ⚠️ Partial — see below |

### LoadMesh Detail

| Step | Binary | Port | Status |
|------|--------|------|--------|
| Mesh name | ReadString from mesh ResourceLoader | Read from root rawData | ✅ Match |
| Bone bindings | Read count + per-bone (name, Vec3 min, Vec3 max) + SetBones | Same from root rawData | ✅ Match |
| Material name | ReadSubResourceLookup → child, ReadString | Reads from material child | ✅ Match |
| Texture loading | ReadSubResourceLookup → grandchild, ReadString path, TextureManager::Load | Same from grandchild | ✅ Match |
| Material colours | Read 4× u32 + float specular, GetColourRGB, set on Effect properties | Read same fields, stored in MeshMaterial struct | ✅ Match (data extracted but used differently) |
| IsLit = false | SetValue\<bool\>(false) on "IsLit" EffectProperty | Stored as m_Material.m_IsLit = false | ✅ Match |
| Geometry parsing | ReadSubResourceLookup per geometry, Read\<u16\> matIndex, Load\<IIndexStream\> + Load\<IVertexStream\> via delegates | Brute-force search children for index+vertex data | ⚠️ Simplified — finds same data, skips per-geometry material index |
| Per-geometry material | matIndex selects SharedEffectProperties from material array | First material applied to entire mesh | ❌ Missing — multi-material meshes not supported |

## Mesh Class

| Aspect | Binary (0x7C bytes) | Port | Status |
|--------|---------------------|------|--------|
| Inheritance | ReferenceCounter → IModelNode → Mesh | ReferenceCounter → Mesh | ⚠️ Simplified — no IModelNode |
| m_Name (0x0C) | AsciiString (0x28 bytes) | std::string | ✅ Functional match |
| m_BoneBindings (0x34) | vector\<BoneBinding\> | Same | ✅ Match |
| m_Geometries (0x40) | vector\<SmartPtr\<Geometry\>\> | VBO/IBO/Layout (single geometry) | ⚠️ Simplified — one geometry only |
| m_SharedEffectProps (0x4C) | SmartPtr\<SharedEffectProperties\> | Not implemented | ❌ Missing |
| m_PropertiesGroups (0x50) | map\<AsciiString, SharedPropsInfo\> | Not implemented | ❌ Missing |
| m_Skeleton (0x68) | Skeleton* | Not implemented | ❌ Missing |
| Effect properties (0x6C–0x78) | 4× EffectProperty* (World, View, Proj, WVP) | MVP computed via MatrixManager | ✅ Replaced — same math, different mechanism |
| Material data | Via Effect property system | MeshMaterial struct with diffuse uniform | ⚠️ Simplified |

## Mesh::Draw (0x001b0c3c)

| Step | Binary | Port | Status |
|------|--------|------|--------|
| Single-bone optimization | If 1 bone: world = boneVertTransform × worldMatrix | Not implemented (no skeleton) | ❌ Missing |
| Set World matrix | TrySetMatrix(m_WorldProp, matrix) | MatrixManager world stack | ✅ Functional match |
| Set View/Proj matrices | TrySetMatrix from renderer offsets | MatrixManager view/proj stacks | ✅ Functional match |
| Set WVP matrix | TrySetMatrix(m_WVPProp, proj × view × world) | Computed as MVP in MatrixManager | ✅ Functional match |
| Render geometries | Loop m_Geometries, call Geometry::Render per geometry | Single draw call with VBO/IBO | ⚠️ Simplified |
| Material diffuse | Via Effect property system on shader | u_diffuse uniform (Vec3) | ⚠️ New — replaces vertex color multiply |

## Model::Draw (0x001930e0)

| Aspect | Binary | Port | Status |
|--------|--------|------|--------|
| Single mesh | Direct draw | Same | ✅ Match |
| Multi-mesh | Depth-sort back-to-front by view-space Z | Same algorithm | ✅ Match |

## Remaining Gaps

### 1. Delegate-based loading
The original uses `RegisterLoader<T>` + `Load<T>` for recursive resource parsing. The port uses direct child searching. Functionally equivalent for current .mmd files but less robust.

### 2. Multi-material / multi-geometry
Original supports multiple materials and geometries per mesh via `ReadSubResourceLookup` per geometry with `Read<u16>` material index. Port uses one material and one VBO/IBO per mesh. Needed if any model has multiple sub-meshes with different materials.

### 3. Skeleton system
Original has full skeleton binding: `Mesh::BindSkeleton` (0x001b0948) resolves bone indices by name lookup, and `GetBoneVertTransform`/`GetBoneWorldTransform`/`GetBoneLocalTransform` (0x001b0688/0x001b0700/0x001b0778) return transforms from the skeleton. Port stores bone bindings but doesn't use them for transforms.

### 4. Effect property system
Original routes all rendering through SharedEffectProperties/EffectPropertyList with 9 named material properties (DiffuseMap, UVWOffset, Alpha, Ambience, Diffuse, SelfIllum, Specular, SpecularStrength, IsLit) and 4 matrix properties (World, View, Proj, WVP). Port uses direct shader uniforms (u_diffuse, u_mvp, u_model).

### 5. Vertex colours
Original uses vertex colours via `glColorPointer` in the GLES1 pipeline (GL_MODULATE: texture × vertex_color). Port's 3D shader ignores vertex colours, using `u_diffuse` uniform instead. The correct integration of vertex colours with material properties is unknown — may need the full Effect system to determine the proper blend mode per material.

### 6. IModelNode base class
Original Mesh inherits from IModelNode which provides virtual methods: GetName, Draw, GetBounds, GenerateBindings, BindSkeleton, GetGeometryCount, GetGeometry. Port inherits directly from ReferenceCounter. Not needed unless other code relies on the IModelNode interface.

## TODO Checklist

Ordered by dependency — complete items higher up before those that depend on them.

### Tier 1: No dependencies (can do in any order)

- [ ] **Fix vertex colour integration in 3D shader**
  - Currently: shader uses `u_diffuse` uniform only, vertex colours ignored
  - Goal: match original GL_MODULATE (`texture × vertex_color`), modulated by material properties
  - Needed for: correct fruit tinting, bomb body appearance
  - Ref: PassBinding::Apply (0x001a39f8), frag shader GL_MODULATE semantics

- [ ] **Proper LoadMesh sequential parsing**
  - Currently: brute-force child search for geometry, material parsed from children heuristically
  - Goal: sequential Read/ReadSubResourceLookup flow matching 0x001a7c90 exactly
  - Depends on: understanding root rawData layout (model name + skeleton + meshCount + sub-resource indices)
  - Ref: LoadMesh (0x001a7c90, 423 lines), LoadModel (0x001a8468)

- [ ] **Multi-geometry per mesh**
  - Currently: one VBO/IBO per Mesh
  - Goal: vector of geometry entries (VBO/IBO pairs), each with its own material index
  - Needed for: models with multiple sub-meshes (e.g. bomb body + fuse as separate geometries)
  - Ref: LoadMesh geometry loop, `Mesh::AddGeometry` (0x001b0d0c)

### Tier 2: Depends on Tier 1

- [ ] **Multi-material support**
  - Currently: first material applied to entire mesh
  - Goal: per-geometry material index (`Read<u16>` matIndex in geometry loop)
  - Depends on: multi-geometry, proper LoadMesh parsing
  - Ref: LoadMesh geometry loop (materialIndex selects from materials array)

- [ ] **Delegate-based resource loading**
  - Currently: direct parsing in LoadMeshInternal
  - Goal: `RegisterLoader<T>` + `Load<T>` pattern matching original
  - Depends on: proper LoadMesh sequential parsing
  - Ref: LoadMeshInternal (0x001a8518), ResourceLoader::Load (0x001acb2c)

### Tier 3: Depends on Tier 2

- [ ] **Skeleton system**
  - Currently: bone bindings stored but no skeleton transforms
  - Goal: `Read<Skeleton>`, `Mesh::BindSkeleton` (0x001b0948), bone index resolution by name, `GetBoneVertTransform`/`GetBoneWorldTransform`/`GetBoneLocalTransform`
  - Depends on: proper LoadMesh parsing (skeleton data read in LoadModel)
  - Needed for: animated models, correct bone transforms in Draw
  - Ref: Skeleton class, BindSkeleton (0x001b0948), bone transform functions (0x001b0688/0x001b0700/0x001b0778)

- [ ] **Single-bone Draw optimization**
  - Currently: world matrix passed directly
  - Goal: if boneCount==1, pre-multiply boneVertTransform into world matrix
  - Depends on: skeleton system (needs GetBoneVertTransform)
  - Ref: Mesh::Draw (0x001b0c3c) first branch

### Tier 4: Full fidelity (optional)

- [ ] **IModelNode base class**
  - Currently: Mesh inherits from ReferenceCounter directly
  - Goal: IModelNode virtual interface (GetName, Draw, GetBounds, GenerateBindings, BindSkeleton, GetGeometryCount, GetGeometry)
  - Depends on: skeleton system
  - Needed only if: other code relies on IModelNode interface polymorphism
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
