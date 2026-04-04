# Mortar Engine Implementation TODO

Comprehensive task list for implementing the `mortar_engine` static library.
Reference docs are in `docs/engine/` (19 files). All struct layouts, method signatures,
and behavior are documented from RE of the original ARM32 binary.

## Status

Phase 1 (Foundation) is implemented. Phases 2-8 are TODO.

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

### Phase 2 — Core Singletons (depends on Phase 1)

Ref: `docs/engine/matrix-manager.md`, `docs/engine/display-manager.md`

- [ ] `render/gl_funcs.h/.cpp` — Migrate from existing `src/engine/gl_funcs.*`. GL function pointer loading for GLES2.
- [ ] `render/MatrixStack.h/.cpp` — 32-deep matrix stack (2120 bytes)
  - `_Matrix44<float> m_Stack[32]` at +0x000
  - `_Matrix44<float> m_Current` at +0x800
  - `int m_Depth` at +0x840, `int m_Version` at +0x844
  - Methods: `Reset()`, `Scale(Vec3)`, `Translate(Vec3)`, `SetCurrentMatrix(Matrix44)`
  - Every mutation increments `m_Version` (dirty-tracking)
- [ ] `render/MatrixManager.h/.cpp` — 4 MatrixStacks + dirty upload (8500 bytes)
  - Stacks: `m_Projection` (+0x04), `m_View` (+0x84C), `m_World` (+0x1094), `m_Texture` (+0x18DC)
  - Version counters at +0x2124..+0x2130
  - `SetupOrtho(top, bottom, left, right, near, far)` — NOTE: non-standard param order!
  - `SetupLookAt(eye, target, up)`
  - `UploadCurrentMatrices(bool forceProjection)` — computes MVP, uploads as shader uniform
  - `ResetAllStacks()`, `GetWorldStack()`, `GetProjectionStack()`
  - For GLES2: the "upload" stores the combined MVP for shader use, not glMatrixMode
- [ ] `render/DisplayManager.h/.cpp` — GL state singleton (148 bytes)
  - Fields: vtable, m_ClearColor, m_DrawColor, m_WindowRect, m_lightDirection, m_GlobalAmbience, m_bRenderingActive, m_bSwapPending, m_TextureOverloadPrefix, filter modes, m_ScreenRotationMatrix
  - `BeginFrame()` — glClear, glEnable blend, set up rotation
  - `EndFrame()` — clear rendering flag
  - `SwapBuffers()` — SDL_GL_SwapWindow
  - `SetDrawColour(Colour)` — shader uniform (replaces glColor4ub)
  - `SetDepthBuffer(bool)`, `SetDepthBufferWrite(bool)`
  - `GetWindowSize()` returns MortarRectangle
  - Vtable: 20 entries (see `docs/engine/vtables.md`)
- [ ] `asset/FileManager.h` — Stub header-only. `AddSystem()`/`RemoveSystem()` are no-ops. Port uses direct file I/O.

---

### Phase 3 — Rendering (depends on Phase 2)

Ref: `docs/engine/rendering-pipeline.md`, `docs/engine/rendering-detail.md`

- [ ] `render/QUADCUSTOMVERTEX.h` — Vertex struct (36 bytes: 3 float pos, 2 float uv, uint32 colour, 3 float normal)
  - Also define compact QuadVertex (20 bytes: 2 float pos, 2 float uv, uint32 colour) for DrawQuadUnCached
- [ ] `render/Renderer.h/.cpp` — GLES2 shader programs + draw functions
  - Two shaders: 2D (MVP + tint uniform) and 3D (MVP + model + light + alpha)
  - `DrawQuad(Colour, u0, v0, u1, v1)` — builds 4-vertex quad, stride 0x14
  - `DrawTriList(QUADCUSTOMVERTEX*, count, bool isStrip)` — stride 0x24
  - `DrawTriStrip(QUADCUSTOMVERTEX*, count)`
  - Migrate and expand from existing `src/engine/Renderer.*`
  - MatrixManager is now a separate singleton, not owned by Renderer
- [ ] `render/MortarCamera.h/.cpp` — Camera with ortho/perspective setup
  - `SetupOrtho()` — gets window size, calls `MatrixManager::SetupOrtho(height/2, -height/2, -width/2, width/2, -1, far)`
  - `SetupLookAt()` — calls `MatrixManager::SetupLookAt(eye, target, up)`
  - Stores view matrix copy at camera offset +0x74 (Matrix43) and projection copy at +0xA4 (Matrix44)
  - Ref: `docs/engine/camera.md`
- [ ] `collision/ColAABB.h` — Axis-aligned bounding box
- [ ] `collision/ColLine.h` — Line segment
- [ ] `collision/ColSphere.h` — Sphere
  - All three: struct + intersection tests (ColAABBLine, ColAABBSphere, ColLineLine, ColSphereSphere)

---

### Phase 4 — Asset Pipeline (depends on Phase 2+3)

Ref: `docs/engine/texture-mesh-manager.md`, `docs/engine/utility-types.md`, `docs/engine/formats/`

- [ ] `asset/tex_loader.h/.cpp` — Migrate from existing `src/engine/tex_loader.*`. Parses .tex files (12-byte header: format, width, height), converts RGBA4444/RGB565 to GL texture.
- [ ] `asset/Texture.h/.cpp` — Texture2D class inheriting ReferenceCounter
  - GL texture handle, width, height
  - `Set()` / `UnSet()` — bind/unbind
  - `static SmartPtr<Texture> Load(const char* path)` — factory via tex_loader
- [ ] `asset/TextureManager.h/.cpp` — Singleton, `std::map<uint32_t, WeakPtr<Texture>>` cache (24 bytes)
  - `Load(const char* path)` — StringHash → Find → if miss: Texture::Load → Add → return SmartPtr
  - `Find(uint32_t hash)` / `Find(const char* name)`
  - `Add(uint32_t hash, SmartPtr<Texture>)`
  - Ref: Loading flow in `docs/engine/texture-mesh-manager.md`
- [ ] `asset/ResourceLoader.h/.cpp` — HBR0 container parser (68 bytes)
  - Recursive: reads child count, constructs child ResourceLoaders
  - `RegisterLoader<T>(Delegate1<SmartPtr<T>, ResourceLoader&>)` — typed callback in global map
  - `Load<T>(name)` — opens file, initializes, calls registered loader
  - Ref: `docs/engine/utility-types.md`
- [ ] `asset/Model.h` — Model with vector<SmartPtr<Mesh>>, depth-sorted Draw
- [ ] `asset/Mesh.h` — Mesh with GeometryBinding, PassBinding, EffectProperties
  - Simplified for port: store vertex/index data + texture reference directly
  - Skip full Effect/GeometryBinding system; use Renderer's 3D shader
- [ ] `asset/MeshManager.h/.cpp` — List<SmartPtr<Model>> cache (20 bytes)
  - `Load(name)` → ResourceLoader chain
- [ ] `asset/AnimationManager.h/.cpp` — List<Animation*> cache (20 bytes), low priority

---

### Phase 5 — Input (depends on Phase 1+2)

Ref: `docs/engine/input-manager.md`, `docs/engine/touch-system.md`

- [ ] `input/InputEvent.h` — Event struct
  - `uint32_t actionHash` — StringHash of action name
  - `uint32_t actionFlags` — DOWN=1, MOVE=2, UP=4
  - `int fingerId` — touch finger index
  - `float x, y` — position in game coordinates
- [ ] `input/InputManager.h/.cpp` — Action-hash callback dispatch singleton
  - `RegisterInputCallback(uint32_t actionHash, uint32_t flags, Delegate1<bool, InputEvent*> callback)`
  - `DispatchEvent(InputEvent* event)` — iterates registered callbacks matching hash+flags
  - `LoadConfigFile(const char* path)` — parses action config XML
  - `ClearActions()`
  - Migrate from existing `src/platform/InputManager.*`, expand with full API
- [ ] `input/Touch.h/.cpp` — Double-buffered 8-slot multitouch (468 bytes)
  - State (28B): startX/Y, currentX/Y, pointerId, touchId, phase
  - TEvnt (20B): pointerId, isDown, x, y, timestamp
  - Ring buffer of 10 TEvnts
  - `__UpdateInternal()` — push TEvnt to ring buffer
  - `Update(dt)` — drain ring buffer, update back buffer, swap to front
  - `SendIndividualTouchCallbacks()` — emit InputDevice events
- [ ] `input/SDLInputTranslator.h/.cpp` — SDL events → Mortar touch events
  - Migrate from existing `src/platform/SDLInputTranslator.*`
  - SDL_FINGERDOWN/MOVE/UP → Touch::__UpdateInternal
  - Mouse fallback for desktop

---

### Phase 6 — Audio (depends on Phase 1+2)

Ref: `docs/engine/sound-system.md`, `docs/engine/audio-internals.md`

- [ ] `audio/MortarSound.h/.cpp` — Sound instance (16 bytes)
  - vtable, m_Name (char*), m_Handle (uint), m_State (0=idle, 1=paused, 2=playing)
  - `SetVolume(float vol)` — maps 0.0-1.0 to 0-255
- [ ] `audio/SoundManager.h/.cpp` — Abstract base singleton (40 bytes)
  - `std::list<MortarSound*>` m_Sounds
  - Static volume globals (m_SFXVolume, m_MusicVolume, m_SFXMuted, m_MusicMuted)
  - Virtual: `SFXPlay()`, `SFXStop()`, `SFXPauseAll()`, `SFXUnpauseAll()`
  - Virtual: `PreLoadSound()`, `PreLoadSoundEx()`
- [ ] `audio/SoundManagerSDL.h/.cpp` — SDL_AudioCallback backend
  - Replaces the MAM/Bada 4-layer stack with SDL2 raw audio
  - 16 voices, 16kHz, 16-bit mono PCM mixing
  - Uses NLFQueue for thread-safe command passing (matching original pattern)
  - `SFXPlay()` → find free voice, assign sound, start mixing
  - `SFXStop()` → clear voice
  - Audio callback: drain command queue, mix active voices, output
- [ ] `audio/GameSound.h/.cpp` — Game-level 32-slot pool (1800 bytes)
  - 32 SoundSlots (56B each): pSound, nameHash, isFree, volume, pitch, callback
  - `SFXPlay(nameHash, volume, pitch)` — find free slot, delegate to SoundManager
  - Volume formula: `(1 - (1 - masterVol) * vol) * pitch`
  - `FindFree()`, `IsPlaying()`, `Release()`, `KillAll()`

---

### Phase 7 — Entities & Particles (depends on Phase 1+2)

Ref: `docs/engine/actor-manager.md`, `docs/engine/particles.md`

- [ ] `entity/Entity.h` — Base entity class (0x3C bytes)
  - Vtable must match original order: ~dtor, ~dtor, OnActivate, OnDeactivate, Update, Draw, PostUpdate
  - Fields: pos (Vec3), vel (Vec3), angle, scale, flags (byte at +0x0C byte 3), m_EntityType (byte at +0x35)
  - Flag bits: 0x01=inactive, 0x04=updating, 0x08=post-updating, 0x10=pending deactivate, 0x20=no destruct
- [ ] `entity/ActorManager.h/.cpp` — Per-type entity pool (4204 bytes)
  - `Entity* m_FreePool[512]` at +0x008
  - `int m_FreeCount` at +0x808
  - `std::list<Entity*>* m_TypeLists` at +0x1010 (array of per-type lists)
  - `int m_NumTypes` at +0x101C
  - `Delegate1<Entity*, long> m_FactoryDelegate` at +0x1024
  - `Initialise(numTypes, heapSize)` — allocate type lists
  - `Add(entityType, activate)` — recycle from free pool or factory
  - `Update(dt)` — tick all, collect deactivation queue, process
  - `Draw()` — render all active
  - `Deactivate(entity)` — move to free pool
  - `Remove(entity)` — destroy (no recycle)
- [ ] `particle/PSPEmitterTemplate.h` — Loaded template data from XML
- [ ] `particle/PSPParticle.h` — Individual particle (164 bytes)
- [ ] `particle/PSPParticleEmitter.h` — Runtime emitter instance (~0x4C bytes)
- [ ] `particle/PSPParticleManager.h/.cpp` — Template-based emitter singleton (48 bytes)
  - `AddEmitter(hash, pos, ...)` — create emitter from template
  - `Update(dt)` — tick all emitters and particles
  - `Draw()` — render all particles

---

### Phase 8 — Font (depends on Phase 3+4)

Ref: `docs/engine/rendering-pipeline.md` (Font System section)

- [ ] `render/Font.h/.cpp` — BMFont .fnt loader + DrawString (~0x430 bytes)
  - 256 glyph entries, page count, atlas dimensions, scale factor
  - `static SmartPtr<Font> Load(const char* path)` — parse BMFont .fnt text format
  - `DrawString(scale, maxWidth, z, atlas, text, pos, colour, alignment, flags)`:
    - Inline color tags `[FFFFFF]text[/]`
    - Word wrapping at maxWidth
    - Per-glyph quad generation with kerning
    - Multi-page atlas support
    - Alignment flags (0x0F mask): left/center/right/top/bottom
    - Batches into per-page vertex arrays, flushes via DrawTriList
- [ ] `render/BakedString.h/.cpp` — Pre-baked text vertex cache (28 bytes)
  - `m_pTextures` — array of page textures
  - `m_PageCount`, `m_pVertexData`, `m_pVertexCounts`
  - `m_Width`, `m_Height`
  - `Bake(font, text, ...)` — render once via Font::DrawString, cache vertices
  - `Draw()` — iterate pages, bind texture, DrawTriList with cached verts

---

## CMakeLists.txt

Current `src/engine/CMakeLists.txt` defines `mortar_engine` with Phase 1 sources.
Add sources from each phase as implemented. The full target will include ~25 .cpp files.

Legacy `fn_engine` target is kept temporarily for game code using old includes.
Remove it once all game code migrates to `#include "render/Renderer.h"` style includes.

## Migration Checklist

When the engine library is complete, update game code:

- [ ] Remove `add_subdirectory(src/math)` and `add_subdirectory(src/platform)` from root CMakeLists.txt
- [ ] Change all game libs to link `mortar_engine` instead of `fn_engine fn_math fn_platform`
- [ ] Update game includes: `#include "Vec3.h"` → `#include "math/Vec3.h"`
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
