# GX render backend -- placeholder mapping

Not implemented. This directory documents, per existing render/asset file,
what its GX-backed equivalent needs to do. A future pass creates
`*Gx.cpp`-suffixed companions (mirroring the `*SDL.cpp`/`*Win32.cpp`
convention) or `#ifdef FRUIT_PLATFORM_WII` branches inside the existing
files where the logic is genuinely shared.

No existing file under `src/engine/render/` or `src/engine/asset/` is
modified by this scaffolding pass.

## Mapping table

| Existing file | GL today | GX equivalent |
|---|---|---|
| `Renderer.cpp` (`DrawQuad`/`DrawTriList`/`DrawTriStrip`/`DrawMesh3D`) | builds vertex buffers, binds `ShaderProgram`, issues `glDrawArrays`/`glDrawElements` | build the same vertex data into a GX display list (`GX_Begin`/`GX_End` immediate mode, or a pre-baked display list for static geometry), submit via `GX_CallDisplayList` |
| `ShaderProgram.cpp` / `Shaders.cpp` (GLES2 modulate/unlit shaders) | compiles+links GLSL, `glUseProgram`, uniform upload | `GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE)` + `GX_SetNumTevStages(1)` reproduces the modulate blend (`texture2D * v_color`); no shader compilation step exists on GX, TEV stages are configured directly, once, at init |
| `DisplayManager.cpp` / `DisplayManagerSDL.cpp` (`glClear`, `glBlendFunc`, `glViewport`, swap) | GL state setup + `SDL_GL_SwapWindow` | `GX_SetCopyClear`, `GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR)`, `GX_SetViewport`, frame swap via `GX_CopyDisp` + `VIDEO_WaitVSync` (see `mainWii.cpp`) |
| `MatrixManager.cpp` | pure math; only the upload site (`glUniformMatrix4fv`) touches GL | **unchanged** -- only the call site swaps to `GX_LoadProjectionMtx`/`GX_LoadPosMtxImm` with the same computed matrix |
| `gl_funcs.h` / `gl_funcsSDL.cpp` / `gl_funcsWin32.cpp` (GL function pointer loading) | resolves GL entry points at runtime | not needed at all -- libogc2's GX API is linked directly, no extension-loading step |
| `Font.cpp` / `BakedString.cpp` / `BakedStringTTF.cpp` / `FancyBakedString.cpp` | batches glyph quads, draws via `Renderer::DrawTriStrip` | **unchanged call site** -- once `DrawTriStrip` has a GX backend these get it for free |
| `Texture.cpp` / `ReloadableTexture.cpp` / `TextureManager.cpp` | `glGenTextures`/`glTexImage2D`, `GLuint` handle | `GX_InitTexObj` + `GX_LoadTexObj`; texture handle type becomes `GXTexObj` instead of `GLuint` -- the `Texture` class's public handle accessor needs a platform-typed member (or a `void*` opaque slot per the platform-header rule) |
| `Geometry.cpp` / `Mesh.cpp` / `MeshManager.cpp` | VBO/IBO (`glGenBuffers`, `glBufferData`), vertex attrib pointers | GX vertex descriptor (`GX_SetVtxDesc`, `GX_SetVtxAttrFmt`) + either a display list or direct-mode array submission (`GX_SetArray`); no buffer-object equivalent, GX reads directly from main memory (cache-flushed via `DCFlushRange`) |

## Notes for the future pass

- GX has no shader concept -- all "shading" is TEV stage configuration done
  once at init, not per-draw-call program binding. `ShaderProgram.cpp`'s
  abstraction (a named program + uniform set) likely collapses to a thin
  TEV-state-setter with no per-frame compile/link cost.
- Texture upload needs a format conversion step: GX wants tiled/swizzled
  texture formats (`GX_TF_RGBA8`, `GX_TF_CMPR`, etc.), not the linear RGBA8
  the WebP/`.tex` decode path produces today. That conversion is a genuinely
  new function, not a call-site swap.
- All GX calls happen between `GX_InvalidateTexAll`/frame setup and
  `GX_DrawDone()`/`GX_CopyDisp()` -- there is no "GL context" to create, so
  `mainWii.cpp`'s init differs structurally from `mainSDL.cpp`'s
  `SDL_GL_CreateContext` step (see that file's `// TODO(wii):` markers).
