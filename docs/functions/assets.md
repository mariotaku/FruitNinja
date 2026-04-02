# Texture & Asset Loading Functions

## Texture / Asset Loading

### GPUafyTexture (0x001898d8, 42 lines)

```c
void BadaTextureData::GPUafyTexture(TextureHeader* tex, int* texIdOut) {
    if (*texIdOut != -1) UnGPUafyTexture(texIdOut);
    
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);
    *texIdOut = texId;
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
    
    GLenum glFmt, glType;
    TexFmtToGL(tex->format, &glType, &glFmt);
    int w = 1 << tex->widthLog2;
    int h = 1 << tex->heightLog2;
    
    if (tex->format >= 0x0B && tex->format <= 0x0E)
        glCompressedTexImage2D(GL_TEXTURE_2D, 0, glFmt, w, h, 0, dataSize, tex + 12);
    else
        glTexImage2D(GL_TEXTURE_2D, 0, glFmt, w, h, 0, glFmt, glType, tex + 12);
}
```

### TexFmtToGL (0x00189f78, 53 lines)

| Format | GL Internal | GL Type |
|--------|------------|---------|
| 0x00 | GL_RGB | GL_UNSIGNED_BYTE |
| 0x01 | GL_RGBA | GL_UNSIGNED_BYTE |
| 0x10 | GL_RGBA | GL_UNSIGNED_SHORT_4_4_4_4 |
| 0x11 | GL_RGB | GL_UNSIGNED_SHORT_5_6_5 |

### LoadVertexStreamPSP (0x001a7b0c, 112 lines)

| Address | Signature |
|---------|-----------|
| 0x001a7b0c | `SmartPtr<IVertexStream> LoadVertexStreamPSP(ResourceLoader&)` |

See `docs/formats/models.md` for PSP vertex declaration bitfield.

### LoadIndexStreamPSP (0x001a799c, 91 lines)

| Address | Signature |
|---------|-----------|
| 0x001a799c | `SmartPtr<IIndexStream> LoadIndexStreamPSP(ResourceLoader&)` |

### GeometryBinding_Bada::PassBinding::Apply (0x001a39f8, 101 lines)

| Address | Signature |
|---------|-----------|
| 0x001a39f8 | `uint PassBinding::Apply()` — sets up glVertexPointer/NormalPointer/ColorPointer/TexCoordPointer |

---

