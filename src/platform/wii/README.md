# Wii port (GX / libogc)

Status: **builds + links to a bootable `fruit-ninja.dol`** (PowerPC, entry
`0x80003f00`). Boots the engine + game; input is live (Wii Remote IR). Rendering
(GX draws) and audio are early — see "State" below. `-DFRUIT_PLATFORM_WII=ON`;
every other build (host/web/asm-verify) is unaffected.

## Approach — GL-on-GX shim (not a separate GX Renderer)

The engine was migrated to hand-written ES2 shaders, and the core render TUs
(`Renderer.cpp`, `DisplayManager.cpp`, `Texture.cpp`, `ShaderProgram.cpp`,
`Shaders.cpp`, `MeshManager.cpp`, …) call GL through `gl_compat.h`. Rather than
fork all of them, the Wii backend implements **GL on top of GX**:

- `src/engine/render/gl_compat.h` — a `FRUIT_PLATFORM_WII` branch defines the GL
  types + standard-value constants + `gl*` prototypes (no `SDL_opengl.h`).
- `src/engine/render/gl_funcsWii.cpp` — the shim. `gl*` are GX-backed:
  - **real**: state (blend/cull/depth via `GX_Set*`), viewport/scissor, textures
    (`glTexImage2D` → linear→`GX_TF_RGBA8` tiled `GX_TexObj`), buffers (CPU copy
    kept), `glUniformMatrix4fv`/`glVertexAttribPointer` recorded for the draw.
  - **draws**: `glDrawArrays`/`glDrawElements` → GX immediate mode. The recorded
    ES2 MVP is loaded as the GX projection (`GX_PERSPECTIVE`, full 4×4) with an
    identity pos-matrix, reproducing `clip = MVP·pos`; TEV = `GX_MODULATE` when
    textured else `GX_PASSCLR`.
  - **no-op**: the shader-object calls (`glCreateShader`/`glUseProgram`/…) — GX
    has no GLSL, so `ShaderProgram`/`Shaders` compile but do nothing; the draw
    path above replaces them.

So the ~14 GL-owning render/asset files compile **unchanged**; the swap is
confined to the shim. `MatrixManager` is pure math, reused as-is.

## Platform pieces

| Concern | File | State |
|---|---|---|
| Entry + loop | `mainWii.cpp` | real: VIDEO/GX/WPAD/libfat init, fixed-step loop, HOME-quit |
| Video swap | `../../engine/render/DisplayManagerWii.cpp` | real: `GX_CopyDisp` + VIDEO present |
| Video seam | `WiiVideo.h` | shared XFB/rmode between mainWii + DisplayManagerWii |
| Input | `InputTranslatorWii.{h,cpp}` | **real**: WPAD IR pointer → `Mortar::Touch` finger (remote 0..3 → channel 1..4), shared `Layout::TouchToGame` transform |
| Game glue | `../../GameWii.cpp` | real: SDL-free twin of `GameSDL.cpp` |
| Licensing | `../../game/LicensingWii.cpp` | stub twin (defunct `OpenBrowser`) |
| Audio | `SoundManagerWii.cpp` | **stub** — ASND/AESND not wired yet (no sound) |
| Filesystem | `FileSystemWii.{cpp,h}`, `FileWii.h` | libfat over the `FileSystem_Direct`/`IFile_Direct` bases |

## Endianness

Wii/PPC is **big-endian**; assets are **little-endian** on disk.
`src/engine/util/Endian.h` defines `FN_BIG_ENDIAN` when `FRUIT_PLATFORM_WII`
is set (or on any `__BYTE_ORDER__`-big-endian target) and provides the
`Endian::fnByteSwap16/32/64/Float` primitives; undefined (dead code) on
host/web/asm-verify.

Byteswaps are gated behind `FN_BIG_ENDIAN` at each native-load choke point:
`DataReader::ReadLE<T>`, `DataStreamReader::ReadRaw`/`ReadBasicType` (already
had a binary-modelled swap keyed on `Endian::GetEndian()`; just needed
`GetEndian()` to return `BIG`), `StringTable::LoadHeader`/`LoadLanguage`,
`TextureFileFormat.cpp`'s Tex2/DDS/Tex3 header + FourCC reads, and
`MeshManager.cpp`'s PSP vertex/index stream header fields. Readers that
already assembled values byte-by-byte (explicit LE assembly) needed no
change; see per-reader notes in each file.

16-bit texture formats (RGBA5551/RGBA4444/RGB565) are the pixel-path case:
`Texture.cpp` swaps the raw pixel buffer's `uint16_t` texels on
`FN_BIG_ENDIAN` before GL/GX upload -- this is live on Wii, since
`gl_funcsWii.cpp`'s `ExpandToRGBA8` does its own native `uint16_t` read of
whatever `glTexImage2D` hands it. RGB888/RGBA8888 are plain byte streams, no
swap needed.

The GX vertex/index buffer bytes uploaded by `MeshManager.cpp`'s PSP stream
loaders are also swapped on `FN_BIG_ENDIAN`: `gl_funcsWii.cpp`'s `EmitVertex`/
`glDrawElements` read the uploaded buffer as native `float` (tex/normal/pos
slots) and native `uint16_t` (indices), so `LoadVertexStreamPSP`/
`LoadIndexStreamPSP` byteswap those components in a CPU-side copy before
`glBufferData`. The packed RGBA colour slot is a plain byte array and is
left untouched.

## Assets — uncompressed

Wii ships **raw Tex1 textures + raw `.wav.pcm` audio** (no WebP/OGG transcode);
`tools/assets/stage-assets.py --wii` does a verbatim copy. TTF glyphs use
**stb_truetype** (`-DFN_TTF_BACKEND=stb`, forced for Wii), no FreeType.

## Build (Windows host)

devkitPPC + libogc are installed via MSYS2 pacman. Configure MUST use the **MSYS
cmake** (devkitPro's `Wii.cmake` rejects Windows/mingw cmake). Full recipe +
gotchas: `tmp/wii/CONFIGURE-RECIPE.md`. In short:

```sh
MSYSTEM=MSYS C:/msys64/usr/bin/bash.exe -lc '
  export MSYSTEM=MSYS; mkdir -p /c/msys64/tmp; cd <repo>; rm -rf build/wii
  /usr/bin/cmake -B build/wii -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE=cmake/wii.toolchain.cmake \
    -DPython3_EXECUTABLE=/usr/bin/python3 -DFRUIT_PLATFORM_WII=ON
  /usr/bin/cmake --build build/wii -j6'
# -> build/wii/fruit-ninja.elf ; elf2dol -> fruit-ninja.dol
```

`cmake/wii.toolchain.cmake` wraps devkitPro's `Wii.cmake` + a Windows/MSYS2 shim
(MSYS `make` drops `TEMP` for the native gcc). **Incremental builds break**
(MSYS make chokes on Windows-path depfiles → "multiple target patterns") — always
`rm -rf build/wii` for a clean build.

## Deploy / boot

SD layout (the build bakes `FN_DATA_DIR = sd:/apps/fruitninja/Data`):

```
sd:/apps/fruitninja/boot.dol      (renamed from fruit-ninja.dol)
sd:/apps/fruitninja/meta.xml      (Homebrew Channel metadata, optional)
sd:/apps/fruitninja/Data/…        (build/wii/staging/Data)
```

Launch via the Homebrew Channel, or open `boot.dol` in Dolphin with an SD card
mounted. Point with the Wii Remote to slice; HOME quits.

## State / remaining

- ✅ compiles + links; ✅ VIDEO/GX/input/fat init; ✅ GX draw path; ✅ endianness gate.
- ⏳ **rendering unvalidated** — the GX draw math (matrix transpose, projection
  type, vertex emit order, color byte order) needs a Dolphin/hardware boot to
  confirm; risks are listed at the top of `gl_funcsWii.cpp`.
- ⏳ **audio** — `SoundManagerWii` is a stub (ASND wiring pending).
- ⏳ Korean/Hangul renders degraded under stb (host/web unaffected).
