# Wii port -- scaffolding roadmap

Status: **scaffolding only**. Nothing here renders, plays audio, reads a
disc/SD file, or reads a Wiimote yet. This is the seam future work fills in.
Not built by default; see "Build gate" below.

## Approach

Same split the SDL/Emscripten backends already use: portable engine/game code
in `src/engine/`, `src/game/`, `src/entities/`, `src/hud/`, `src/screens/`,
`src/ui/` stays untouched. Only the platform boundary changes:

| Concern | SDL backend | Wii backend |
|---|---|---|
| Video/GL | OpenGL ES2 via SDL_GL + `gl_funcsSDL.cpp` | GX (libogc2), no GL at all -- see `gx/` |
| Input | SDL touch/mouse -> `InputTranslatorSDL` | WPAD IR -> `InputTranslatorWii` |
| Audio | SDL raw audio callback -> `SoundManagerSDL`-style backend | ASND/AESND -> `SoundManagerWii` |
| Filesystem | `FileSystem_Direct`/`IFile_Direct` (stdio) | same base classes, libfat FILE* -- see `FileSystemWii.cpp` |
| Entry point | `mainSDL.cpp` | `mainWii.cpp` |

The engine's draw API (`Renderer::DrawQuad`/`DrawTriList`/`DrawTriStrip`/
`DrawMesh3D` + font/particle wrappers, `src/engine/render/Renderer.h`) is the
**only** surface gameplay/screen/HUD code calls -- confirmed zero raw GL in
`src/game/`, `src/entities/`, `src/hud/`, `src/screens/`, `src/ui/`. That
means the Wii port needs a GX-backed `Renderer` implementation and nothing
else changes above the render API. `MatrixManager` (`src/engine/render/
MatrixManager.h`, `MatrixManager.cpp`) is pure math (row/col vectors + 4x4
multiply, no GL calls) and needs **no changes** -- only the site that
currently uploads the MVP via `glUniformMatrix4fv` swaps to writing GX
projection/modelview registers.

## GX render backend mapping

See `gx/README.md` for the file-by-file mapping. Summary: the ~14
render/asset files that own real GL calls today are:

- `src/engine/render/`: `Renderer.cpp`, `ShaderProgram.cpp`, `Shaders.cpp`,
  `DisplayManager.cpp` (+ `DisplayManagerSDL.cpp`), `MatrixManager.cpp`,
  `gl_funcs.h` (+ `gl_funcsSDL.cpp`/`gl_funcsWin32.cpp`), `Font.cpp`/
  `BakedString.cpp`/`BakedStringTTF.cpp`/`FancyBakedString.cpp` (font quad
  batching goes through `Renderer::DrawTriStrip`, no separate GL surface).
- `src/engine/asset/`: `Texture.cpp`, `ReloadableTexture.cpp`,
  `TextureManager.cpp`, `Geometry.cpp`, `Mesh.cpp`, `MeshManager.cpp`.

None of these files are edited by this scaffolding pass -- `gx/` holds
placeholder headers/stubs a future pass fills in, guarded so they only
compile under `FRUIT_PLATFORM_WII`.

## Platform backend pieces (this pass: stubs only)

- `mainWii.cpp` -- entry point. GX/VI init skeleton, `FixedStepDriver`-based
  fixed-timestep loop calling into `Game`, GX frame swap placeholder. Mirrors
  `mainEmscripten.cpp`'s structure (reuses the same `fn::FixedStepDriver`) but
  with no SDL window -- `Game::init(void*, void*)` is called with two null/
  placeholder opaque pointers since GX has no window-handle or GL-context
  concept.
- `InputTranslatorWii.h/.cpp` -- WPAD IR pointer -> `Mortar::Touch` finger
  channel seam. 4 Wiimotes = 4 IR pointers = 4 finger channels (the same
  per-finger multi-blade slicing the SDL backend drives from up to 8 touch
  channels -- see `CLAUDE.md`'s "Preserve simultaneous multi-finger slicing"
  policy). Each remote's IR dot maps to one fixed channel (remote 0 -> channel
  0, remote 1 -> channel 1, ...) -- no dynamic channel allocation needed since
  the Wii has a hard 4-remote ceiling versus SDL's dynamic finger-ID mapping.
- `SoundManagerWii.cpp` -- ASND/AESND backend for `Mortar::SoundManager`
  (same public API `SoundManagerSDL`/`SoundManagerWebAudio`-shaped backends
  implement: `Init`, `SFXPlay`/`Stop`/`Pause`/`Resume`/`SetVolume`,
  `SongPlay`/`Stop`/`Pause`/`Resume`, `SFXPauseAll`/`UnpauseAll`).
- `FileSystemWii.cpp`/`FileWii.cpp` -- libfat-backed filesystem. Subclasses
  the SAME `Mortar::FileSystem_Direct`/`Mortar::IFile_Direct` bases the
  Win32/Posix backends use (`src/engine/asset/FileSystem_Direct.h`,
  `IFile_Direct.h`) -- libfat exposes a POSIX-compatible `FILE*`/`fopen`
  surface, so the existing `stdio`-based `IFile_Direct` body is reusable
  as-is; the Wii files only need to own `fatInitDefault()` bring-up and pick
  the mount prefix (`sd:/` or `usb:/`).

## Endianness plan

Wii/PPC is **big-endian**; the original Bada binary and all RE'd struct
layouts/asset formats (`.tex`, `.mad`, `.mmd`, save XML numeric blobs, string
tables) are **little-endian** on-disk. Every binary file reader that does a
raw multi-byte load (not just `memcpy` into a struct with matching field
widths) needs an explicit byte-swap on Wii:

- `src/engine/asset/DataReader.cpp`/`DataStreamReader.cpp`/
  `FileDataReader.cpp`/`VectorDataReader.cpp` -- the actual `ReadInt32`/
  `ReadFloat`/`ReadShort`-style primitive readers are the single choke point;
  gate a byteswap there behind a platform macro rather than touching every
  call site.
- `src/engine/util/StringHash.cpp`/`StringTable.cpp` -- hash values are
  computed at runtime from string bytes (endian-neutral), but *serialized*
  hash tables read as raw `uint32_t` blobs need the same swap.
- Texture/mesh binary blobs (`TextureFileFormat.cpp`, `Mesh.cpp`) -- decoded
  through the same `DataReader` primitives, so fixing the choke point above
  covers them for free.
- Runtime-only structs (in-memory game state, no on-disk representation) need
  **no** swapping -- only data that crosses the file I/O boundary.

No swap code is added in this pass; this is the plan for the pass that adds
`FileSystemWii.cpp`'s actual read path.

## Toolchain / build steps (future, once devkitPPC is available)

1. Install devkitPro + devkitPPC + libogc2 (`$DEVKITPRO`, `$DEVKITPPC` env vars
   set by the devkitPro installer/pacman).
2. Configure: `cmake --preset wii` (or manually
   `cmake -DCMAKE_TOOLCHAIN_FILE=cmake/wii.toolchain.cmake -DFRUIT_PLATFORM_WII=ON -B build/wii`).
3. Build: `cmake --build build/wii` -- produces `fruit-ninja.dol` (or `.elf`
   if `.dol` conversion is deferred).
4. Run: `.dol`/`.elf` on real hardware (SD card + Homebrew Channel) or in
   Dolphin.

This CANNOT be exercised in this repo/session -- no devkitPPC install here.
The toolchain file and CMake option exist so a machine that DOES have
devkitPro can pick up the Wii sources; every other build (host, web,
asm-verify) is completely unaffected -- see the CMake guard note in
`cmake/wii.toolchain.cmake` and `src/platform/CMakeLists.txt`.

## Files in this directory

- `mainWii.cpp` -- entry point stub.
- `InputTranslatorWii.h` / `.cpp` -- WPAD IR -> Touch stub.
- `SoundManagerWii.cpp` -- ASND audio backend stub.
- `FileSystemWii.cpp` / `FileWii.cpp` -- libfat filesystem stub.
- `gx/` -- GX render backend placeholder (see `gx/README.md`).
- `CMakeLists.txt` -- Wii-only source list, included only when
  `FRUIT_PLATFORM_WII` is set (see `src/platform/CMakeLists.txt`).
