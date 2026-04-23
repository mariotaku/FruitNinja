# Mortar Engine Implementation TODO

Comprehensive task list for implementing the `mortar_engine` static library.
Reference docs are in `docs/engine/` (19 files). All struct layouts, method signatures,
and behavior are documented from RE of the original ARM32 binary.

## Status

All phases (1-8) are implemented. See details below for completion status and remaining items.

### Phase 1 — Foundation ✅ DONE

Files in `core/`, `math/`, `util/`:

- [x] `util/StringHash.h/.cpp` — Jenkins lookup3 hash
- [x] `util/ReferenceCounter.h` — Intrusive refcount base (12B)
- [x] `util/SmartPtr.h` — 4-byte intrusive smart pointer
- [x] `util/Delegate.h` — Delegate0-4 via std::function
- [x] `util/NLFQueue.h` — SPSC ring buffer (16B)
- [x] `util/AsciiString.h` — String wrapper
- [x] `util/List.h` — Mortar List<T> template
- [x] `math/Vec3.h` — _Vector3<float> (12B)
- [x] `math/Matrix44.h` — Column-major 4x4 + OrthoW, GlobalTranslate44, LocalTranslate44, Scale44
- [x] `math/Quaternion.h` — Quaternion with FromAxisAngle, ToMatrix44
- [x] `math/Colour.h` — BGRA packed + TintWhite/TintColour
- [x] `math/Random.h/.cpp` — 64-bit LCG (Knuth MMIX)
- [x] `math/MathUtil.h` — Clamp, Lerp, SinIdx, CosIdx
- [x] `math/math3d.h` — Standalone float[16] helpers
- [x] `core/Singleton.h` — Meyers singleton template
- [x] `core/MortarTypes.h` — FN_SCREEN_W/H, MortarRectangle
- [x] `core/SystemManager.h/.cpp` — FPS ring buffer, quit lifecycle

**Build note:** Phase 1 has NOT been build-tested yet (MSYS2 env issue). Test on Linux first.

---

### Phase 2 — Core Singletons ✅ DONE

Ref: `docs/engine/matrix-manager.md`, `docs/engine/display-manager.md`

- [x] `render/gl_funcs.h/.cpp` — Migrated + extended with glScissor, glGetError, glPixelStorei, glCompressedTexImage2D
- [x] `render/MatrixStack.h` — Full 32-deep stack (2120 bytes), version-tracked, Push/Pop/Scale/Translate
- [x] `render/MatrixManager.h/.cpp` — 4 MatrixStacks + dirty upload, singleton, SetupOrtho/LookAt/GetMVP
- [x] `render/DisplayManager.h/.cpp` — GL state singleton, BeginFrame/EndFrame/SwapBuffers/SetDrawColour
- [x] `asset/FileManager.h` — Stub header-only, no-op AddSystem/RemoveSystem

---

### Phase 3 — Rendering ✅ DONE

Ref: `docs/engine/rendering-pipeline.md`, `docs/engine/rendering-detail.md`

- [x] `render/QUADCUSTOMVERTEX.h` — QUADCUSTOMVERTEX (36B) + QuadVertex (20B) with static_assert
- [x] `render/Renderer.h/.cpp` — Migrated + expanded: 3 shaders (2D tint, 3D mesh, 2D vertex-color), DrawTriList/DrawTriStrip, MatrixManager singleton (no longer owned by Renderer)
- [x] `render/MortarCamera.h/.cpp` — Camera with SetupOrtho/SetupLookAt/SetupPerspective via MatrixManager
- [x] `collision/ColAABB.h` — AABB with AABB-AABB, AABB-Sphere, AABB-Line tests
- [x] `collision/ColLine.h` — Line segment with closest-point helpers
- [x] `collision/ColSphere.h` — Sphere with Sphere-Sphere, Sphere-Line, Contains tests

---

### Phase 4 — Asset Pipeline ✅ DONE

Ref: `docs/engine/texture-mesh-manager.md`, `docs/engine/utility-types.md`, `docs/engine/formats/`

- [x] `asset/tex_loader.h/.cpp` — Migrated + fixed RGBA8888/RGB888 format support
- [x] `asset/Texture.h/.cpp` — Texture2D with ReferenceCounter, Set/UnSet, Load (native GL upload), UploadRGBA/UploadNative
- [x] `asset/TextureManager.h/.cpp` — Singleton cache keyed by StringHash, Load/Find/Add/PurgeExpired
- [x] `asset/ResourceLoader.h/.cpp` — HBR0 container parser, recursive children, sequential Read/ReadString/ReadSubResourceLookup
- [x] `asset/Mesh.h/.cpp` — MortarMesh (VBO/IBO, VertexLayout, Draw) + Model (vector<SmartPtr<MortarMesh>>, depth-sorted Draw)
- [x] `asset/MeshManager.h/.cpp` — List-based cache, LoadVertexStreamPSP/LoadIndexStreamPSP parsing
- [ ] `asset/AnimationManager.h/.cpp` — List<Animation*> cache (20 bytes), low priority

---

### Phase 5 — Input ✅ DONE

Ref: `docs/engine/input-manager.md`, `docs/engine/touch-system.md`

- [x] `input/InputEvent.h` — Event struct with actionHash, actionFlags, fingerId, x/y, deltaX/Y
- [x] `input/InputManager.h` — Action-hash callback dispatch (header-only, matches existing src/platform/)
- [x] `input/Touch.h/.cpp` — Double-buffered 8-slot multitouch with 10-entry ring buffer
- [ ] `input/SDLInputTranslator.h/.cpp` — Not yet migrated (still at src/platform/, working)

---

### Phase 6 — Audio ✅ DONE

Ref: `docs/engine/sound-system.md`, `docs/engine/audio-internals.md`

- [x] `audio/MortarSound.h/.cpp` — Sound instance (16B): m_Name, m_Handle, m_State, Play/Pause/Stop/SetVolume
- [x] `audio/SoundManager.h/.cpp` — Abstract base singleton with static volume globals, virtual SFX/Music API
- [x] `audio/GameSound.h/.cpp` — 32-slot pool with FindFree, SFXPlay (volume formula), IsPlaying, Release, KillAll
- [ ] `audio/SoundManagerSDL.h/.cpp` — SDL_AudioCallback backend (stub — SoundManager base class has virtual stubs)

---

### Phase 7 — Entities & Particles ✅ DONE

Ref: `docs/engine/actor-manager.md`, `docs/engine/particles.md`

- [x] ~~`entity/Entity.h/.cpp`~~ — removed; the game-namespace `::Entity` in `src/entities/Entity.h` is the active port
- [x] ~~`entity/ActorManager.h/.cpp`~~ — removed; see `src/entities/ActorManager.{h,cpp}` for the binary-faithful port with free pool, per-type lists, factory delegate
- [x] `particle/PSPParticleManager.h/.cpp` — Singleton with PSPParticleEmitter struct, AddEmitter/Update/Draw/LoadFile stubs
- [ ] Particle rendering — template loading and particle quad generation not yet implemented

---

### Phase 8 — Font ✅ DONE

Ref: `docs/engine/rendering-pipeline.md` (Font System section)

- [x] `render/Font.h/.cpp` — BMFont .fnt loader, 256 glyphs, DrawString with [FFFFFF] color tags, word wrap, alignment, per-page vertex batching
- [x] `render/BakedString.h/.cpp` — Pre-baked vertex cache with per-page PageData, Draw via GL vertex arrays
- [ ] BakedString::Bake vertex capture — currently stubbed, falls back to Font::DrawString

---

## CMakeLists.txt

Current `src/engine/CMakeLists.txt` defines `mortar_engine` with all Phase 1-8 sources (23 .cpp files).

Legacy `fn_engine` target is kept temporarily for game-level `Mesh.cpp` (the HBR0 mesh loader used by Fruit/DojoScreen).
Old headers at `src/engine/` root (`gl_funcs.h`, `MatrixManager.h`, `Renderer.h`, `tex_loader.h`) are forwarding headers.
Old `src/math/` headers forward to `src/engine/math/` — canonical math types live in the engine.

## Migration Checklist

When the engine library is complete, update game code:

- [x] Remove `add_subdirectory(src/math)` and `add_subdirectory(src/platform)` from root CMakeLists.txt
- [x] Change all game libs to link `mortar_engine` instead of `fn_engine fn_math fn_platform`
- [x] Update game includes: `#include "Vec3.h"` → `#include "math/Vec3.h"`
- [ ] Update game includes: `#include "Renderer.h"` → `#include "render/Renderer.h"`
- [ ] Update game includes: `#include "InputManager.h"` → `#include "input/InputManager.h"`
- [ ] Move singletons out of Game struct into engine GetInstance() calls
- [ ] Delete old `src/math/`, `src/platform/`, old files in `src/engine/`
- [ ] Remove legacy `fn_engine` target from CMakeLists.txt

## Key Design Notes

1. **SetupOrtho param order**: `(top, bottom, left, right, near, far)` — NOT standard GL `(left, right, bottom, top)`. The game calls `SetupOrtho(160, -160, -240, 240, 2000, -6000)`.

2. **GLES2 translation**: Original uses GL ES 1.x fixed-function (glMatrixMode, glVertexPointer, etc.). Port uses shaders. MatrixManager computes MVP and stores as uniform. Renderer manages shader programs and attribute binding.

3. **Two rendering paths**: Path A (3D) goes through Effect/Geometry/PassBinding. Path B (2D) is immediate-mode DrawQuad/DrawTriList. For the port, both use Renderer's GLES2 shaders.

4. **SmartPtr is intrusive**: Objects inherit ReferenceCounter. NOT std::shared_ptr.

5. **Delegate wraps std::function**: Match original API (Delegate0-4, QCallee<T> factory) but use std::function internally. Don't replicate binary layout.

6. **Original centered ortho**: All positions use centered coordinates. X: +160 (top) to -160 (bottom). Y: -240 (left) to +240 (right). No conversion needed.
