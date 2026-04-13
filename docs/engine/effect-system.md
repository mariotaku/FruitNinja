# Effect Property System

<!-- Analysed: 2026-04-12T01:15 -->

The Mortar engine's effect property system is the bridge between mesh materials/shaders and the GL pipeline. It stores named, typed values (textures, floats, bools, Vec3s, Matrix44s) in a shared, parent-chained container called `SharedEffectProperties`. Meshes look up properties by string name to set matrices, material colours, and textures before each draw.

---

## Type Enum (`EffectDataTypes::Type`)

| Value | C++ Type        | Notes |
|-------|-----------------|-------|
| 1     | `float`         | |
| 2     | `bool`          | |
| 3     | `Matrix44`      | 4×4 float |
| 5     | `_Vector3<float>` | Vec3 (RGB colours, UVW offsets) |
| 7     | `SmartPtr<Texture2D>` | Texture handle |

Types 0, 4, 6, 8, 9 are allocated in `ValueBuffer` but unused by FruitNinja meshes.

---

## Struct Layouts

### `EffectPropertyDefinition` — 0x0C bytes (12)

Immutable descriptor created on the stack to define a property slot.

```
+0x00  Immutable<string>*  name_ptr   — shared interned string; name char data at *(name_ptr + 0x0C)
+0x04  uint32              type       — Type enum (1/2/3/5/7)
+0x08  uint32              count      — element count in the value array
```

Stride is always **0x0C**. Template specialisations (`Contains<4ul>`, `GetPropertiesGroup<9ul>`) use compile-time stride * N for the end pointer:
- `Contains<4ul>`:       end = begin + 4 * 0x0C = begin + 0x30
- `GetPropertiesGroup<9ul>`: end = begin + 9 * 0x0C = begin + 0x6C

The `Immutable<basic_string>` stored at `name_ptr` has a 12-byte header (vtable + refcount + ???), so the actual C-string (`basic_string` object) is at **`*name_ptr + 0x0C`**.

### `EffectProperty` — 0x14 bytes (20)

Extends `EffectPropertyDefinition` (first 12 bytes are identical). Lives in the sorted `vector<EffectProperty>` inside `EffectPropertyList`.

```
+0x00  Immutable<string>*     name_ptr   — (inherited from EPD; name at *name_ptr + 0x0C)
+0x04  uint32                 type       — (inherited)
+0x08  uint32                 count      — (inherited)
+0x0C  EffectPropertyValues*  m_Values   — pointer to this list's shared value buffer
+0x10  ulong                  m_Offset   — index of the first slot in m_Values for this property
```

**Constructor** (0x001b685c real impl):
```cpp
EffectProperty(EPD* src, EffectPropertyValues* values, ulong offset):
    EffectPropertyDefinition(src)   // copies name_ptr, type, count
    this->m_Values = values         // stores pointer to EffectPropertyValues
    this->m_Offset = offset         // stores per-type slot offset
```

The vector is kept **sorted by name** (binary search via `std::lower_bound`).

### `EffectPropertyList` — 0x14 bytes (20)

Container of named `EffectProperty` entries plus a chained parent for inheritance.

```
+0x00  SmartPtr<SharedEffectProperties>  m_Parent  — raw pointer (4 bytes); null = no parent
+0x04  auto_ptr<EffectPropertyValues>    m_Values  — 4-byte raw pointer to this list's value buffer
+0x08  vector<EffectProperty>            m_Props:
         +0x08  EffectProperty* begin
         +0x0C  EffectProperty* end
         +0x10  EffectProperty* capacity
```

**Initialisation** (`InitPropertyList<EPD*>`, 0x001b25b4 real impl):
1. Copy parent SmartPtr → `m_Parent`
2. Tally count totals per type (skip EPDs already in parent)
3. `operator_new(0x58)` + `EffectPropertyValues(counts)` → store in `m_Values`
4. Reserve vector, then for each novel EPD: construct `EffectProperty(epd, m_Values, running_offset)` + push_back
5. Sort by name

### `SharedEffectProperties` — 0x20 bytes (32)

Reference-counted (intrusive) container of one `EffectPropertyList`.

```
+0x00  void*              vtable      — (from ReferenceCounter base class)
+0x04  int                ref_count   — (from ReferenceCounter)
+0x08  ???                            — 4 bytes unknown (padding or extra RC field)
+0x0C  EffectPropertyList m_Props     — 20 bytes
```

**Constructor** `SharedEffectProperties<N>(EPDs, parent)` (thunk 0x00106434 → real 0x001b2788):
```cpp
EffectPropertyList::EffectPropertyList<N>(this + 0x0C, EPDs, parent)
ReferenceCounter::ReferenceCounter(this)
*(int*)this = vtable_ptr
```

Allocated via `operator_new(0x20)`.

Two constructor flavours:
- `SharedEffectProperties<4ul>(EPD[4], SmartPtr)` — for the 4 matrix props in `Mesh::Mesh`
- `SharedEffectProperties<EPD*>(begin, end, SmartPtr)` — for material props in `GetPropertiesGroup`

### `EffectPropertyValues` / `ValueBuffer` — 0x58 bytes (88)

Holds the actual typed value storage for one `EffectPropertyList`. Inherits from `ValueBuffer` which is a flat struct of 10 `ArrayItem` entries plus a heap buffer.

```
+0x00  ArrayItem[0]   (8 bytes)  — type 0
+0x08  ArrayItem[1]   (8 bytes)  — type 1 = float
+0x10  ArrayItem[2]   (8 bytes)  — type 2 = bool
+0x18  ArrayItem[3]   (8 bytes)  — type 3 = Matrix44  ← Value<Type3> uses this
+0x20  ArrayItem[4]   (8 bytes)  — type 4
+0x28  ArrayItem[5]   (8 bytes)  — type 5 = Vec3
+0x30  ArrayItem[6]   (8 bytes)  — type 6
+0x38  ArrayItem[7]   (8 bytes)  — type 7 = Texture2D
+0x40  ArrayItem[8]   (8 bytes)  — type 8
+0x48  ArrayItem[9]   (8 bytes)  — type 9
+0x50  uint32         m_BufferSize   — total heap buffer size in bytes
+0x54  void*          m_Buffer       — heap-allocated, zero-initialised raw storage
```

`ArrayItem` (8 bytes, layout inferred):
```
+0x00  void*   m_Data    — pointer into m_Buffer for this type's array
+0x04  uint32  m_Count   — number of elements allocated
```

`GetValueRef<Matrix44>(values, offset)` returns:
```cpp
(Matrix44*)( values->ArrayItem[3].m_Data ) + offset
```

---

## Key Functions

| Address | Name | Signature | Notes |
|---------|------|-----------|-------|
| 0x001b67b8 | `EffectPropertyList::GetProperty` | `(list, char*) → EffectProperty*` | Binary search then parent chain |
| 0x001b6828 | `EffectPropertyList::Contains` (single) | `(list, EPD*) → bool` | Calls GetProperty by EPD name |
| 0x001b1938 | `EffectPropertyList::Contains<EPD*>` (range) | `(list, begin, end) → bool` | Iterates single Contains |
| 0x001b1958 | `EffectPropertyList::Contains<4ul>` | `(list, EPD[4]) → bool` | Passes begin, begin+0x30 |
| 0x001b685c | `EffectProperty::EffectProperty` | `(EPD*, values*, offset)` | Copy EPD fields, set m_Values/m_Offset |
| 0x001b25b4 | `EffectPropertyList::InitPropertyList<EPD*>` | `(list, begin, end, parent)` | Full init: tally, alloc buffer, build sorted props |
| 0x001b2738 | `EffectPropertyList::EffectPropertyList<4ul>` | `(list, EPD[4], parent)` | Default SmartPtr + ZeroInit + vector + InitPropertyList |
| 0x001b2788 | `SharedEffectProperties::SharedEffectProperties<4ul>` | `(sep, EPD[4], parent)` | EPL at +0xC, RefCounter, vtable |
| 0x001b68e4 | `ValueBuffer::ValueBuffer` | `(buf, counts[10])` | Alloc buffer, InitAll<Type0> |
| 0x001b1eb4 | `EffectPropertyValues::GetValueRef<Matrix44>` | `(values, offset)` | Delegates to ArrayItem[3] |
| 0x001b1ebc | `EffectPropertyValues::TrySetValue<Matrix44>` | `(values, type, offset, mat*)` | Guards type==3, then GetValueRef+copy |
| 0x000feed4 | `TrySetValue<Matrix44>` (thunk) | — | → 0x001b1ebc |
| 0x001b0c28 | `TrySetMatrix_EffectProp` | `(EP*, Matrix44*)` | Reads EP+4/+C/+10, calls TrySetValue |
| 0x001a76fc | `SetEffectVec3_Engine` | `(list, name, Vec3*)` | GetProperty + SetValue<Vec3> |
| 0x001a74bc | `GetColourRGB` | `(out_Vec3*, uint32) → Vec3` | byte0=R, byte1=G, byte2=B, /255.0 |

---

## GetProperty Algorithm

```cpp
EffectProperty* GetProperty(EffectPropertyList* list, char* name) {
    // Binary search the sorted vector at list+0x08
    auto it = std::lower_bound(list->m_Props.begin(), list->m_Props.end(), name);
    if (it != list->m_Props.end()) {
        // Compare: (*it->name_ptr + 0x0C) is the basic_string at the Immutable
        if (basic_string::compare(*it->name_ptr + 0x0C, name) == 0)
            return &*it;
    }
    // Fall back to parent chain
    if (list->m_Parent != nullptr) {
        // *(list->m_Parent) = SharedEffectProperties*
        // + 0x0C = its EffectPropertyList
        return GetProperty(*(list->m_Parent) + 0x0C, name);
    }
    return nullptr;
}
```

---

## SetValue / TrySetValue

```cpp
// Called as: EffectProperty::SetValue<T>(ep, value_ptr, index=0)
void SetValue<T>(EffectProperty* ep, T* value, int idx) {
    EffectPropertyValues::TrySetValue<T>(ep->m_Values, ep->type,
                                          ep->m_Offset + idx, value);
}

// TrySetValue<Matrix44>:
bool TrySetValue<Matrix44>(EffectPropertyValues* values, Type type,
                            ulong offset, Matrix44* mat) {
    if (type != 3) return false;
    Matrix44* slot = (Matrix44*)values->ArrayItem[3].m_Data + offset;
    *slot = *mat;  // 16-float copy
    return true;
}
```

---

## Property Flow: LoadMesh → Draw → GL

### Step 1 — LoadMesh (0x001a7c90, material loop)

For each material entry in the `.mad` file:

```
// 9 EffectPropertyDefinitions on stack:
EPD[0] = { "DiffuseMap",       type=7, count=1 }  // Texture2D
EPD[1] = { "UVWOffset",        type=5, count=3 }  // Vec3 × 3
EPD[2] = { "Alpha",            type=1, count=1 }  // float
EPD[3] = { "Ambience",         type=5, count=1 }  // Vec3
EPD[4] = { "Diffuse",          type=5, count=1 }  // Vec3
EPD[5] = { "SelfIllum",        type=5, count=1 }  // Vec3
EPD[6] = { "Specular",         type=5, count=1 }  // Vec3
EPD[7] = { "SpecularStrength", type=1, count=1 }  // float
EPD[8] = { "IsLit",            type=2, count=1 }  // bool

SharedPropsInfo* group = mesh->GetPropertiesGroup<9>(material_name, EPDs);
SharedEffectProperties* sep = *(SmartPtr*)group;   // group is SmartPtr

// Set property values:
GetProperty(sep->m_Props, "IsLit") → SetValue<bool>(false)
GetColourRGB(ambience_colour)   → SetEffectVec3_Engine(sep, "Ambience",  &colour)
GetColourRGB(diffuse_colour)    → SetEffectVec3_Engine(sep, "Diffuse",   &colour)
GetColourRGB(selfillum_colour)  → SetEffectVec3_Engine(sep, "SelfIllum", &colour)
GetProperty(sep, "SpecularStrength") → SetValue<float>(spec_strength)
GetProperty(sep, "DiffuseMap")       → SetValue<SmartPtr<Texture2D>>(texture)

// Optional: name the texture map for GenerateBindings
if (tex_name.length > 0):
    group->AddTextureMap("DiffuseMap", tex_name)
        // stores EffectProperty* for "DiffuseMap" in group->m_TextureMaps["DiffuseMap"]
```

### Step 2 — Mesh::Mesh constructor (0x001b0e70)

Creates 4 matrix `EffectPropertyDefinitions` and a `SharedEffectProperties` for them:

```
EPD[0] = { "World",                  type=3, count=1 }
EPD[1] = { "SceneCamera.View",       type=3, count=1 }
EPD[2] = { "SceneCamera.Projection", type=3, count=1 }
EPD[3] = { "WorldViewProjection",    type=3, count=1 }

// If parent already has all 4 → reuse parent's SharedEffectProperties
// Otherwise → new SharedEffectProperties<4ul>(EPDs, parent)

// Cache EffectProperty pointers:
mesh->m_WorldEP      (this+0x6C) = GetProperty(list, "World")
mesh->m_ViewEP       (this+0x70) = GetProperty(list, "SceneCamera.View")
mesh->m_ProjEP       (this+0x74) = GetProperty(list, "SceneCamera.Projection")
mesh->m_WVP_EP       (this+0x78) = GetProperty(list, "WorldViewProjection")
```

### Step 3 — Mesh::Draw (0x001b0c3c)

```cpp
// Per-frame: update the 4 matrix properties
TrySetMatrix_EffectProp(mesh->m_WorldEP, &worldMatrix)
TrySetMatrix_EffectProp(mesh->m_ViewEP,  SceneCamera.View_matrix_ptr)
TrySetMatrix_EffectProp(mesh->m_ProjEP,  SceneCamera.Proj_matrix_ptr)
if (mesh->m_WVP_EP):
    WVP = worldMatrix * View * Proj
    TrySetMatrix_EffectProp(mesh->m_WVP_EP, &WVP)

// Then render each Geometry
for geo in mesh->m_Geometries:
    Geometry::Render(geo)  → PassBinding::Apply(0x001a39f8)
```

### Step 4 — PassBinding::Apply (0x001a39f8)

Reads values from the `SharedEffectProperties` associated with each `Geometry` and submits them to GLES1:

| Property            | GL call (original GLES1)  |
|---------------------|---------------------------|
| `DiffuseMap`        | `glBindTexture`           |
| `World`             | `glLoadMatrixf`           |
| `SceneCamera.View`  | `glLoadMatrixf`           |
| `SceneCamera.Projection` | `glLoadMatrixf`      |
| `WorldViewProjection`| `glLoadMatrixf`          |
| `Ambience`          | `glMaterialfv(GL_AMBIENT)`|
| `Diffuse`           | `glMaterialfv(GL_DIFFUSE)`|
| `IsLit`             | Enables/disables lighting |

---

## Mesh::SharedPropsInfo (nested type)

Stored in `Mesh::m_PropertiesGroups` (map at `this+0x50`), keyed by material name.

```
Constructor:   0x001b2234
Destructor:    0x001b207c
AddTextureMap: 0x001b1394

+0x00  SmartPtr<SharedEffectProperties>             m_Props
+0x04  map<AsciiString, TextureProps>               m_TextureMaps
```

`GetPropertiesGroup` (1-param, 0x001b0988) — returns `(SharedPropsInfo*)` at `(map_node + 0x38)`.

`GetPropertiesGroup` (3-param, 0x001b1430) — find-or-create:
1. Look up by name in `m_PropertiesGroups`
2. If found and existing `SharedEffectProperties` already `Contains` all requested EPDs → reuse
3. Otherwise → `operator_new(0x20)` + `SharedEffectProperties<EPD*>(begin, end, existing_or_mesh_parent)` → insert into map

### `Mesh::TextureProps` (4 bytes)

```
+0x00  EffectProperty*  m_Property  — points into the SharedEffectProperties' value vector
```

`AddTextureMap(mesh_name, tex_name)` (0x001b1394):
- Looks up or inserts `TextureProps` in `m_TextureMaps`
- Calls `GetProperty(sep->m_Props, full_property_name)` to fill `m_Property`

---

## GetPropertiesGroup Algorithm (3-param)

```cpp
SmartPtr<SharedEffectProperties>*
GetPropertiesGroup(Mesh* mesh, AsciiString name, EPD* begin, EPD* end) {
    SmartPtr<SEP>* slot = m_PropertiesGroups.find(name);  // look up in map
    if (slot != nullptr) {
        // Check that existing SEP has ALL requested properties
        bool all_present = true;
        for (EPD* epd = begin; epd != end; epd += 0xC) {
            if (!Contains((*slot)->m_Props, epd)) { all_present = false; break; }
        }
        if (all_present) return slot;
    }
    // Create new SharedEffectProperties
    slot = m_PropertiesGroups[name];   // insert/fetch slot in map
    SEP* parent = (slot != nullptr) ? *slot : mesh->m_MatrixSEP;
    SEP* new_sep = operator_new(0x20);
    SharedEffectProperties<EPD*>(new_sep, begin, end, parent);
    *slot = new_sep;
    return slot;
}
```

---

## Notes for Port

- **EffectProperty is a value type** (lives in a vector, not on heap). The vector stores `EffectProperty` by value and sorts them.
- **Parent chaining**: the hierarchy is `Geometry::SharedEffectProperties → Mesh::m_MatrixSEP`. Matrix props (World/View/Proj/WVP) live in the mesh-level SEP; material props (colours, textures) live in geometry-level SEPs. `GetProperty` falls back to the parent chain automatically.
- **No ref counting for EffectProperty itself** — only `SharedEffectProperties` is ref-counted via `SmartPtr`. `EffectPropertyValues` is owned by `auto_ptr` inside `EffectPropertyList`.
- **Matrix update is per-frame** in `Mesh::Draw`; material colour/texture updates happen only during `LoadMesh` (static after load).
- **Port currently bypasses this** with direct shader uniforms. Full fidelity would replicate this system, especially the parent-chain inheritance which allows the engine to efficiently share matrix properties across all meshes.
