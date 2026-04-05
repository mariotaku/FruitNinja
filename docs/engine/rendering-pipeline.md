# Rendering Pipeline Internals

Two distinct rendering paths in the Mortar engine.

## Path A — Data-Driven 3D Rendering

Used for fruit meshes, bombs, 3D models. Full Effect/Material system.

```
Model::Draw(transform)
  → depth-sort meshes if >1
  → Mesh::Draw(transform) for each
    → sets matrix EffectProperties ("World", "ViewProjection", "View")
    → for each Geometry in mesh:
      → Geometry::Render()
        → loads matrices into GL via EffectProperty
        → binds textures from effect properties ("DiffuseTexture")
        → iterates PassBinding objects
          → PassBinding::Apply() sets up vertex arrays (glVertexPointer, glNormalPointer, etc.)
          → glDrawElements or glDrawArrays
```

### Key Structs

#### Effect_Bada (~0x1C bytes)
| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | ReferenceCounter | refcount | Intrusive ref counting |
| +0x0C | vector\<Pass\> | m_Passes | Render passes |
| +0x18 | vector\<EffectPropertyDefinition_Bada\> | m_PropertyDefs | Named property definitions |

#### GeometryBinding_Bada (~0x30+ bytes)
| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | SmartPtr\<EffectGroup\> | m_pEffectGroup | Shader/effect reference |
| +0x04 | vector\<SmartPtr\<IVertexStream\>\> | m_VertexStreams | Vertex buffer bindings |
| +0x10 | SmartPtr\<IIndexStream\> | m_pIndexStream | Index buffer |
| +0x14 | map\<name, IIndexStream\> | m_NamedIndexStreams | Named index stream overrides |
| +0x20 | vector\<EffectBinding\> | m_Bindings | Per-pass effect bindings |

#### PassBinding (0x68 bytes)
5 GLFuncParams blocks + IIndexStream reference:

| Offset | Size | Name | Notes |
|--------|------|------|-------|
| +0x00 | 0x14 | position | GLFuncParams: size, type, stride, offset, stream ptr |
| +0x14 | 0x14 | normal | |
| +0x28 | 0x14 | color | |
| +0x3C | 0x14 | texcoord0 | |
| +0x50 | 0x14 | texcoord1 | |
| +0x64 | 0x04 | m_pIndexStream | IIndexStream pointer for this pass |

GLFuncParams (0x14 bytes each): `{ int size, GLenum type, int stride, int offset, void* streamPtr }`

#### Geometry (~0x18+ bytes)
| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | GeometryBinding* | m_pBinding | |
| +0x04 | int | m_EffectBindingIndex | Which pass binding to use |
| +0x08 | SharedEffectProperties* | m_pProperties | Material properties |

#### Mesh (~0x7C bytes)
| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | IModelNode | base | Node interface |
| +0x?? | AsciiString | m_Name | Mesh name |
| +0x?? | vector\<BoneBinding\> | m_BoneBindings | Skeletal animation |
| +0x?? | vector\<SmartPtr\<Geometry\>\> | m_Geometries | Sub-meshes |
| +0x?? | SharedEffectProperties* | m_pProperties | Shared material properties |
| +0x74 | EffectProperty* | m_pWorldProp | Cached "World" matrix property |
| +0x78 | EffectProperty* | m_pViewProjProp | Cached "ViewProjection" matrix property |

#### EffectProperty (0x14 bytes)
| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | EffectPropertyDefinition | m_Definition | Name + type |
| +0x08 | EffectPropertyValues* | m_pValues | Actual data storage |
| +0x0C | int | m_Offset | Offset into values buffer |

Named properties used in the engine:
- `"World"` — model matrix (Matrix44)
- `"ViewProjection"` — combined view×projection matrix
- `"View"` — view matrix only
- `"DiffuseTexture"` — texture binding

### Geometry::Render (0x001a3e98) — Core 3D Draw Call

```c
void Geometry::Render() {
    // Load matrices from EffectProperties into GL
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(viewProjection);     // from "ViewProjection" property
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(world);              // from "World" property

    // Bind diffuse texture
    Texture::Set(diffuseTexture);      // from "DiffuseTexture" property

    // Apply vertex attribute bindings
    PassBinding::Apply(binding);       // sets glVertexPointer, glNormalPointer, etc.

    // Issue draw call
    if (indexStream)
        glDrawElements(GL_TRIANGLES, count, type, offset);
    else
        glDrawArrays(GL_TRIANGLES, 0, count);
}
```

### PassBinding::Apply (0x001a39f8) — GL 1.x Vertex Setup

```c
void PassBinding::Apply() {
    // Position (always enabled)
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(pos.size, pos.type, pos.stride, pos.streamPtr + pos.offset);

    // Normal (optional)
    if (normal.streamPtr)
        glEnableClientState(GL_NORMAL_ARRAY);
        glNormalPointer(normal.type, normal.stride, normal.streamPtr + normal.offset);

    // Color (optional)
    if (color.streamPtr)
        glEnableClientState(GL_COLOR_ARRAY);
        glColorPointer(color.size, color.type, color.stride, ...);

    // TexCoord0 (optional)
    if (texcoord0.streamPtr)
        glClientActiveTexture(GL_TEXTURE0);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(texcoord0.size, texcoord0.type, ...);

    // TexCoord1 (optional)
    if (texcoord1.streamPtr)
        glClientActiveTexture(GL_TEXTURE1);
        ...
}
```

**Port note:** Replace with `glVertexAttribPointer` + shader attributes in GLES2.

---

## Path B — Immediate-Mode 2D Rendering

Used for HUD, backgrounds, text, blade trails, splats. Bypasses Effect system entirely.

### DrawQuadUnCached (0x00194060)

Builds 4-vertex quad on stack with compact 0x14 (20 byte) stride:
```
struct QuadVertex {  // 0x14 bytes
    float x, y;      // position
    float u, v;       // texcoord
    uint32 color;     // packed BGRA
};
```

```c
void Mesh::DrawQuadUnCached(Colour colour, float u0, float v0, float u1, float v1) {
    QuadVertex verts[4];
    // Build quad: (-0.5,-0.5) to (0.5,0.5) with UVs and colour
    verts[0] = { -0.5, -0.5, u0, v0, colour };
    verts[1] = {  0.5, -0.5, u1, v0, colour };
    verts[2] = { -0.5,  0.5, u0, v1, colour };
    verts[3] = {  0.5,  0.5, u1, v1, colour };

    glVertexPointer(2, GL_FLOAT, 0x14, &verts[0].x);
    glTexCoordPointer(2, GL_FLOAT, 0x14, &verts[0].u);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0x14, &verts[0].color);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}
```

### DrawTriList / DrawTriStrip (0x00193f5c)

Used for blade trails (SlashEntity), sparkle rings (MenuButton), splats:
```c
void Mesh::DrawTriList(QUADCUSTOMVERTEX* verts, int vertCount, bool isStrip) {
    // QUADCUSTOMVERTEX stride = 0x24 (36 bytes)
    // position at +0x00 (3 floats)
    // texcoord at +0x0C (2 floats)
    // colour   at +0x18 (packed BGRA)

    glVertexPointer(3, GL_FLOAT, 0x24, &verts[0].x);
    glTexCoordPointer(2, GL_FLOAT, 0x24, &verts[0].u);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0x24, &verts[0].colour);

    GLenum mode = isStrip ? GL_TRIANGLE_STRIP : GL_TRIANGLES;
    glDrawArrays(mode, 0, vertCount);
}
```

---

## Font System

### Font (~0x430 bytes)

256 glyph entries + page count + atlas dimensions + scale factor + per-page vertex batches.

Loaded from `.fnt` BMFont text format via `Font::Load` (0x00199e9c, 270 lines).

### Font::DrawString (0x00198e44)

Full text renderer with:
- Inline color tags (e.g., `[FFFFFF]text[/]`)
- Word wrapping at max width
- Per-glyph quad generation with kerning
- Multi-page atlas support
- Alignment flags (0x0F mask): left/center/right/top/bottom

```c
void Font::DrawString(float scale, float maxWidth, float z,
                      SmartPtr<Texture> atlas, Utf8StringIterator& text,
                      Vec3& pos, Colour colour, Vec2& alignment,
                      int flags, DrawEffectContainer* fx) {
    // For each character:
    //   look up glyph in 256-entry table
    //   compute quad position with kerning + scale
    //   batch into per-page vertex arrays
    // Flush each page's batch via DrawTriList
}
```

### BakedString (~0x1C bytes)

Pre-baked text: renders once via Font::DrawString, caches the vertex data for fast repeated draws.

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | SmartPtr\<Texture\>* | m_pTextures | Array of page textures |
| +0x04 | int | m_PageCount | Number of atlas pages |
| +0x08 | QUADCUSTOMVERTEX** | m_pVertexData | Per-page vertex arrays |
| +0x0C | int* | m_pVertexCounts | Per-page vertex counts |
| +0x10 | float | m_Width | Baked string width |
| +0x14 | float | m_Height | Baked string height |

`BakedString::Draw` (0x0019738c) — iterates pages, binds texture, calls DrawTriList with pre-baked vertices. Very fast for static text like menu labels.

---

## Key Function Addresses

| Function | Address | Bytes | Path | Notes |
|----------|---------|-------|------|-------|
| Geometry::Render | 0x001a3e98 | ~300 | A | Core 3D draw: matrices + texture + PassBinding + glDraw |
| PassBinding::Apply | 0x001a39f8 | ~200 | A | GL 1.x vertex attribute setup |
| Mesh::Draw | (via Model::Draw) | — | A | Sets EffectProperties then calls Geometry::Render |
| DrawQuadUnCached | 0x00194060 | 73 | B | 4-vertex quad, 0x14 stride |
| DrawTriList/Strip | 0x00193f5c | ~100 | B | QUADCUSTOMVERTEX 0x24 stride |
| Font::DrawString | 0x00198e44 | ~500 | B | Full text with color tags, wrapping, alignment |
| BakedString::Draw | 0x0019738c | ~100 | B | Pre-baked text, fast repeated draw |
| Font::Load | 0x00199e9c | 270 | — | BMFont .fnt parser |

---

## GameDraw (0x16b888, 211 lines) — Full Frame Render Order

Decompiled and verified from binary. Called from `GameTaskDraw` when in State 2 (Game).

### Setup
```c
DisplayManager::SetDepthBuffer(false);
DisplayManager::SetDepthBufferWrite(false);
DisplayManager::SetDrawColour(Colour(64, 64, 64, 255));
DisplayManager::SetLightDirection(Vec3(g_GameData+0x90, g_GameData+0x94, 100.0));
FruitCamera::SetupPerspective(g_GameData->pCamera, 0, 0);
```

### Background Quad (bg_fruit_ninja.tex)

Texture from `g_TaskState+0xfc` (pBackgroundTexture). Two rendering paths:

**Normal (no shake):** `camera+0x144 == 0 && camera+0x148 == 0`
```
Texture::Set(pBackgroundTexture)
ResetMatrix
Scale(481.0, 321.0, 0.0)              // 1px oversize to prevent edge seams
Translate(0.0, 0.0, -5599.0)          // far back in Z
DrawQuad(UV: 0.03125, 0.96875, 0.1875, 0.8125)   // cropped — texture has border padding
```

**With shake:** `camera target != 0`
```
Scale(513.0, 361.0, 0.0)              // larger to cover screen during shake
Translate(0.0, 0.0, -5599.0)
DrawQuad(UV: 0.0, 1.0, 0.148, DAT)    // wider UV range
```

### Constant Pool (at 0x0016ba90)

| Address | Label | Value | Usage |
|---------|-------|-------|-------|
| 0x16ba90 | BG_LIGHT_DIR_Z | 100.0 | Light direction Z component |
| 0x16ba94 | BG_SCALE_X_NORMAL | 481.0 | Background quad width (normal) |
| 0x16ba98 | BG_SCALE_Y_NORMAL | 321.0 | Background quad height (normal) |
| 0x16ba9c | BG_ZERO | 0.0 | Zero constant |
| 0x16baa0 | BG_TRANSLATE_Z | -5599.0 | Background Z depth |
| 0x16baa4 | BG_UV_LEFT | 0.03125 | UV left edge (normal) |
| 0x16baa8 | BG_SCALE_X_SHAKE | 513.0 | Background quad width (shake) |
| 0x16baac | BG_SCALE_Y_SHAKE | 361.0 | Background quad height (shake) |

### Full Draw Order

| Step | Layer | What | Notes |
|------|-------|------|-------|
| 1 | — | FruitCamera::SetupPerspective(0, 0) | Ortho + LookAt |
| 2 | — | Background quad (pBackgroundTexture) | At Z=-5599, cropped UVs |
| 3 | — | LoadingJob::CanBoot check | If not ready: DrawStartFade + return |
| 4 | — | DisplayManager: depth on, draw colour | |
| 5 | — | ActorManager::Draw() | 3D fruit/bomb entities |
| 6 | — | DisplayManager: depth write on, depth off | |
| 7 | — | HUD::BeginDraw(dt) | |
| 8 | 0x40 | HUD::Draw(0x40) | |
| 9 | — | SplatEntity::DrawActiveSplats() | |
| 10 | — | Fruit::DrawShadows() | |
| 11 | — | SlashEntity::PreDraw() | Blade trail setup |
| 12 | — | BombBlast::DrawActiveBlasts() | |
| 13 | — | BombFlash::DrawActiveFlashes() | |
| 14 | 0x80 | HUD::Draw(0x80) | |
| 15 | — | PSPParticleManager::Draw(dt, active, -1) | All particle layers |
| 16 | — | SlashEntity::Draw() (×16 slash entities) | Blade trails |
| 17 | — | PSPParticleManager::Draw(dt, active, 0) | Layer 0 particles |
| 18 | — | DrawSlices(dt) | Slice effect rendering |
| 19 | 0x01 | HUD::Draw(0x01) | **MainScreen** (blurry_backing + logos) |
| 20 | — | PSPParticleManager::Draw(dt, active, 1) | Layer 1 particles |
| 21 | — | HUD scale reset to (1,1,1) | |
| 22 | — | WaveManager::Draw(0) | |
| 23 | 0x08 | HUD::Draw(0x08) | **Buttons** (Play, Dojo, toggles) |
| 24 | — | MainScreen::DrawPostEffects() | |
| 25 | — | DrawCritHit() | Fullscreen critical hit overlay |
| 26 | 0x100 | HUD::Draw(0x100) | |
| 27 | — | DrawBombHit() | Fullscreen bomb flash overlay |
| 28 | 0x200 | HUD::Draw(0x200) | |
| 29 | — | NetworkManager::DrawNews() | If showing modal dialog |
| 30 | — | DrawStartFade() | If fadeTimer > 0 |
| 31 | — | InputManager::ClearActions | Conditional on flags |
| 32 | 0x400 | HUD::Draw(0x400) | Top layer |

### GOT Offsets in GameDraw

| Offset | Label | Points to |
|--------|-------|-----------|
| DAT_0016bab8 | GOT_OFF_g_GameData | g_GameData pointer |
| DAT_0016bac4 | GOT_OFF_g_TaskState | g_TaskState pointer |

---

## Port Strategy

**Path A (3D):** Replace `Geometry::Render` + `PassBinding::Apply` with a single GLES2 shader that takes MVP matrix + diffuse texture as uniforms. The EffectProperty named lookup ("World", "ViewProjection", "DiffuseTexture") maps directly to shader uniforms.

**Path B (2D):** Already close to port-ready. Replace `glVertexPointer`/`glTexCoordPointer`/`glColorPointer` + `glDrawArrays` with `glVertexAttribPointer` + VBO in GLES2. Vertex layouts (0x14 and 0x24 stride) are well-defined.

**Font:** Reuse the same BMFont .fnt parser. Replace the glyph batching with GLES2 quads. BakedString optimization can be kept.

---

## See Also

- [Rendering detail](rendering-detail.md) — Model::Draw, TintColour pseudocode
- [Rendering functions](rendering-functions.md) — HUDControl3d::Draw pipeline
- [Asset formats](formats/models.md) — HBR0 container, vertex stream format
- [Matrix manager](matrix-manager.md) — Matrix stack used by both paths