# .tex Texture Format

## File Layout

```
+0x00: byte   widthLog2      (width = 1 << widthLog2)
+0x01: byte   heightLog2     (height = 1 << heightLog2)
+0x02: byte   format         (pixel format enum, see table below)
+0x03: byte   padding        (always 0)
+0x04: ushort width          (little-endian, redundant with log2)
+0x06: ushort height         (little-endian, redundant with log2)
+0x08: uint   sentinel       (0xFFFFFFFF)
+0x0C: byte[] pixelData      (width * height * bpp/8 bytes)
```

**Header size: 12 bytes.** Pixel data starts at offset 0x0C.

Total file size = 12 + width * height * bytesPerPixel.

## Pixel Format Table (from TexFmtToGL, 0x189f78)

| Format | GL Internal | GL Type | Description |
|--------|------------|---------|-------------|
| 0x00 | GL_RGB (0x1907) | GL_UNSIGNED_BYTE (0x1401) | RGB888 — 3 bytes/pixel |
| 0x01 | GL_RGBA (0x1908) | GL_UNSIGNED_BYTE (0x1401) | RGBA8888 — 4 bytes/pixel |
| 0x0B | GL_RGBA | COMPRESSED_RGBA_PVRTC_2BPP (0x8C03) | PVRTC 2bpp RGBA |
| 0x0C | GL_RGBA | COMPRESSED_RGBA_PVRTC_4BPP (0x8C02) | PVRTC 4bpp RGBA |
| 0x0D | GL_RGB | COMPRESSED_RGB_PVRTC_2BPP (0x8C01) | PVRTC 2bpp RGB |
| 0x0E | GL_RGB | COMPRESSED_RGB_PVRTC_4BPP (0x8C00) | PVRTC 4bpp RGB |
| 0x0F | GL_RGBA | GL_UNSIGNED_SHORT_8_8_8_8 (0x8034) | RGBA8888 packed short |
| **0x10** | **GL_RGBA (0x1908)** | **GL_UNSIGNED_SHORT_4_4_4_4 (0x8033)** | **RGBA4444 — 2 bytes/pixel** |
| **0x11** | **GL_RGB (0x1907)** | **GL_UNSIGNED_SHORT_5_6_5 (0x8363)** | **RGB565 — 2 bytes/pixel** |

### Formats Used in FruitNinjaBada

| Format | Used by | Textures |
|--------|---------|----------|
| **0x10 (RGBA4444)** | Sprites with alpha | apple.tex, arcade_60seconds.tex, word_freeze.tex |
| **0x11 (RGB565)** | Opaque backgrounds | bg_fruit_ninja.tex, bg_greatwave.tex |

Compressed PVRTC formats (0x0B-0x0E) may be used on iOS builds but not in this Bada build.

## GPU Upload (from GPUafyTexture, 0x1898d8)

```c
glGenTextures(1, &texId);
glBindTexture(GL_TEXTURE_2D, texId);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);

if (format >= 0x0B && format <= 0x0E) {
    // Compressed texture (PVRTC)
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, glFormat,
        1 << widthLog2, 1 << heightLog2, 0, dataSize, data + 12);
} else {
    // Uncompressed texture
    glTexImage2D(GL_TEXTURE_2D, 0, glInternal,
        1 << widthLog2, 1 << heightLog2, 0, glInternal, glType, data + 12);
}
```

## Conversion to PNG

For the port, convert at build time:
- **0x10 (RGBA4444)**: Expand each 16-bit pixel to 32-bit RGBA (multiply each 4-bit channel by 17)
- **0x11 (RGB565)**: Expand R5G6B5 to RGB888 + opaque alpha

```c
// RGBA4444 → RGBA8888
uint16_t pixel = *(uint16_t*)(data + 12 + i*2);
uint8_t r = ((pixel >> 12) & 0xF) * 17;
uint8_t g = ((pixel >> 8)  & 0xF) * 17;
uint8_t b = ((pixel >> 4)  & 0xF) * 17;
uint8_t a = ((pixel >> 0)  & 0xF) * 17;

// RGB565 → RGBA8888
uint16_t pixel = *(uint16_t*)(data + 12 + i*2);
uint8_t r = ((pixel >> 11) & 0x1F) * 255 / 31;
uint8_t g = ((pixel >> 5)  & 0x3F) * 255 / 63;
uint8_t b = ((pixel >> 0)  & 0x1F) * 255 / 31;
uint8_t a = 255;
```
