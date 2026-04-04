# Rendering Functions

## Rendering

### HUDControl3d::Draw (0x0014428c, 57 lines)

```c
void HUDControl3d::Draw(float* tintScale) {
    if (!m_PauseTitleTex || m_Alpha == 0) return;
    
    Texture::Set(m_PauseTitleTex);
    MatrixStack::Reset();
    
    Matrix44 mat;
    Scale44(mat, size);
    if (m_Timer != 0) {
        float s = sin(m_Timer * ROT_SPEED);
        float c = cos(m_Timer * ROT_SPEED);
        RotZ44(mat, s, c);
    }
    Vec3 drawPos = pos + OFFSET * scale;
    GlobalTranslate44(mat, drawPos);
    SetCurrentMatrix(mat);
    UploadMatrices();
    
    Colour col = TintColour(m_DrawColour, tintScale);
    DrawQuadUnCached(col, m_UVLeft, m_UVRight, m_UVTop, m_UVBottom);
    
    Texture::UnSet(m_PauseTitleTex);
}
```

### Model::Draw (0x001930e0, 79 lines)

```c
void Model::Draw(const Matrix44& transform) {
    int meshCount = meshes.size();
    if (meshCount < 2) {
        meshes[0]->Draw(transform);  // single mesh: direct draw
    } else {
        // Multi-mesh: depth sort then draw
        Matrix44 viewProj = projection * transform;
        for (int i = 0; i < meshCount; i++) {
            Vec3 center = meshes[i]->GetBounds().Center();
            float z = (center * viewProj).z / (center * viewProj).w;
            sortArray[i] = {meshes[i], z};
        }
        qsort(sortArray, meshCount, 8, depthCompare);
        for (int i = 0; i < meshCount; i++)
            sortArray[i].mesh->Draw(transform);
    }
}
```

### TintWhite (0x00135af8, 52 lines)

| Address | Signature |
|---------|-----------|
| 0x00135af8 | `Colour TintWhite(float* rgb)` — converts float[3] (0-1) to packed BGRA |

### TintColour (0x0013540c, 53 lines)

| Address | Signature |
|---------|-----------|
| 0x0013540c | `Colour TintColour(Colour* colour, float* scale)` — multiplies RGB channels by scale |

### DrawQuadUnCached (0x00194060, 73 lines)

| Address | Signature |
|---------|-----------|
| 0x00194060 | `void Mesh::DrawQuadUnCached(Colour, float u0, float v0, float u1, float v1, DrawEffectContainer*)` |

### SetupQuad (0x0013929c, 78 lines)

| Address | Signature |
|---------|-----------|
| 0x0013929c | `int SetupQuad(QUADCUSTOMVERTEX*, Vec3 pos, float w, float h, Rect uv, Colour)` |

### AddQuad (0x00175db0, 67 lines)

| Address | Signature |
|---------|-----------|
| 0x00175db0 | `void AddQuad(QUADCUSTOMVERTEX**, float x, float y, float halfW, float halfH, Colour)` |

### SplatEntity::DrawActiveSplats (0x00180344, 45 lines)

| Address | Signature |
|---------|-----------|
| 0x00180344 | `void SplatEntity::DrawActiveSplats()` — batch draws via `Mesh::DrawTriList` |

### DrawSlices (0x00169ac8, 61 lines)

| Address | Signature |
|---------|-----------|
| 0x00169ac8 | `void DrawSlices(float dt)` — iterates `List<SliceEffect>`, draws animated slice models |

### DrawCritHit (0x0016b5b4, 72 lines)

| Address | Signature |
|---------|-----------|
| 0x0016b5b4 | `void DrawCritHit()` — full-screen critical hit flash |

### DrawBombHit (0x0016b73c)

| Address | Signature |
|---------|-----------|
| 0x0016b73c | `void DrawBombHit()` — full-screen bomb hit flash |

---

