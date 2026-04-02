# .mad / .mmd Model Formats

## Overview

Halfbrick proprietary binary model format using `HBR0` container.

- **.mad** — Model Animation Data (references, bone names, hierarchy)
- **.mmd** — Model Mesh Data (geometry, materials, texture refs)

## HBR0 Container

```
+0x00: char[4]  magic         "HBR0"
+0x04: int32    type/version  (0 for .mad, varies for .mmd)
+0x08: int32    flags
+0x0C: int32    dataSize
```

HBR0 containers can be nested (seen in .mmd files).

## .mad — Model Animation Data

Contains bone names and animation references. Example (apple.mad):

```
HBR0 header (type=0)
+0x10: int16  count (1)
+0x12: int16  pathLength (0x3E = 62)
+0x14: char[] originalPath ("D:\Projects\iPhoneDev\FruitNinja\Asset_working\Fruit\Fruit.max")
+....: float  unknown (0.4333...)
+....: float  unknown (0.0666...)
+....: int16  boneCount (2)
+....: bone references: "a_piece_1", "a_piece_2"
```

The original path confirms this was built on Windows from 3DS Max files for an iPhone project, then cross-compiled for Bada.

## .mmd — Model Mesh Data

Contains geometry, material references, and texture bindings. Example (apple_single.mmd):

```
HBR0 outer (type=2, size=0x73)
  HBR0 inner (type=1, size=0x32)
    HBR0 geometry (type=0)
      +...: int16 nameLength (6)
      +...: char[] name ("Map #1")
      +...: int16 texPathLength (0x18 = 24)
      +...: char[] texturePath ("textures\fruit_atlas.tex")
      +...: material data: "fruit_atlas"
      +...: vertex/index data
```

## Model Naming Convention

Each fruit has multiple model files:

| File | Purpose |
|------|---------|
| `{name}.mad` | Animation data (whole fruit, references piece names) |
| `{name}_single.mad` | Static whole-fruit model |
| `{name}_single.mmd` | Whole-fruit mesh data |
| `{name}_a_piece_1.mmd` | Sliced half A mesh |
| `{name}_a_piece_2.mmd` | Sliced half B mesh |
| `{name}_outline.mad` | Outline effect model |
| `{name}_center.mad` | Center cross-section model |
| `{name}ice.mad` | Frozen variant (freeze power-up) |

## Texture References

Models reference textures by path relative to `Data/`:
- `textures\fruit_atlas.tex` — main fruit sprite sheet
- `textures\fruit_big_sheet_1.tex` — additional sprites

## GL Vertex Format (from GeometryBinding_Bada::PassBinding::Apply, 0x1a39f8)

The rendering uses OpenGL ES 1.x fixed-pipeline vertex arrays. Four arrays are bound:

| Array | GL Call | Components | Type |
|-------|---------|-----------|------|
| Position | glVertexPointer | 2 or 3 | GL_FLOAT (0x1406) |
| Normal | glNormalPointer | 3 (implied) | GL_FLOAT |
| Colour | glColorPointer | 3 or 4 | GL_UNSIGNED_BYTE (0x1401) |
| TexCoord | glTexCoordPointer | 2 | GL_FLOAT |

### PassBinding Struct (per-pass vertex attribute descriptor)

```
+0x00: int  vertexSize        (2 or 3)
+0x04: int  vertexType        (GL_FLOAT = 0x1406)
+0x08: int  vertexStride
+0x0c: int  vertexOffset
+0x10: IVertexStream*  vertexStream

+0x14: int  normalSize        (unused in call, always 3)
+0x18: int  normalType
+0x1c: int  normalStride
+0x20: int  normalOffset
+0x24: IVertexStream*  normalStream

+0x28: int  colorSize         (3 or 4)
+0x2c: int  colorType         (GL_UNSIGNED_BYTE)
+0x30: int  colorStride
+0x34: int  colorOffset
+0x38: IVertexStream*  colorStream

+0x3c: int  texCoordSize      (2)
+0x40: int  texCoordType      (GL_FLOAT)
+0x44: int  texCoordStride
+0x48: int  texCoordOffset
+0x4c: IVertexStream*  texCoordStream
```

### Quad Vertex Format (DrawQuadUnCached)

For simple 2D quads (UI, splats, blade trail):
```c
struct QuadVertex {  // stride = 0x14 = 20 bytes
    float x, y, z;   // position (3 floats)
    float u, v;       // tex coords (2 floats)
};
// Rendered as GL_TRIANGLE_STRIP with 4 vertices
// Colour set globally via DisplayManager::SetDrawColour
```

### 3D Mesh Vertex Format (fruit models)

For 3D fruit meshes, all 4 arrays (position + normal + colour + texcoord) are used. The vertex streams are stored in VBOs (`IVertexStream_Bada::_BindBuffer`). Based on the PassBinding setup:

```c
struct MeshVertex {  // estimated stride
    float px, py, pz;     // position (12 bytes)
    float nx, ny, nz;     // normal (12 bytes)
    uint8_t r, g, b, a;   // colour (4 bytes)
    float u, v;            // tex coord (8 bytes)
};  // total: 36 bytes estimated
```

Note: position/normal/colour/texcoord may use separate streams (interleaved or separate VBOs).

## Vertex Data Format — PSP GE Legacy (FULLY DECODED)

Source: `LoadVertexStreamPSP` (0x1a7b0c, 112 lines) and `LegacyPSPVertexDecl` (0x1a741c).

The vertex format is specified by a **packed uint32 bitfield** following the PSP Graphics Engine vertex declaration format. This is read from the .mmd file and determines the layout of each vertex.

### Vertex Declaration Bitfield

```
uint32_t packed;
bits [1:0]   = texture coordinate format  (0=none, 1=u8, 2=u16, 3=float)
bits [4:2]   = weight format              (0=none, 1=u8, 2=u16, 3=float)
bits [6:5]   = color format               (0=none, 1=565, 2=5551, 3=4444, 4+=8888)
bits [8:7]   = normal format              (0=none, 1=s8, 2=s16, 3=float)
bits [10:9]  = position format            (0=none, 1=s8, 2=s16, 3=float)
bits [12:11] = additional field           (2 bits)
bits [15:13] = morph/bone count           (3 bits)
bits [18:16] = weight count               (3 bits)
bit  [19]    = flag (transform 2D?)       (1 bit)
bits [21:20] = field                      (2 bits)
bits [23:22] = field                      (2 bits)
bits [31:24] = vertex count or flags      (8 bits)
```

### Vertex Element Listing (GenerateElementListing, 0x1a7718)

Elements created in order with named semantics:
1. **Position** — size varies by format (s8=1, s16=2, float=4 bytes per component), 2 components
2. **Field at +0x04** — 4 components (weights or bone indices)
3. **Normal at +0x34** — 3 components
4. **Color at +0x30** — 3 components

### Stride Calculation

```c
int Stride() {
    return (FormatSize(normal) + FormatSize(color) + FormatSize(field_0xd) + FormatSize(field_0xc)) * 3
         + FormatSize(texcoord) * (numWeights + 1)
         + FormatSize(position) * 2
         + FormatSize(weight);
}
```

Where `FormatSize(fmt)` returns bytes per component: 0=0, 1=1(byte), 2=2(short), 3=4(float).

### Index Data Format (LoadIndexStreamPSP, 0x1a799c)

```c
byte flags = Read<byte>();
PrimType = (flags & 0xF0):
    0x20 → GL_TRIANGLE_STRIP
    0x30 → GL_TRIANGLE_FAN
    0x40 → GL_TRIANGLES
    0x50 → GL_LINES
    0x60 → GL_POINTS

IndexFormat = (flags & 0x0F):
    determines bytes per index (u8 or u16)
```

### Loading Flow (LoadVertexStreamPSP)

```
1. Read byte: skip count → skip that many uint32s
2. Read uint32: vertex declaration bitfield → unpack format fields
3. Read uint32: vertex count
4. Compute stride = LegacyPSPVertexDecl::Stride()
5. Read raw bytes: count * stride → vertex data buffer
6. Create IVertexSource with data + vertex element listing
7. Wrap in VertexStreamBasic → return SmartPtr<IVertexStream>
```

### Practical Parsing

For the Fruit Ninja Bada build, vertices are most likely:
- Position: **3 floats** (format=3)
- TexCoord: **2 floats** (format=3)
- Color: **4 bytes RGBA** (format=7 → 8888)
- Normal: **3 floats** or **3 shorts** (format=2 or 3)

To extract: read the bitfield, compute stride, then interpret the raw data block as an array of vertices with the decoded layout.

---

## HBR0 Container Structure

```
HBR0 Header (12 bytes):
  +0x00: char[4]  "HBR0"
  +0x04: uint16   type       (0=data, 1=node, 2=container)
  +0x06: uint16   padding?
  +0x08: uint32   dataSize   (size of payload, not including header)

Types:
  0 = Leaf data block (vertex data, index data, or metadata)
  1 = Node (contains sub-blocks, references textures)
  2 = Container (top-level, wraps child nodes)
```

Blocks are nested: Container → Node → Data blocks. The node blocks contain:
- Short-prefixed strings (2-byte length + chars): texture paths, map names
- Material references: "fruit_atlas", texture paths like "textures\fruit_atlas.tex"
- Vertex/index data in leaf blocks

## For Porting

**Recommended approach**: Write an HBR0 extractor tool that:
1. Parses nested HBR0 containers
2. Extracts texture path references (to map to converted PNGs)
3. Extracts vertex data blocks (interpret as float arrays for position/UV, byte arrays for colour)
4. Extracts index data blocks (uint16 triangle indices)
5. Outputs to a simple format (OBJ, or a custom JSON+binary)

The fruit meshes are simple (each half is ~100-200 triangles) so the vertex data blocks will be small and identifiable.

## Key Functions

| Function | Address | Lines | Purpose |
|----------|---------|-------|---------|
| GeometryBinding_Bada::PassBinding::Apply | 0x001a39f8 | 101 | Sets up GL vertex arrays (fully decompiled) |
| Mesh::DrawQuadUnCached | 0x00194060 | 73 | Draws 2D textured quad (fully decompiled) |
| Mesh::DrawTriList | 0x0019404c | — | Draws indexed triangle list |
| Mesh::DrawTris | 0x00193f5c | — | Draws non-indexed triangles |
| BadaTextureData::GPUafyTexture | 0x001898d8 | 42 | Uploads texture to GPU (fully decompiled) |
| BadaTextureData::TexFmtToGL | 0x00189f78 | 53 | Format enum → GL constants (fully decompiled) |
| ResourceLoader::Load | 0x001aa684 | 17 | HBR0 container loading entry point |
| Texture2DFromFile_Bada ctor | 0x00189c1c | 65 | Loads .tex file (fully decompiled) |

---

## See Also

- [Asset functions](../functions/assets.md) -- LoadVertexStreamPSP pseudocode
- [Rendering detail system](../systems/rendering-detail.md) -- mesh pipeline
