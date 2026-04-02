# Rendering Details

Low-level graphics functions decompiled from the Mortar engine.

## Mortar::Model::Draw (0x1930e0, 79 lines)

Renders a 3D model with a transform matrix.

```c
void Model::Draw(const Matrix44& transform) {
    int meshCount = meshes.size();
    
    if (meshCount < 2) {
        // Single mesh: draw directly
        meshes[0]->Draw(transform);
    } else {
        // Multiple meshes: depth-sort then draw back-to-front
        // 1. Compute view-space Z for each mesh center
        Matrix44 viewProj = projection * transform;
        for each mesh:
            Vec3 center = mesh->GetBounds().Center();
            float z = (center * viewProj).z / (center * viewProj).w;
            sortArray[i] = {mesh, z};
        
        // 2. Sort by Z using qsort
        qsort(sortArray, meshCount, 8, compareFunc);
        
        // 3. Draw in sorted order
        for each sorted entry:
            entry.mesh->Draw(transform);
    }
}
```

**Key insight**: Each Mesh has a `Draw(Matrix44)` virtual call at vtable+0x10. Models with multiple meshes (sliced fruit halves) are depth-sorted before drawing.

The actual per-mesh draw is in `Mesh::Draw` which calls `GeometryBinding_Bada::PassBinding::Apply` (already decompiled) to set up vertex arrays, then `glDrawElements` or `glDrawArrays`.

## TintWhite (0x135af8, 52 lines)

Converts a float[3] colour (RGB, 0.0-1.0) to a packed Colour (BGRA bytes).

```c
Colour TintWhite(float* rgb) {
    uint8_t r = clamp(rgb[0] * 255.0, 0, 255);
    uint8_t g = clamp(rgb[1] * 255.0, 0, 255);
    uint8_t b = clamp(rgb[2] * 255.0, 0, 255);
    return MakeColour(r, g, b);  // alpha = 0xFF implicitly
}
```

Called before every textured quad draw to set the tint colour. `DAT_00135ba0` = 255.0f.

## TintColour (0x13540c, 53 lines)

Multiplies a Colour's RGB channels by a float[3] scale, in-place.

```c
Colour TintColour(Colour* colour, float* scale) {
    colour->r = clamp(colour->r * scale[0], 0, 255);
    colour->g = clamp(colour->g * scale[1], 0, 255);
    colour->b = clamp(colour->b * scale[2], 0, 255);
    return *colour;
}
```

Used for power-up tints, ambient lighting, and fade effects.

## SetupQuad (0x13929c, 78 lines)

Builds a QUADCUSTOMVERTEX[4] quad with position, UV, and colour, with Y-axis clipping.

```c
int SetupQuad(QUADCUSTOMVERTEX* out, Vec3 pos, float width, float height, 
              MortarRectangleDec rect, Colour colour) {
    float top = pos.y + height * 0.5;
    float bottom = pos.y - height * 0.5;
    
    // Clip to screen bounds
    if (bottom < screenBottom) { uvTop = 0.5 - (screenBottom - pos.y) / height; bottom = screenBottom; }
    if (top > screenTop) { top = screenTop; uvBottom = ...; }
    
    // Build 4 vertices (triangle strip)
    for i in 0..3:
        vertex.colour = PlatformColour(colour);
        vertex.x = pos.x ± width/2;
        vertex.y = top or bottom;
        vertex.u = 0 or 1;
        vertex.v = computed from clipping;
    return 1;  // or 0 if fully clipped
}
```

## AddQuad (0x175db0, 67 lines)

Appends a 6-vertex quad (2 triangles) to a QUADCUSTOMVERTEX buffer. Used for batch rendering (splats, blade trail).

```c
void AddQuad(QUADCUSTOMVERTEX** buffer, float x, float y, float halfW, float halfH, Colour colour) {
    QUADCUSTOMVERTEX* p = *buffer;
    
    // 6 vertices = 2 triangles (not strip)
    // Initialize all 6 with default UVs and colour
    for (int i = 0; i < 6; i++) {
        p[i].colour = PlatformColour(colour);
        p[i].z = 0; p[i].nx = 0; p[i].ny = 0; p[i].nz = 1.0;
    }
    
    // Triangle 1: bottom-left, bottom-left copy, top-left
    p[0] = {x-halfW, y-halfH, u=0, v=1}
    p[1] = p[0]  // degenerate? or same corner
    p[2] = {x-halfW, y+halfH, u=0, v=0}
    
    // Triangle 2: top-right, bottom-right, top-right copy  
    p[3] = {x+halfW, y-halfH, u=1, v=1}
    p[4] = {x+halfW, y+halfH, u=1, v=0}
    p[5] = p[4]
    
    *buffer += 6 * 0x24;  // advance pointer by 6 vertices
}
```

Vertex stride = 0x24 (36 bytes) confirmed. Each QUADCUSTOMVERTEX:

| Offset | Type | Field |
|--------|------|-------|
| +0x00 | float | x |
| +0x04 | float | y |
| +0x08 | float | z (=0) |
| +0x0c | float | nx (=0) |
| +0x10 | float | ny (=0) |
| +0x14 | float | nz (=1.0) |
| +0x18 | uint | colour (packed BGRA) |
| +0x1c | float | u |
| +0x20 | float | v |

## Mortar::Font::Load (0x199e9c, 270 lines)

Loads a BMFont .fnt file. Parses the text format line-by-line:
1. Opens file via `Mortar::File`
2. Reads `info`, `common`, `page`, `chars`, `char` lines
3. For each `page`: loads the referenced .tex texture atlas
4. For each `char`: stores glyph metrics (x, y, width, height, xoffset, yoffset, xadvance)
5. Parses `kerning` pairs

Font::DrawString (0x198e44, 13 params, ~300 lines) renders text by iterating characters, looking up glyph rects in the atlas, applying kerning, and drawing textured quads.

## QUADCUSTOMVERTEX Struct (confirmed)

```c
struct QUADCUSTOMVERTEX {  // 0x24 = 36 bytes
    float x, y, z;         // position
    float nx, ny, nz;      // normal (typically 0,0,1 for 2D)
    uint32_t colour;       // packed BGRA
    float u, v;            // texture coordinates
};
```

Used by: SlashEntity blade trail, SlashEntityGhost, SplatEntity, SetupQuad, AddQuad, Mesh::DrawTriList.

## SplatEffect (0x180438, separate from SplatEntity)

Simple textured quad overlay. Size ~0x1C bytes.

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | Colour | m_Colour | BGRA tint |
| +0x04 | Vec3 | m_Scale | Random (0.5-1.0)× texture size |
| +0x10 | Vec3 | m_Position | From global config |

- **Update**: no-op (returns 0)
- **Draw**: `Texture::Set → Reset matrix → Scale → Translate → UploadMatrices → DrawQuadUnCached → Texture::UnSet`
- Constructor loads a localised texture, computes size from texture dimensions, randomises scale

## ScoreMultiplyerBoard : HUDControl3d

Arcade mode score multiplier popup (e.g. "x2"). Size ~0x9C bytes.

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x7c | float | m_PosX | From global Vec3 |
| +0x80 | float | m_PosY | |
| +0x84 | float | m_PosZ | |
| +0x88 | int | m_field88 | = 0 |
| +0x8c | int | m_CurrentScore | Current multiplied score |
| +0x90 | int | m_PrevScore | Previous score (-1 = none) |
| +0x94 | float | m_Timer | Animation timer |
| +0x98 | float | m_Scale | = 1.0 |

- **Draw**: Formats score as string (e.g. "%d"), renders via `Font::DrawString` with colour (green if timer < 0.5, default otherwise)
- Position offset from HUDControl base position

## PowerUpShop : HUDControl3d

In-game power-up purchase screen. Constructor just creates empty vectors.

- `vector<PowerUp*>` — available power-ups
- `vector<Vec3>` — button positions
- `this[0x32]` = byte flag = 1 (active/enabled)

Relatively simple; the real complexity is in ShopScreen which manages the full shop UI.

## ShopListItem : ScrollingMenuItem

Individual item in the blade shop. Size ~0x284 bytes.

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x25c | float | m_field25c | = 0 |
| +0x260 | float | m_field260 | = 0 |
| +0x264 | float | m_field264 | = 0 |
| +0x274 | SmartPtr\<Texture\> | m_ItemTexture | |
| +0x278 | int | m_field278 | = 0 |
| +0x27c | byte | m_bSelected | = 1 |
| +0x27d | byte | m_bField27d | = 0 |
| +0x27e | byte | m_bField27e | = 0 |
| +0x280 | float | m_field280 | = 0 |

Inherits ScrollingMenuItem (already documented in ui-widgets.md).

## Mortar::DisplayManager (singleton)

GL state manager. Constructor reveals key fields:

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | DisplayManagerFns* | vtable | Virtual: IsRenderingAllowed, SetDepthBuffer, SetDrawColour, GetWindowSize, etc. |
| +0x04 | Colour | m_ClearColour | Background clear colour |
| +0x08 | Colour | m_DrawColor | Current draw colour (set by TintWhite/TintColour) |
| +0x0c | Rect | m_Viewport | left, top, right, bottom |
| +0x1c | Vec3 | m_lightDirection | Set by SetLightDirection |
| +0x28 | uint | m_AmbientColour | = 0xFF000000 (black opaque) |
| +0x2c | byte | m_bField2c | = false |
| +0x2d | byte | m_bField2d | = 0 |
| +0x30 | int | m_field30 | |
| +0x34 | int | m_field34 | = 0 |
| +0x44 | int | m_MagFilter | = 1 (GL_NEAREST or GL_LINEAR) |
| +0x48 | int | m_MinFilter | = 1 |
| +0x4c | int | m_WrapS | = 1 |
| +0x50 | int | m_WrapT | = 1 |

Key virtual functions used throughout GameDraw:
- `SetDepthBuffer(bool)` / `SetDepthBufferWrite(bool)`
- `SetGlobalAmbience(Colour)`
- `SetDrawColour(Colour)`
- `GetWindowSize(out)`
- `IsRenderingAllowed()`

---

## See Also

- [Rendering functions](../functions/rendering.md) -- draw call pseudocode
- [Asset functions](../functions/assets.md) -- GPUafyTexture, LoadVertexStreamPSP
- [Model format](../formats/models.md) -- HBR0 container, vertex streams
- [Texture format](../formats/textures.md) -- .tex file layout
